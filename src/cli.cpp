#include "cli.hpp"

#include <stdexcept>
#include <string>

namespace cli {

void print_usage(std::ostream& os, const char* prog) {
    os << "Usage: " << prog << " [options]\n"
       << "\n"
       << "Options:\n"
       << "  --config <path>            YAML config file (default: config.yaml)\n"
       << "  --mock                     No UDP, no serial; loop example.json (M5b smoke)\n"
       << "  --receiver-only            UDP receive + parse + print; no XHand\n"
       << "  --duration <sec>           Auto-exit after N seconds (default: run until signal)\n"
       << "  --hand <left|right|both>   Which hand(s) to drive (default: both)\n"
       << "  --port <path>              Override xhand.serial_port from config\n"
       << "  --actions <names>          Comma-separated preset names (fist,palm,v,ok) — M3 equiv\n"
       << "                             Single token 'calibrate-udcap' enters M8a calibration mode\n"
       << "  --hold <sec>               Per-preset hold (default: 1.0); only with --actions\n"
       << "  --calibrate-duration <s>   M8a calibration window (default: 30);\n"
       << "                             only with --actions calibrate-udcap\n"
       << "  --help, -h                 Print this message and exit\n";
}

static std::string next_value(int argc, char** argv, int& i, const std::string& flag) {
    if (i + 1 >= argc) {
        throw std::runtime_error("Flag " + flag + " requires a value");
    }
    return std::string(argv[++i]);
}

Args parse(int argc, char** argv) {
    Args a;
    bool hold_seen = false;
    bool duration_seen = false;
    bool calibrate_duration_seen = false;

    for (int i = 1; i < argc; ++i) {
        std::string f = argv[i];
        if (f == "--help" || f == "-h") {
            a.help = true;
        } else if (f == "--config") {
            a.config_path = next_value(argc, argv, i, f);
        } else if (f == "--mock") {
            a.mock = true;
        } else if (f == "--receiver-only") {
            a.receiver_only = true;
        } else if (f == "--duration") {
            a.duration = std::stod(next_value(argc, argv, i, f));
            duration_seen = true;
        } else if (f == "--hand") {
            std::string v = next_value(argc, argv, i, f);
            if      (v == "left")  a.hand = HandSelect::Left;
            else if (v == "right") a.hand = HandSelect::Right;
            else if (v == "both")  a.hand = HandSelect::Both;
            else throw std::runtime_error("--hand must be left|right|both, got: " + v);
        } else if (f == "--port") {
            a.port_override = next_value(argc, argv, i, f);
        } else if (f == "--actions") {
            a.actions = next_value(argc, argv, i, f);
        } else if (f == "--hold") {
            a.hold = std::stod(next_value(argc, argv, i, f));
            hold_seen = true;
        } else if (f == "--calibrate-duration") {
            a.calibrate_duration = std::stod(next_value(argc, argv, i, f));
            calibrate_duration_seen = true;
        } else {
            throw std::runtime_error("Unknown flag: " + f);
        }
    }

    if (a.mock && a.receiver_only) {
        throw std::runtime_error("--mock and --receiver-only are mutually exclusive");
    }
    if (a.actions && a.mock) {
        throw std::runtime_error("--actions and --mock are mutually exclusive");
    }
    if (a.actions && a.receiver_only) {
        throw std::runtime_error("--actions and --receiver-only are mutually exclusive");
    }
    if (a.actions && duration_seen) {
        throw std::runtime_error("--actions and --duration are mutually exclusive "
                                  "(actions total time is N × --hold; for calibrate-udcap use --calibrate-duration)");
    }
    if (hold_seen && !a.actions) {
        throw std::runtime_error("--hold is only valid with --actions");
    }
    // M8a Step A.1: calibrate-udcap mutex / preset compatibility.
    if (calibrate_duration_seen && !a.is_calibrate_udcap()) {
        throw std::runtime_error("--calibrate-duration is only valid with --actions calibrate-udcap");
    }
    if (a.is_calibrate_udcap()) {
        if (hold_seen) {
            throw std::runtime_error("--hold is meaningless with --actions calibrate-udcap; "
                                      "use --calibrate-duration");
        }
        if (a.calibrate_duration <= 0.0) {
            throw std::runtime_error("--calibrate-duration must be > 0");
        }
        // --mock / --receiver-only / --duration already excluded above by the
        // generic --actions guards. --port is allowed but ignored (XHand not opened).
    }

    return a;
}

}  // namespace cli
