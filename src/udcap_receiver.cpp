#include "udcap_receiver.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "logging.hpp"

std::optional<UdcapFrame> parse_udcap_payload(const std::string& json_bytes) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_bytes);
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
    if (!j.is_object() || j.empty()) return std::nullopt;

    // UDCAP shape: { "<frame_key>": { "Parameter": [ {Name, Value}, ... ] } }
    auto inner_it = j.begin();
    if (!inner_it.value().is_object()) return std::nullopt;
    const auto& inner = inner_it.value();
    auto pit = inner.find("Parameter");
    if (pit == inner.end() || !pit->is_array()) return std::nullopt;

    // Build Name → Value lookup once. Skip entries with non-string Name or missing Value.
    std::unordered_map<std::string, const nlohmann::json*> values;
    values.reserve(pit->size());
    for (const auto& p : *pit) {
        if (!p.is_object()) continue;
        auto nit = p.find("Name");
        auto vit = p.find("Value");
        if (nit == p.end() || vit == p.end() || !nit->is_string()) continue;
        values[nit->get<std::string>()] = &vit.value();
    }

    auto get_double = [&](const std::string& name) -> double {
        auto it = values.find(name);
        if (it == values.end() || !it->second->is_number()) return 0.0;
        return it->second->get<double>();
    };
    auto get_int = [&](const std::string& name) -> int {
        auto it = values.find(name);
        if (it == values.end() || !it->second->is_number()) return 0;
        return it->second->get<int>();
    };

    UdcapFrame frame;
    for (int i = 0; i < 24; ++i) {
        frame.l[i] = get_double("l" + std::to_string(i));
        frame.r[i] = get_double("r" + std::to_string(i));
    }
    frame.calib_left  = get_int("L_CalibrationStatus");
    frame.calib_right = get_int("R_CalibrationStatus");
    return frame;
}

UdcapReceiver::UdcapReceiver(const std::string& bind_host, uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));
    }

    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        int e = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("fcntl O_NONBLOCK failed: " + std::string(std::strerror(e)));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind_host.empty() || bind_host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else if (::inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr) != 1) {
        int e = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("inet_pton(" + bind_host + ") failed: " +
                                  std::string(std::strerror(e)));
    }

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int e = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("bind(" + (bind_host.empty() ? "0.0.0.0" : bind_host) + ":" +
                                  std::to_string(port) + ") failed: " +
                                  std::string(std::strerror(e)));
    }

    LOG_INFO("UDP bound " << (bind_host.empty() ? "0.0.0.0" : bind_host) << ":" << port);
}

UdcapReceiver::~UdcapReceiver() {
    if (fd_ >= 0) ::close(fd_);
}

std::optional<UdcapFrame> UdcapReceiver::try_recv() {
    std::vector<char> buf(65535);
    sockaddr_in src{};
    socklen_t slen = sizeof(src);

    // Drain to the latest packet (matches Python udcap_receiver.py:46-54).
    std::string latest;
    std::string latest_ip;
    while (true) {
        slen = sizeof(src);
        ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0,
                                reinterpret_cast<sockaddr*>(&src), &slen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOG_WARN("recvfrom: " << std::strerror(errno));
            break;
        }
        latest.assign(buf.data(), buf.data() + n);
        char ipbuf[INET_ADDRSTRLEN] = {0};
        if (::inet_ntop(AF_INET, &src.sin_addr, ipbuf, sizeof(ipbuf))) {
            latest_ip.assign(ipbuf);
        }
    }

    if (latest.empty()) return std::nullopt;
    if (!latest_ip.empty()) last_sender_ip_ = latest_ip;

    auto frame = parse_udcap_payload(latest);
    if (!frame) {
        ++parse_errors_;
        return std::nullopt;
    }
    // SPEC §5: only forward calibrated data.
    if (frame->calib_left != 3 || frame->calib_right != 3) return std::nullopt;

    frame->recv_ts = std::chrono::steady_clock::now();
    return frame;
}
