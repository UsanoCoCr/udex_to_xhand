#pragma once

// M8a Step A.2 — UDCAP range capture mode (no XHand).
//
// Provided as a header-only interface; the implementation lives in main.cpp
// alongside run_actions() so we don't have to extend CMakeLists.txt for this
// one entry point (matches the ADR-032 / ADR-034 pattern that keeps
// --actions infrastructure in main.cpp).
//
// Usage: ./udex_to_xhand --actions calibrate-udcap [--calibrate-duration N] \
//                       --config <path> [--hand left|right|both]
//
// Behavior:
//   - Opens UdcapReceiver on udcap.host:udcap.port from config.yaml.
//   - Constructs JointMapper(config_path) so the per-hand JointConfig
//     {sources, weights, sign, offset} is available; the calibration loop
//     replays the weighted-sum + sign + offset math itself (NOT through
//     apply_one, which would clamp + convert to radians).
//   - Loops at 100 Hz for --calibrate-duration seconds, tracking per-source
//     and per-joint (pre-clamp degrees) min/max for the requested hand(s).
//   - Every 5 s prints a progress line to stderr.
//   - On exit prints a YAML fragment to stdout that can be pasted into
//     config.yaml under mapping.<hand>.<joint>.input_range.
//   - Does NOT open the XHand serial port; safe to run without hardware on
//     the bus and ignores --port if passed.
//   - Respects SIGINT / SIGTERM via the same safety::install_signal_handlers
//     hook the main control loop uses.

#include <array>

#include "cli.hpp"

// Min/max tracker with sentinel "uninitialized" state via has_data flag so
// we can distinguish "no frames seen for this hand" from "all zeros". Plan
// §3 M8a Step A.3 simple stats.
struct MinMax {
    double mn{0.0};
    double mx{0.0};
    bool has_data{false};

    void update(double v) {
        if (!has_data) { mn = mx = v; has_data = true; return; }
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
};

struct CalibStats {
    // Raw source ranges (24 UDCAP parameters per hand) — sanity check that
    // the operator actually moved each finger through its full range.
    std::array<MinMax, 24> left_src{};
    std::array<MinMax, 24> right_src{};
    // Joint pre-clamp ranges (12 XHand joints per hand, value =
    // sign * weighted_sum(sources, weights) + offset, in degrees). These
    // are what get pasted into config.yaml as input_range entries.
    std::array<MinMax, 12> left_joint{};
    std::array<MinMax, 12> right_joint{};
    long frames_left{0};
    long frames_right{0};
};

// Entry point. Returns process exit code (0 on success, 2 on error). Reads
// args.config_path, args.calibrate_duration, args.hand.
int run_calibrate_udcap(const cli::Args& args);
