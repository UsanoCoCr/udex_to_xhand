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
    std::optional<std::string> actions;         // Comma-separated preset names (fist|palm|v|ok); also accepts the
                                                // single token "calibrate-udcap" for M8a calibration mode.
    double hold{1.0};                           // Per-preset hold seconds; only meaningful with --actions
    double calibrate_duration{30.0};            // M8a: --actions calibrate-udcap capture window seconds
    bool help{false};

    // M8a Step A.1: convenience predicate so main.cpp can dispatch without
    // re-checking the string literal.
    bool is_calibrate_udcap() const {
        return actions && *actions == "calibrate-udcap";
    }
};

// Parses argv. Throws std::runtime_error on malformed input.
Args parse(int argc, char** argv);

void print_usage(std::ostream& os, const char* prog);

}  // namespace cli
