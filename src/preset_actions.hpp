#pragma once

// Preset 12-joint poses used by the --actions mode (M3 equivalent in C++).
// Values are byte-identical to legacy_python/xhand_driver.py:9-14 — equivalence is
// asserted by docs/plans/20260518-m5c-pc2-hardware-revalidation-plan.md §6.1.
// Header-only: 4 × 12 doubles + two trivial helpers don't warrant a .cpp.

#include <array>
#include <cmath>
#include <string_view>

namespace preset_actions {

inline constexpr int kNumJoints = 12;
inline constexpr int kNumPresets = 4;

struct Preset {
    const char* name;
    std::array<double, kNumJoints> deg;
};

inline constexpr std::array<Preset, kNumPresets> kPresets = {{
    {"fist", { 11.85, 74.58, 40.0, -3.08,106.02,110.0, 109.75,107.56,107.66,110.0,109.10,109.15}},
    {"palm", {  0.00, 80.66, 33.2,  0.00,  5.11,  0.0,   6.53,  0.00,  6.76,  4.41, 10.13,  0.00}},
    {"v",    { 38.32, 90.00, 52.08, 6.21,  2.60,  0.0,   2.10,  0.00,110.00,110.00,110.00,109.23}},
    {"ok",   { 45.88, 41.54, 67.35, 2.22, 80.45, 70.82, 31.37, 10.39, 13.69, 16.88,  1.39, 10.55}},
}};

inline std::array<double, kNumJoints> deg_to_rad(const std::array<double, kNumJoints>& deg) {
    std::array<double, kNumJoints> rad{};
    for (int i = 0; i < kNumJoints; ++i) {
        rad[i] = deg[i] * M_PI / 180.0;
    }
    return rad;
}

inline const Preset* find_preset(std::string_view name) {
    for (const auto& p : kPresets) {
        if (name == p.name) return &p;
    }
    return nullptr;
}

}  // namespace preset_actions
