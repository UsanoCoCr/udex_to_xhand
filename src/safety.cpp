#include "safety.hpp"

#include <algorithm>
#include <csignal>

namespace safety {

namespace {
std::atomic<bool>* g_shutdown_flag = nullptr;

extern "C" void on_signal(int /*signum*/) {
    // Async-signal-safe: only an atomic store.
    if (g_shutdown_flag) g_shutdown_flag->store(true);
}
}  // namespace

void clamp_in_place(std::array<double, 12>& rad) {
    for (auto& v : rad) v = std::max(kHardMinRad, std::min(kHardMaxRad, v));
}

Watchdog::Watchdog(std::chrono::milliseconds timeout) : timeout_(timeout) {}

void Watchdog::update(std::chrono::steady_clock::time_point t) {
    last_ok_ = t;
    has_seen_frame_ = true;
}

bool Watchdog::is_stale(std::chrono::steady_clock::time_point now) const {
    if (!has_seen_frame_) return true;
    return (now - last_ok_) > timeout_;
}

void install_signal_handlers(std::atomic<bool>& shutdown_flag) {
    g_shutdown_flag = &shutdown_flag;
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
}

}  // namespace safety
