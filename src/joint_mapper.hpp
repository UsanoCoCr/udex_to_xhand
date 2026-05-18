#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

struct JointConfig {
    std::vector<int> sources;
    std::vector<double> weights;
    int sign{1};
    double offset{0.0};
    double clamp_min{0.0};
    double clamp_max{0.0};
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

 private:
    std::array<JointConfig, 12> left_;
    std::array<JointConfig, 12> right_;

    static std::array<JointConfig, 12> load_hand(const std::string& yaml_path,
                                                  const std::string& hand_key);
    static double apply_one(const JointConfig& jc,
                            const std::array<double, 24>& src);
};
