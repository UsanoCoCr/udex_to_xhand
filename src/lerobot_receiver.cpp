#include "lerobot_receiver.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "logging.hpp"

namespace {

// action[36] / action[37] per the dataset spec (data_stepit action layout).
constexpr std::size_t kLeftClosedIdx  = 36;
constexpr std::size_t kRightClosedIdx = 37;
constexpr std::size_t kActionMinLen   = 38;

}  // namespace

std::optional<LerobotHandFrame> parse_lerobot_payload(const std::string& json_bytes,
                                                      double threshold) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_bytes);
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
    if (!j.is_object()) return std::nullopt;

    // Shape 1: { "action": [ ...>=38 numbers... ] } — the canonical LeRobot/VLA shape.
    auto ait = j.find("action");
    if (ait != j.end() && ait->is_array()) {
        const auto& action = *ait;
        if (action.size() < kActionMinLen) return std::nullopt;
        const auto& lv = action[kLeftClosedIdx];
        const auto& rv = action[kRightClosedIdx];
        if (!lv.is_number() || !rv.is_number()) return std::nullopt;
        LerobotHandFrame f;
        f.left_closed  = lv.get<double>() > threshold;
        f.right_closed = rv.get<double>() > threshold;
        return f;
    }

    // Shape 2: { "left_closed": <num>, "right_closed": <num> } — convenience for
    // simple producers and the UDP test sender.
    auto lit = j.find("left_closed");
    auto rit = j.find("right_closed");
    if (lit != j.end() && rit != j.end() && lit->is_number() && rit->is_number()) {
        LerobotHandFrame f;
        f.left_closed  = lit->get<double>() > threshold;
        f.right_closed = rit->get<double>() > threshold;
        return f;
    }

    return std::nullopt;
}

LerobotReceiver::LerobotReceiver(const std::string& bind_host, uint16_t port, double threshold)
    : threshold_(threshold) {
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

    LOG_INFO("LeRobot UDP bound " << (bind_host.empty() ? "0.0.0.0" : bind_host) << ":" << port);
}

LerobotReceiver::~LerobotReceiver() {
    if (fd_ >= 0) ::close(fd_);
}

std::optional<LerobotHandFrame> LerobotReceiver::try_recv() {
    std::vector<char> buf(65535);
    sockaddr_in src{};
    socklen_t slen = sizeof(src);

    // Drain to the latest packet (matches udcap_receiver.cpp:119-133).
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

    auto frame = parse_lerobot_payload(latest, threshold_);
    if (!frame) {
        ++parse_errors_;
        return std::nullopt;
    }

    frame->recv_ts = std::chrono::steady_clock::now();
    return frame;
}
