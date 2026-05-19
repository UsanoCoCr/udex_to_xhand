// M6 — Unit tests for safety primitives.
//
// Hand-rolled assertion style (same shape as tests/test_mapper_snapshot.cpp);
// no GoogleTest / Catch2 dependency (plan §3.4 "no new dependencies").
//
// Built only when -DBUILD_TESTS=ON. Does NOT link xhand_control — pure C++
// against safety primitives. Runs on darwin/arm64 host without vendor .so.

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "safety.hpp"

namespace {
int g_failures = 0;

void check(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::fprintf(stderr, "[FAIL] %s:%d %s\n", file, line, expr);
        ++g_failures;
    }
}
#define CHECK(c) check((c), #c, __FILE__, __LINE__)

void test_watchdog_fresh_is_stale() {
    safety::Watchdog w(std::chrono::milliseconds(200));
    auto now = std::chrono::steady_clock::now();
    CHECK(!w.has_seen_frame());
    CHECK( w.is_stale(now));        // No frame ever → stale by contract
}

void test_watchdog_after_update_not_stale() {
    safety::Watchdog w(std::chrono::milliseconds(200));
    auto t0 = std::chrono::steady_clock::now();
    w.update(t0);
    CHECK( w.has_seen_frame());
    CHECK(!w.is_stale(t0));                                              // 0ms
    CHECK(!w.is_stale(t0 + std::chrono::milliseconds(100)));             // 100ms < 200ms
    CHECK(!w.is_stale(t0 + std::chrono::milliseconds(200)));             // exactly at boundary — '>' in impl
    CHECK( w.is_stale(t0 + std::chrono::milliseconds(201)));             // 201ms > 200ms
    CHECK( w.is_stale(t0 + std::chrono::milliseconds(5000)));            // far past
}

void test_watchdog_update_resets_staleness() {
    safety::Watchdog w(std::chrono::milliseconds(200));
    auto t0 = std::chrono::steady_clock::now();
    w.update(t0);
    CHECK( w.is_stale(t0 + std::chrono::milliseconds(500)));
    w.update(t0 + std::chrono::milliseconds(450));                       // late update
    CHECK(!w.is_stale(t0 + std::chrono::milliseconds(500)));             // 50ms after fresh update
}

void test_clamp_in_place_within_range_noop() {
    std::array<double, 12> v{};
    for (int i = 0; i < 12; ++i) v[i] = 0.1 * i;                         // all small positive rad
    auto orig = v;
    safety::clamp_in_place(v);
    for (int i = 0; i < 12; ++i) CHECK(v[i] == orig[i]);
}

void test_clamp_in_place_clamps_high_low() {
    std::array<double, 12> v{};
    v[0] = 100.0;                                                        // way above kHardMaxRad ≈ 1.9199
    v[1] = -100.0;                                                       // way below kHardMinRad ≈ -1.5708
    v[2] = safety::kHardMaxRad;                                          // exactly upper — should remain
    v[3] = safety::kHardMinRad;                                          // exactly lower — should remain
    safety::clamp_in_place(v);
    CHECK(std::fabs(v[0] - safety::kHardMaxRad) < 1e-12);
    CHECK(std::fabs(v[1] - safety::kHardMinRad) < 1e-12);
    CHECK(std::fabs(v[2] - safety::kHardMaxRad) < 1e-12);
    CHECK(std::fabs(v[3] - safety::kHardMinRad) < 1e-12);
    for (int i = 4; i < 12; ++i) CHECK(v[i] == 0.0);
}
}  // namespace

int main() {
    test_watchdog_fresh_is_stale();
    test_watchdog_after_update_not_stale();
    test_watchdog_update_resets_staleness();
    test_clamp_in_place_within_range_noop();
    test_clamp_in_place_clamps_high_low();
    std::fprintf(stderr, "[test_safety] %s (failures=%d)\n",
                 g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
