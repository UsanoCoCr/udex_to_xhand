#pragma once

#include <array>
#include <atomic>
#include <chrono>

namespace safety {

// XHand physical envelope: -90° to 110° per joint (HandAnglePose_t comment, data_type.hpp:147).
// Used as the radian-domain fail-safe clamp (ADR-021 two-layer defense), applied AFTER
// the mapper's degree-domain clamp. Hardcoded here to match Python safety.py HARD_LIMITS_RAD.
inline constexpr double kHardMinRad = -90.0  * (3.14159265358979323846 / 180.0);
inline constexpr double kHardMaxRad =  110.0 * (3.14159265358979323846 / 180.0);

// Clamps each element to [kHardMinRad, kHardMaxRad].
void clamp_in_place(std::array<double, 12>& rad);

// Watchdog tracks the time of the last valid UDP frame. is_stale returns true if
// the most recent update is older than `timeout` or if no frame has ever been seen.
// The "hold last position on stale" reaction is M6 scope; M5b only wires the class.
class Watchdog {
 public:
    explicit Watchdog(std::chrono::milliseconds timeout);
    void update(std::chrono::steady_clock::time_point t);
    bool is_stale(std::chrono::steady_clock::time_point now) const;
    bool has_seen_frame() const { return has_seen_frame_; }

 private:
    std::chrono::milliseconds timeout_;
    std::chrono::steady_clock::time_point last_ok_{};
    bool has_seen_frame_{false};
};

// Registers SIGINT + SIGTERM handlers that flip the shared atomic flag.
// Handler body is async-signal-safe (only an atomic store) per CLAUDE.md safety rules.
void install_signal_handlers(std::atomic<bool>& shutdown_flag);

}  // namespace safety
