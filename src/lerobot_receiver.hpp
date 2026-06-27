#pragma once

// LeRobot action-stream UDP receiver — the second, fully independent input path
// alongside the UDCAP receiver (src/udcap_receiver.{hpp,cpp}). Ingests realtime
// 38-dim LeRobot `action` frames over UDP/JSON and extracts ONLY the two binary
// hand dims:
//
//   action[36] = left_hand_closed   (0 = open, 1 = closed)
//   action[37] = right_hand_closed
//
// Everything else in the 38-dim vector (base / legs / waist / arms) is ignored —
// this path drives the XHand grasp/open only. See
// docs/superpowers/specs/2026-06-28-lerobot-binary-hand-sdk-design.md.
//
// Shape mirrors UdcapReceiver: non-blocking socket, drain-to-latest, parse error
// → skip frame (never drive on corrupt data).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// One decoded hand command: the two binary bits + receive timestamp.
struct LerobotHandFrame {
    bool left_closed{false};
    bool right_closed{false};
    std::chrono::steady_clock::time_point recv_ts{};  // populated by try_recv
};

// Pure helper — parses a single LeRobot JSON payload into a hand frame.
// Accepted shapes (first match wins):
//   1. { "action": [ ...>=38 numbers... ] }  → left=action[36]>thr, right=action[37]>thr
//   2. { "left_closed": <num>, "right_closed": <num> }  → used directly (convenience
//      for simple producers / the test sender)
// Returns nullopt on malformed JSON / missing structure / action shorter than 38.
std::optional<LerobotHandFrame> parse_lerobot_payload(const std::string& json_bytes,
                                                      double threshold = 0.5);

class LerobotReceiver {
 public:
    LerobotReceiver(const std::string& bind_host, uint16_t port, double threshold = 0.5);
    ~LerobotReceiver();

    LerobotReceiver(const LerobotReceiver&) = delete;
    LerobotReceiver& operator=(const LerobotReceiver&) = delete;

    // Drains the socket buffer to the latest packet, parses, returns the decoded
    // hand frame. Returns nullopt when: no data (EAGAIN) or parse error
    // (counter incremented).
    std::optional<LerobotHandFrame> try_recv();

    const std::string& last_sender_ip() const { return last_sender_ip_; }
    std::size_t parse_errors() const { return parse_errors_; }

 private:
    int fd_{-1};
    double threshold_{0.5};
    std::string last_sender_ip_;
    std::size_t parse_errors_{0};
};
