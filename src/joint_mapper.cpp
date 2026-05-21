#include "joint_mapper.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include <yaml-cpp/yaml.h>

// Match Python's math.pi exactly (IEEE-754 double 0x400921FB54442D18).
static constexpr double kDeg2Rad = 3.141592653589793 / 180.0;

JointMapper::JointMapper(const std::string& yaml_path) {
    // M8a Step A.0: read the master retarget switch before parsing per-hand
    // entries. Required field — explicit `false` is the supported M7-baseline
    // setting; missing key fails fast so an unintended schema regression can
    // never silently fall back to M7 with the operator thinking they enabled
    // the new path. See docs/plans/20260521-m8-tuning-acceptance-plan.md §0.
    YAML::Node root = YAML::LoadFile(yaml_path);
    auto mapping = root["mapping"];
    if (!mapping || !mapping.IsMap()) {
        throw std::runtime_error(yaml_path + ": missing top-level 'mapping' map");
    }
    if (!mapping["use_new_retarget"]) {
        throw std::runtime_error(
            yaml_path + ": mapping.use_new_retarget required "
            "(set to false to keep M7 baseline; true enables M8 retarget pipeline)");
    }
    use_new_retarget_ = mapping["use_new_retarget"].as<bool>();

    left_  = load_hand(yaml_path, "left",  use_new_retarget_);
    right_ = load_hand(yaml_path, "right", use_new_retarget_);
}

std::array<JointConfig, 12>
JointMapper::load_hand(const std::string& yaml_path, const std::string& hand_key,
                       bool use_new_retarget) {
    YAML::Node root = YAML::LoadFile(yaml_path);
    auto mapping = root["mapping"];
    if (!mapping || !mapping.IsMap()) {
        throw std::runtime_error(yaml_path + ": missing top-level 'mapping' map");
    }
    auto hand = mapping[hand_key];
    if (!hand || !hand.IsMap()) {
        throw std::runtime_error(yaml_path + ": missing mapping." + hand_key + " map");
    }

    std::array<JointConfig, 12> out{};
    for (size_t i = 0; i < kJointOrder.size(); ++i) {
        const std::string name = kJointOrder[i];
        auto node = hand[name];
        if (!node || !node.IsMap()) {
            throw std::runtime_error(yaml_path + ": missing mapping." + hand_key + "." + name);
        }

        JointConfig jc;
        if (!node["sources"]) throw std::runtime_error(yaml_path + ": " + hand_key + "." + name + ".sources required");
        if (!node["weights"]) throw std::runtime_error(yaml_path + ": " + hand_key + "." + name + ".weights required");
        if (!node["sign"])    throw std::runtime_error(yaml_path + ": " + hand_key + "." + name + ".sign required");
        if (!node["clamp"])   throw std::runtime_error(yaml_path + ": " + hand_key + "." + name + ".clamp required");

        jc.sources = node["sources"].as<std::vector<int>>();
        jc.weights = node["weights"].as<std::vector<double>>();
        jc.sign    = node["sign"].as<int>();
        jc.offset  = node["offset"] ? node["offset"].as<double>() : 0.0;

        auto clamp = node["clamp"].as<std::vector<double>>();
        if (clamp.size() != 2) {
            throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                      ".clamp must have exactly 2 elements");
        }
        jc.clamp_min = clamp[0];
        jc.clamp_max = clamp[1];

        if (jc.sources.size() != jc.weights.size()) {
            throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                      ": sources/weights length mismatch (" +
                                      std::to_string(jc.sources.size()) + " vs " +
                                      std::to_string(jc.weights.size()) + ")");
        }
        for (int s : jc.sources) {
            if (s < 0 || s > 23) {
                throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                          ": source index " + std::to_string(s) +
                                          " out of range [0, 23]");
            }
        }

        // M8b Step B.2: optional affine rescale fields. Schema validation
        // (size == 2, both-or-none) is independent of the master switch so
        // a config-only typo still fails fast.
        if (node["input_range"]) {
            auto r = node["input_range"].as<std::vector<double>>();
            if (r.size() != 2) {
                throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                          ".input_range must have exactly 2 elements");
            }
            jc.input_range = std::make_pair(r[0], r[1]);
        }
        if (node["output_range"]) {
            auto r = node["output_range"].as<std::vector<double>>();
            if (r.size() != 2) {
                throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                          ".output_range must have exactly 2 elements");
            }
            jc.output_range = std::make_pair(r[0], r[1]);
        }
        if (jc.input_range.has_value() != jc.output_range.has_value()) {
            throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                      ": input_range and output_range must be both present or both absent");
        }

        // Flag-gating happens at load time so apply_one's hot path stays
        // branch-free. Plan §0 and ADR-048: false → strip the optional
        // entries so the M7 weighted-sum + sign + offset + clamp path is
        // bit-identical regardless of what config.yaml actually filled in.
        if (!use_new_retarget) {
            jc.input_range.reset();
            jc.output_range.reset();
        }

        out[i] = std::move(jc);
    }
    return out;
}

double JointMapper::apply_one(const JointConfig& jc,
                               const std::array<double, 24>& src) {
    // Mirror Python joint_mapper.py:76-82 — left-to-right accumulation, no fsum.
    double acc = 0.0;
    for (size_t i = 0; i < jc.sources.size(); ++i) {
        acc += jc.weights[i] * src[jc.sources[i]];
    }
    double deg = static_cast<double>(jc.sign) * acc + jc.offset;
    // M8b Step B.3: optional affine rescale. Both ranges already stripped
    // to nullopt by load_hand when use_new_retarget=false, so this branch
    // truly never fires on the M7 path. Arithmetic order matches the
    // Python oracle (legacy_python/joint_mapper.py) so ‖C++ − Python‖∞
    // stays ≤ 1e-17 rad after fixture regen.
    if (jc.input_range && jc.output_range) {
        const double in_min  = jc.input_range->first;
        const double in_max  = jc.input_range->second;
        const double out_min = jc.output_range->first;
        const double out_max = jc.output_range->second;
        const double span    = in_max - in_min;
        if (span > 1e-9) {
            const double ratio = (deg - in_min) / span;
            deg = ratio * (out_max - out_min) + out_min;
        } else {
            // Degenerate: input range collapsed → take the output midpoint
            // rather than divide by zero. Calibration tool warns about this
            // but the mapper must still produce a defined value.
            deg = (out_min + out_max) * 0.5;
        }
    }
    deg = std::max(jc.clamp_min, std::min(jc.clamp_max, deg));   // ADR-020: clamp in degrees
    return deg * kDeg2Rad;                                        // ADR-021: convert once at boundary
}

std::array<double, 12>
JointMapper::map_left(const std::array<double, 24>& src) const {
    std::array<double, 12> out{};
    for (size_t i = 0; i < 12; ++i) out[i] = apply_one(left_[i], src);
    return out;
}

std::array<double, 12>
JointMapper::map_right(const std::array<double, 24>& src) const {
    std::array<double, 12> out{};
    for (size_t i = 0; i < 12; ++i) out[i] = apply_one(right_[i], src);
    return out;
}
