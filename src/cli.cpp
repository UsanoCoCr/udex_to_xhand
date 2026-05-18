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
       << "  --actions <names>          Comma-separated preset names (fist,palm,v,ok) — M5c scope\n"
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
        } else {
            throw std::runtime_error("Unknown flag: " + f);
        }
    }
    if (a.mock && a.receiver_only) {
        throw std::runtime_error("--mock and --receiver-only are mutually exclusive");
    }
    return a;
}

}  // namespace cli
