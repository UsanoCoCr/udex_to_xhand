#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>

struct UdcapFrame {
    std::array<double, 24> l{};                       // degrees, UDCAP convention (negative = flexion)
    std::array<double, 24> r{};
    int calib_left{0};                                // 0..3
    int calib_right{0};
    std::chrono::steady_clock::time_point recv_ts{};  // populated by try_recv / main loop
};

// Pure helper — parses a single UDCAP JSON payload into a frame.
// Returns nullopt on malformed JSON / missing required structure.
// Does NOT enforce CalibrationStatus == 3 (caller's job; see SPEC §5).
std::optional<UdcapFrame> parse_udcap_payload(const std::string& json_bytes);

class UdcapReceiver {
 public:
    UdcapReceiver(const std::string& bind_host, uint16_t port);
    ~UdcapReceiver();

    UdcapReceiver(const UdcapReceiver&) = delete;
    UdcapReceiver& operator=(const UdcapReceiver&) = delete;

    // Drains the socket buffer to the latest packet, parses, validates calib.
    // Returns nullopt when: no data (EAGAIN), parse error (counter incremented),
    // or calib != 3 on either hand (SPEC §5).
    std::optional<UdcapFrame> try_recv();

    const std::string& last_sender_ip() const { return last_sender_ip_; }
    std::size_t parse_errors() const { return parse_errors_; }

 private:
    int fd_{-1};
    std::string last_sender_ip_;
    std::size_t parse_errors_{0};
};
