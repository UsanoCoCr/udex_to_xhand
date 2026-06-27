#pragma once

// Core SDK: maps the two binary hand bits (left_closed / right_closed) to XHand
// 12-DOF poses and drives the hand(s) with smooth, rate-limited motion.
//
// Transport-agnostic: it knows nothing about UDP or LeRobot — it takes booleans
// via set_target() and pushes radian poses to XHandDriver via tick(). The UDP
// ingest (lerobot_receiver) and loop wiring (lerobot_hand_main) sit on top.
//
// Mapping: closed → close_pose (default `fist`), open → open_pose (default `palm`).
// Each hand carries a phase in [0,1] (0 = open, 1 = closed) advanced by a fixed
// step per tick so a full open<->close transition takes ~transition_ms; the
// commanded pose is lerp(open, close, phase). This keeps the grasp gentle and
// makes a flickering input bit reverse smoothly instead of slamming.

#include <array>

#include "xhand_driver.hpp"

struct LerobotControllerConfig {
    // Open / close target poses in DEGREES (converted to rad + clamped at ctor).
    // Defaults are zero-initialised; lerobot_hand_main fills them from the
    // preset_actions palm/fist tables (or --open-pose/--close-pose).
    std::array<double, 12> open_pose_deg{};
    std::array<double, 12> close_pose_deg{};

    double transition_ms{400.0};  // full open<->close transition duration
    double tick_period_ms{10.0};  // control-loop period (= 1000 / rate_hz)
    int debounce_frames{0};       // require N consecutive identical bits before
                                  // committing a new target (0 = commit immediately)
};

class LerobotHandController {
 public:
    // left / right may be nullptr when that side is not selected / not present.
    // The pointers are borrowed (owned by the caller) and must outlive this object.
    LerobotHandController(XHandDriver* left, XHandDriver* right,
                          const LerobotControllerConfig& cfg);

    // Update desired open/closed state from the latest decoded frame. Honors the
    // debounce window: a new value must persist debounce_frames calls before it
    // becomes the active target.
    void set_target(bool left_closed, bool right_closed);

    // Advance each hand's phase one step toward its target, compute the lerped
    // pose, clamp, and send to the driver(s). Call once per control tick. Sends
    // every tick (resends the steady pose), which also satisfies "hold last
    // position" when the input stream stalls.
    void tick();

    // Explicitly resend the most recent commanded pose without advancing phase.
    // Equivalent to tick() at the steady state; provided for the watchdog path.
    void hold();

    // Current phase (0 = fully open, 1 = fully closed); exposed for logging.
    double left_phase()  const { return phase_left_; }
    double right_phase() const { return phase_right_; }

 private:
    void send_current();
    static std::array<double, 12> lerp(const std::array<double, 12>& a,
                                       const std::array<double, 12>& b, double t);

    XHandDriver* left_;
    XHandDriver* right_;

    std::array<double, 12> open_rad_{};
    std::array<double, 12> close_rad_{};
    double phase_step_{1.0};

    double phase_left_{0.0};
    double phase_right_{0.0};
    bool target_left_closed_{false};
    bool target_right_closed_{false};

    // Debounce bookkeeping (per hand): the candidate value and how many
    // consecutive set_target calls have proposed it.
    int debounce_frames_{0};
    bool cand_left_{false};
    bool cand_right_{false};
    int cand_left_count_{0};
    int cand_right_count_{0};

    std::array<double, 12> last_left_rad_{};
    std::array<double, 12> last_right_rad_{};
    bool have_sent_left_{false};
    bool have_sent_right_{false};
};
