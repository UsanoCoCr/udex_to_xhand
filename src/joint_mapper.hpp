#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct JointConfig {
    std::vector<int> sources;
    std::vector<double> weights;
    int sign{1};
    double offset{0.0};
    double clamp_min{0.0};
    double clamp_max{0.0};
    // M8b Step B.1: optional per-joint affine rescale. Both fields required
    // together (both-or-none, enforced in load_hand). Only consulted when
    // JointMapper::use_new_retarget() is true — load_hand resets both to
    // nullopt under flag=false so apply_one's hot path never re-checks the
    // flag (cold-path gating, see docs/plans/20260521-m8-tuning-acceptance-plan.md §0).
    std::optional<std::pair<double, double>> input_range;
    std::optional<std::pair<double, double>> output_range;
};

class JointMapper {
 public:
    // Authoritative joint order J0..J11 — mirrors Python JOINT_ORDER (ADR-022).
    static constexpr std::array<const char*, 12> kJointOrder = {
        "thumb_bend",   "thumb_rota1",  "thumb_rota2",
        "index_bend",   "index_joint1", "index_joint2",
        "mid_joint1",   "mid_joint2",
        "ring_joint1",  "ring_joint2",
        "pinky_joint1", "pinky_joint2",
    };

    explicit JointMapper(const std::string& yaml_path);

    std::array<double, 12> map_left (const std::array<double, 24>& src) const;
    std::array<double, 12> map_right(const std::array<double, 24>& src) const;

    // M8a Step A.0: master switch driving M8 retarget pipeline (four-finger
    // affine rescale + thumb retargeting). Default false → M7-baseline
    // mapping (weighted-sum + sign + offset + clamp + deg→rad). When false
    // the loader strips input_range/output_range so apply_one stays on the
    // M7 path. See docs/plans/20260521-m8-tuning-acceptance-plan.md §0.
    bool use_new_retarget() const { return use_new_retarget_; }

    // M8a Step A.3: read-only access to the per-hand JointConfig array so
    // the calibrate-udcap mode can compute the same weighted+sign+offset
    // pre-clamp value as apply_one() (without going through clamp/deg→rad).
    const std::array<JointConfig, 12>& left_config()  const { return left_;  }
    const std::array<JointConfig, 12>& right_config() const { return right_; }

 private:
    bool use_new_retarget_{false};
    std::array<JointConfig, 12> left_;
    std::array<JointConfig, 12> right_;

    static std::array<JointConfig, 12> load_hand(const std::string& yaml_path,
                                                  const std::string& hand_key,
                                                  bool use_new_retarget);
    static double apply_one(const JointConfig& jc,
                            const std::array<double, 24>& src);
};
