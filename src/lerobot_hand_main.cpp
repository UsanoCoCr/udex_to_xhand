// LeRobot binary-hand → XHand teleoperation entry point.
//
// Second, fully independent input path alongside udex_to_xhand: receives realtime
// 38-dim LeRobot `action` frames over UDP/JSON (default port 9100, separate from
// UDCAP's 9000), extracts action[36]=left_hand_closed / action[37]=right_hand_closed,
// and drives each XHand to a grasp (closed) or open pose with smooth, rate-limited
// motion.
//
// Self-contained: own minimal CLI + xhand-config loader (does NOT touch cli.cpp /
// main.cpp). Reuses XHandDriver, safety primitives, and the palm/fist preset poses.
// See docs/superpowers/specs/2026-06-28-lerobot-binary-hand-sdk-design.md.
//
// Modes:
//   (default)   UDP + controller + XHand drive
//   --dry-run   UDP + parse + print decisions; no serial, no XHand

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <yaml-cpp/yaml.h>

#include "lerobot_hand_controller.hpp"
#include "lerobot_receiver.hpp"
#include "logging.hpp"
#include "preset_actions.hpp"
#include "safety.hpp"
#include "xhand_driver.hpp"

namespace {

enum class HandSelect { Left, Right, Both };

struct Args {
    std::string config_path{"config.yaml"};
    std::string udp_host{"0.0.0.0"};
    int udp_port{9100};
    HandSelect hand{HandSelect::Both};
    int rate_hz{100};               // control loop rate; 0 → take from config / default
    double transition_ms{400.0};
    int debounce_frames{0};
    int watchdog_ms{200};
    std::string open_pose{"palm"};
    std::string close_pose{"fist"};
    double duration{0.0};           // 0 = run until signal
    bool dry_run{false};
    bool help{false};
};

// Mirrors the xhand section in config.yaml (see main.cpp load_xhand_config). Kept
// local so this binary does not depend on main.cpp's anonymous-namespace loader.
struct XHandConfig {
    std::string left_serial_port {"/dev/ttyACM2"};
    std::string right_serial_port{"/dev/ttyACM1"};
    int baud_rate{3000000};
    int update_rate_hz{100};
    XHandPID pid{};
};

void print_usage(std::ostream& os, const char* prog) {
    os << "Usage: " << prog << " [options]\n"
       << "\n"
       << "Drives XHand grasp/open from a realtime LeRobot action UDP/JSON stream.\n"
       << "Reads action[36]=left_hand_closed, action[37]=right_hand_closed only.\n"
       << "\n"
       << "Options:\n"
       << "  --config <path>            YAML config (xhand section); default: config.yaml\n"
       << "  --udp-port <n>             UDP listen port (default: 9100)\n"
       << "  --udp-host <ip>            Bind address (default: 0.0.0.0)\n"
       << "  --hand <left|right|both>   Which hand(s) to drive (default: both)\n"
       << "  --rate <hz>                Control loop rate (default: 100)\n"
       << "  --transition-ms <ms>       Open<->close transition time (default: 400; 0 = snap)\n"
       << "  --debounce-frames <n>      Require n stable bits before switching (default: 0)\n"
       << "  --watchdog-ms <ms>         Hold last pose if no UDP for this long (default: 200)\n"
       << "  --open-pose <name>         Preset for 'open'  (fist|palm|v|ok; default: palm)\n"
       << "  --close-pose <name>        Preset for 'close' (fist|palm|v|ok; default: fist)\n"
       << "  --duration <sec>           Auto-exit after N seconds (default: run until signal)\n"
       << "  --dry-run                  No serial/XHand; parse UDP and print decisions\n"
       << "  --help, -h                 Print this message and exit\n";
}

std::string next_value(int argc, char** argv, int& i, const std::string& flag) {
    if (i + 1 >= argc) throw std::runtime_error("Flag " + flag + " requires a value");
    return std::string(argv[++i]);
}

Args parse_args(int argc, char** argv) {
    Args a;
    bool rate_seen = false;
    for (int i = 1; i < argc; ++i) {
        std::string f = argv[i];
        if (f == "--help" || f == "-h") {
            a.help = true;
        } else if (f == "--config") {
            a.config_path = next_value(argc, argv, i, f);
        } else if (f == "--udp-port") {
            a.udp_port = std::stoi(next_value(argc, argv, i, f));
        } else if (f == "--udp-host") {
            a.udp_host = next_value(argc, argv, i, f);
        } else if (f == "--hand") {
            std::string v = next_value(argc, argv, i, f);
            if      (v == "left")  a.hand = HandSelect::Left;
            else if (v == "right") a.hand = HandSelect::Right;
            else if (v == "both")  a.hand = HandSelect::Both;
            else throw std::runtime_error("--hand must be left|right|both, got: " + v);
        } else if (f == "--rate") {
            a.rate_hz = std::stoi(next_value(argc, argv, i, f));
            rate_seen = true;
        } else if (f == "--transition-ms") {
            a.transition_ms = std::stod(next_value(argc, argv, i, f));
        } else if (f == "--debounce-frames") {
            a.debounce_frames = std::stoi(next_value(argc, argv, i, f));
        } else if (f == "--watchdog-ms") {
            a.watchdog_ms = std::stoi(next_value(argc, argv, i, f));
        } else if (f == "--open-pose") {
            a.open_pose = next_value(argc, argv, i, f);
        } else if (f == "--close-pose") {
            a.close_pose = next_value(argc, argv, i, f);
        } else if (f == "--duration") {
            a.duration = std::stod(next_value(argc, argv, i, f));
        } else if (f == "--dry-run") {
            a.dry_run = true;
        } else {
            throw std::runtime_error("Unknown flag: " + f);
        }
    }
    if (a.udp_port <= 0 || a.udp_port > 65535) {
        throw std::runtime_error("--udp-port out of range: " + std::to_string(a.udp_port));
    }
    if (rate_seen && a.rate_hz <= 0) {
        throw std::runtime_error("--rate must be > 0");
    }
    return a;
}

XHandConfig load_xhand_config(const YAML::Node& root) {
    XHandConfig c;
    auto x = root["xhand"];
    if (x) {
        if (x["left_serial_port"])  c.left_serial_port  = x["left_serial_port"].as<std::string>();
        if (x["right_serial_port"]) c.right_serial_port = x["right_serial_port"].as<std::string>();
        if (x["baud_rate"])         c.baud_rate         = x["baud_rate"].as<int>();
        if (x["update_rate_hz"])    c.update_rate_hz    = x["update_rate_hz"].as<int>();
        if (x["default_kp"])        c.pid.kp            = x["default_kp"].as<int>();
        if (x["default_ki"])        c.pid.ki            = x["default_ki"].as<int>();
        if (x["default_kd"])        c.pid.kd            = x["default_kd"].as<int>();
        if (x["default_tor_max"])   c.pid.tor_max       = x["default_tor_max"].as<int>();
        if (x["control_mode"])      c.pid.mode          = x["control_mode"].as<int>();
    }
    return c;
}

std::array<double, 12> resolve_pose_deg(const std::string& name) {
    const auto* p = preset_actions::find_preset(name);
    if (!p) {
        throw std::runtime_error("unknown pose preset: '" + name + "' (known: fist|palm|v|ok)");
    }
    return p->deg;
}

// Wait for the first valid UDP frame or timeout / signal. Mirrors main.cpp's
// startup gate cadence (10ms poll). Returns the frame on success, nullopt on
// timeout / shutdown.
std::optional<LerobotHandFrame> wait_first_frame(LerobotReceiver& rx,
                                                 std::chrono::seconds timeout,
                                                 const std::atomic<bool>& shutdown_flag) {
    using namespace std::chrono;
    LOG_INFO("waiting for first LeRobot UDP frame (timeout=" << timeout.count() << "s)...");
    const auto deadline = steady_clock::now() + timeout;
    while (!shutdown_flag.load()) {
        if (auto f = rx.try_recv()) return f;
        if (steady_clock::now() >= deadline) {
            LOG_ERROR("startup gate: no LeRobot UDP frame in " << timeout.count()
                      << "s; aborting");
            return std::nullopt;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    try {
        args = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Argument error: " << e.what() << "\n\n";
        print_usage(std::cerr, argv[0]);
        return 2;
    }
    if (args.help) {
        print_usage(std::cout, argv[0]);
        return 0;
    }

    // Config: needed for serial ports / PID in drive mode. In dry-run it is
    // optional (we never open the XHand), so tolerate a missing file there.
    YAML::Node root;
    try {
        root = YAML::LoadFile(args.config_path);
    } catch (const std::exception& e) {
        if (!args.dry_run) {
            LOG_ERROR("failed to load config " << args.config_path << ": " << e.what());
            return 2;
        }
        LOG_WARN("config " << args.config_path << " not loaded (" << e.what()
                 << "); dry-run continues with defaults");
    }
    XHandConfig xhand_cfg = root.IsNull() ? XHandConfig{} : load_xhand_config(root);

    // Resolve open/close poses up front so a bad name fails before opening hardware.
    std::array<double, 12> open_deg, close_deg;
    try {
        open_deg  = resolve_pose_deg(args.open_pose);
        close_deg = resolve_pose_deg(args.close_pose);
    } catch (const std::exception& e) {
        LOG_ERROR(e.what());
        return 2;
    }

    const int rate_hz = args.rate_hz > 0 ? args.rate_hz
                       : (xhand_cfg.update_rate_hz > 0 ? xhand_cfg.update_rate_hz : 100);

    LOG_INFO("mode: " << (args.dry_run ? "DRY-RUN (no XHand)" : "FULL (UDP + XHand)")
             << "  hand=" << (args.hand == HandSelect::Left  ? "left"
                            : args.hand == HandSelect::Right ? "right" : "both")
             << "  udp=" << args.udp_host << ":" << args.udp_port
             << "  rate=" << rate_hz << "Hz  transition=" << args.transition_ms << "ms"
             << "  open=" << args.open_pose << " close=" << args.close_pose);

    std::atomic<bool> shutdown_flag{false};
    safety::install_signal_handlers(shutdown_flag);

    const bool want_left  = (args.hand != HandSelect::Right);
    const bool want_right = (args.hand != HandSelect::Left);

    // Open drivers (skip in dry-run). Post-open L/R sanity check catches cabling
    // swaps, exactly as main.cpp does. Fail-closed: any side failing → exit 2.
    std::optional<XHandDriver> driver_left, driver_right;
    if (!args.dry_run) {
        try {
            if (want_left) {
                driver_left.emplace(xhand_cfg.left_serial_port, xhand_cfg.baud_rate, xhand_cfg.pid);
                driver_left->open();
                if (!driver_left->has_left()) {
                    LOG_ERROR("xhand.left_serial_port=" << xhand_cfg.left_serial_port
                              << " did not discover a Left hand (got "
                              << (driver_left->has_right() ? "Right" : "none") << ")");
                    return 2;
                }
            }
            if (want_right) {
                driver_right.emplace(xhand_cfg.right_serial_port, xhand_cfg.baud_rate, xhand_cfg.pid);
                driver_right->open();
                if (!driver_right->has_right()) {
                    LOG_ERROR("xhand.right_serial_port=" << xhand_cfg.right_serial_port
                              << " did not discover a Right hand (got "
                              << (driver_right->has_left() ? "Left" : "none") << ")");
                    return 2;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("XHandDriver: " << e.what());
            return 2;
        }
    }

    std::optional<LerobotReceiver> receiver;
    try {
        receiver.emplace(args.udp_host, static_cast<uint16_t>(args.udp_port));
    } catch (const std::exception& e) {
        LOG_ERROR("LerobotReceiver: " << e.what());
        return 2;
    }

    // Startup gate: block until first valid frame (or timeout / signal) before
    // entering the drive loop. Skipped in dry-run so it can idle waiting for data.
    std::optional<LerobotHandFrame> primed_frame;
    if (!args.dry_run) {
        primed_frame = wait_first_frame(*receiver, std::chrono::seconds(10), shutdown_flag);
        if (!primed_frame) return 2;
    }

    LerobotControllerConfig cc;
    cc.open_pose_deg   = open_deg;
    cc.close_pose_deg  = close_deg;
    cc.transition_ms   = args.transition_ms;
    cc.tick_period_ms  = 1000.0 / rate_hz;
    cc.debounce_frames = args.debounce_frames;
    LerobotHandController controller(
        driver_left  ? &*driver_left  : nullptr,
        driver_right ? &*driver_right : nullptr,
        cc);

    safety::Watchdog wdog(std::chrono::milliseconds(args.watchdog_ms));
    auto period = std::chrono::microseconds(1'000'000 / rate_hz);

    auto loop_start = std::chrono::steady_clock::now();
    auto next_tick  = loop_start;
    long tick = 0;
    long valid_frames = 0;
    bool first_packet_logged = false;
    bool was_stale = false;
    auto last_warn_ts = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    // Track last-printed decision so dry-run only logs on change.
    std::optional<std::pair<bool, bool>> last_decision;

    auto duration_elapsed = [&]() {
        if (args.duration <= 0) return false;
        using namespace std::chrono;
        return duration<double>(steady_clock::now() - loop_start).count() >= args.duration;
    };

    while (!shutdown_flag.load() && !duration_elapsed()) {
        auto tick_start = std::chrono::steady_clock::now();
        ++tick;

        std::optional<LerobotHandFrame> frame_opt;
        if (primed_frame) {
            frame_opt = std::move(primed_frame);
            primed_frame.reset();
        } else {
            frame_opt = receiver->try_recv();
        }

        if (frame_opt) {
            ++valid_frames;
            wdog.update(frame_opt->recv_ts);
            if (was_stale) {
                using namespace std::chrono;
                const auto gap_ms =
                    duration<double, std::milli>(tick_start - last_warn_ts).count();
                LOG_INFO("watchdog: recovered after " << gap_ms << "ms");
                was_stale = false;
            }
            if (!first_packet_logged) {
                LOG_INFO("first packet from " << receiver->last_sender_ip());
                first_packet_logged = true;
            }

            controller.set_target(frame_opt->left_closed, frame_opt->right_closed);

            auto decision = std::make_pair(frame_opt->left_closed, frame_opt->right_closed);
            if (args.dry_run && (!last_decision || *last_decision != decision)) {
                LOG_INFO("[dry-run] L=" << (decision.first  ? "CLOSE" : "open ")
                         << "  R=" << (decision.second ? "CLOSE" : "open "));
                last_decision = decision;
            }
        }

        // Always tick: advances phase toward the latest target and resends the
        // current pose (this also "holds last position" when the stream stalls).
        if (!args.dry_run) {
            try {
                controller.tick();
            } catch (const std::exception& e) {
                LOG_ERROR("send: " << e.what());
                shutdown_flag.store(true);
                break;
            }
        }

        // Rate-limited stale WARN (1Hz), mirroring main.cpp. Pose hold is already
        // handled by tick() resending every cycle.
        if (!frame_opt && wdog.has_seen_frame() && wdog.is_stale(tick_start)) {
            using namespace std::chrono;
            if (tick_start - last_warn_ts >= seconds(1)) {
                LOG_WARN("watchdog: no UDP for >" << args.watchdog_ms
                         << "ms, holding last position");
                last_warn_ts = tick_start;
            }
            was_stale = true;
        }

        next_tick += period;
        std::this_thread::sleep_until(next_tick);
    }

    if (driver_left) {
        try { driver_left->shutdown(); }
        catch (const std::exception& e) { LOG_WARN("shutdown(left): " << e.what()); }
    }
    if (driver_right) {
        try { driver_right->shutdown(); }
        catch (const std::exception& e) { LOG_WARN("shutdown(right): " << e.what()); }
    }

    using namespace std::chrono;
    double total = duration<double>(steady_clock::now() - loop_start).count();
    std::ostringstream tail;
    tail << "exited after " << total << "s, " << tick << " ticks, "
         << valid_frames << " valid frames";
    if (receiver) tail << ", " << receiver->parse_errors() << " parse errors";
    LOG_INFO(tail.str());
    return 0;
}
