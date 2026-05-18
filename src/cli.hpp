#pragma once

#include <optional>
#include <ostream>
#include <string>

namespace cli {

enum class HandSelect { Left, Right, Both };

struct Args {
    std::string config_path{"config.yaml"};
    bool mock{false};
    bool receiver_only{false};
    double duration{0.0};                  // 0 = run until signal
    HandSelect hand{HandSelect::Both};
    std::optional<std::string> port_override;   // overrides xhand.serial_port
    std::optional<std::string> actions;         // Comma-separated preset names (M3 equivalent); see preset_actions.hpp
    double hold{1.0};                           // Per-preset hold seconds; only meaningful with --actions
    bool help{false};
};

// Parses argv. Throws std::runtime_error on malformed input.
Args parse(int argc, char** argv);

void print_usage(std::ostream& os, const char* prog);

}  // namespace cli
