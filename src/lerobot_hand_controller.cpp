#include "lerobot_hand_controller.hpp"

#include <algorithm>
#include <cmath>

#include "safety.hpp"

namespace {

// deg → rad for a full 12-joint pose, then apply the radian-domain hard clamp so
// the open/close endpoints can never command past the physical envelope.
std::array<double, 12> deg_pose_to_clamped_rad(const std::array<double, 12>& deg) {
    std::array<double, 12> rad{};
    for (int i = 0; i < 12; ++i) rad[i] = deg[i] * M_PI / 180.0;
    safety::clamp_in_place(rad);
    return rad;
}

}  // namespace

LerobotHandController::LerobotHandController(XHandDriver* left, XHandDriver* right,
                                            const LerobotControllerConfig& cfg)
    : left_(left), right_(right), debounce_frames_(std::max(0, cfg.debounce_frames)) {
    open_rad_  = deg_pose_to_clamped_rad(cfg.open_pose_deg);
    close_rad_ = deg_pose_to_clamped_rad(cfg.close_pose_deg);

    // Per-tick phase increment so a full transition spans transition_ms. A
    // non-positive transition_ms means "snap instantly" (step = 1.0).
    if (cfg.transition_ms <= 0.0 || cfg.tick_period_ms <= 0.0) {
        phase_step_ = 1.0;
    } else {
        phase_step_ = std::min(1.0, cfg.tick_period_ms / cfg.transition_ms);
    }

    // Start fully open (safe startup posture).
    phase_left_ = 0.0;
    phase_right_ = 0.0;
}

std::array<double, 12> LerobotHandController::lerp(const std::array<double, 12>& a,
                                                  const std::array<double, 12>& b, double t) {
    std::array<double, 12> out{};
    for (int i = 0; i < 12; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
    return out;
}

void LerobotHandController::set_target(bool left_closed, bool right_closed) {
    // Debounce per hand: a proposed value must repeat debounce_frames_ times
    // before it becomes the active target. With debounce_frames_ == 0 the value
    // commits immediately.
    auto commit = [](bool proposed, int needed, bool& cand, int& count, bool& target) {
        if (proposed == target) { count = 0; return; }
        if (proposed == cand) {
            ++count;
        } else {
            cand = proposed;
            count = 1;
        }
        if (count > needed) {
            target = proposed;
            count = 0;
        }
    };
    commit(left_closed,  debounce_frames_, cand_left_,  cand_left_count_,  target_left_closed_);
    commit(right_closed, debounce_frames_, cand_right_, cand_right_count_, target_right_closed_);
}

void LerobotHandController::tick() {
    auto advance = [this](double& phase, bool target_closed) {
        const double goal = target_closed ? 1.0 : 0.0;
        if (phase < goal) {
            phase = std::min(goal, phase + phase_step_);
        } else if (phase > goal) {
            phase = std::max(goal, phase - phase_step_);
        }
    };
    if (left_)  advance(phase_left_,  target_left_closed_);
    if (right_) advance(phase_right_, target_right_closed_);
    send_current();
}

void LerobotHandController::hold() {
    // Resend the last commanded pose without advancing phase. If nothing has been
    // sent yet, fall back to commanding the current (startup-open) pose.
    if (!have_sent_left_ && !have_sent_right_) {
        send_current();
        return;
    }
    if (left_  && have_sent_left_)  left_->send_left(last_left_rad_);
    if (right_ && have_sent_right_) right_->send_right(last_right_rad_);
}

void LerobotHandController::send_current() {
    if (left_) {
        auto pose = lerp(open_rad_, close_rad_, phase_left_);
        safety::clamp_in_place(pose);   // defense in depth (endpoints already clamped)
        left_->send_left(pose);
        last_left_rad_ = pose;
        have_sent_left_ = true;
    }
    if (right_) {
        auto pose = lerp(open_rad_, close_rad_, phase_right_);
        safety::clamp_in_place(pose);
        right_->send_right(pose);
        last_right_rad_ = pose;
        have_sent_right_ = true;
    }
}
