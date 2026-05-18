#include "joint_mapper.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include <yaml-cpp/yaml.h>

// Match Python's math.pi exactly (IEEE-754 double 0x400921FB54442D18).
static constexpr double kDeg2Rad = 3.141592653589793 / 180.0;

JointMapper::JointMapper(const std::string& yaml_path) {
    left_  = load_hand(yaml_path, "left");
    right_ = load_hand(yaml_path, "right");
}

std::array<JointConfig, 12>
JointMapper::load_hand(const std::string& yaml_path, const std::string& hand_key) {
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
