// UDCAP → XHand teleoperation main loop.
//
// Modes:
//   --mock           : no UDP, no serial; loop example.json (M5b smoke)
//   --receiver-only  : UDP + parse + print; no XHand
//   (default)        : UDP + mapper + XHand drive
//
// Per CLAUDE.md "Architecture" §, this binary replaces main.py + udcap_receiver.py +
// joint_mapper.py + xhand_driver.py + safety.py from M0-M4.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "calibrate_udcap.hpp"
#include "cli.hpp"
#include "joint_mapper.hpp"
#include "logging.hpp"
#include "preset_actions.hpp"
#include "safety.hpp"
#include "udcap_receiver.hpp"
#include "xhand_driver.hpp"

namespace {

struct UdcapConfig {
    std::string host{"0.0.0.0"};
    int port{9000};
    int timeout_ms{200};         // watchdog stale threshold
    int startup_timeout_s{10};   // M6: startup gate (no calibrated frame → abort)
};

// M7 rev2 (ADR-039): two-port split topology — each XHand on its own
// USB-to-RS485 CDC-ACM endpoint. Defaults reflect observed PC2 mapping
// (2026-05-19): usb 1-2.3 primary → ACM2 = Left, usb 1-2.2 primary → ACM1 = Right.
struct XHandConfig {
    std::string left_serial_port {"/dev/ttyACM2"};
    std::string right_serial_port{"/dev/ttyACM1"};
    int baud_rate{3000000};
    int update_rate_hz{100};
    XHandPID pid{};
};

UdcapConfig load_udcap_config(const YAML::Node& root) {
    UdcapConfig c;
    auto u = root["udcap"];
    if (u) {
        if (u["host"])              c.host              = u["host"].as<std::string>();
        if (u["port"])              c.port              = u["port"].as<int>();
        if (u["timeout_ms"])        c.timeout_ms        = u["timeout_ms"].as<int>();
        if (u["startup_timeout_s"]) c.startup_timeout_s = u["startup_timeout_s"].as<int>();
    }
    return c;
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

std::string read_text_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string fmt_joints_rad(const std::array<double, 12>& v) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) os << ", ";
        os << std::showpos << std::fixed << std::setprecision(3) << v[i];
    }
    os << "]";
    return os.str();
}

std::string fmt_raw24(const std::array<double, 24>& v, char prefix) {
    std::ostringstream os;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) os << " ";
        os << prefix << i << "=" << std::fixed << std::setprecision(1) << v[i];
    }
    return os.str();
}

// Per-frame end-to-end (receive→post-send) latency accumulator. Plan §3.2.
// Bounded by N = update_rate_hz × session_seconds; ≤ 30k for M5c's 30s session,
// so vector + sort for p95 is preferable to streaming approximations.
struct LatencyStats {
    std::vector<double> samples_ms;

    void add(double ms) { samples_ms.push_back(ms); }
    bool empty() const { return samples_ms.empty(); }

    void summary(std::ostream& os) const {
        if (samples_ms.empty()) { os << "latency_ms{no samples}"; return; }
        std::vector<double> s = samples_ms;
        std::sort(s.begin(), s.end());
        const size_t n = s.size();
        const double sum = std::accumulate(s.begin(), s.end(), 0.0);
        auto pct = [&](double q) {
            return s[std::min(n - 1, static_cast<size_t>(q * (n - 1)))];
        };
        os << "latency_ms{n=" << n
           << " min=" << s.front()
           << " avg=" << (sum / n)
           << " p50=" << pct(0.50)
           << " p95=" << pct(0.95)
           << " max=" << s.back() << "}";
    }
};

// M6: Startup gate. Polls `rx.try_recv()` at 100Hz until the first calibrated
// frame arrives, or `timeout` elapses, or `shutdown_flag` flips. Returns the
// frame on success; nullopt on timeout / signal. Logs the wait + the abort.
// See plan §2.2 / §3.1 + ADR-036. Polls at the same 10ms cadence as the main
// loop so wake-up latency is bounded by one tick.
std::optional<UdcapFrame> wait_first_valid_frame(
    UdcapReceiver& rx,
    std::chrono::seconds timeout,
    const std::atomic<bool>& shutdown_flag)
{
    using namespace std::chrono;
    LOG_INFO("waiting for first calibrated UDP frame (timeout="
             << timeout.count() << "s)...");
    const auto deadline = steady_clock::now() + timeout;
    while (!shutdown_flag.load()) {
        if (auto f = rx.try_recv()) return f;
        if (steady_clock::now() >= deadline) {
            LOG_ERROR("startup gate: no calibrated UDP frame in "
                      << timeout.count() << "s; aborting");
            return std::nullopt;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return std::nullopt;
}

// --actions mode (M3 equivalent). No UDP, no mapper — open driver, send each
// preset (deg→rad + safety clamp), hold args.hold seconds, repeat. Plan §3.2.
int run_actions(const cli::Args& args, const XHandConfig& xc) {
    std::vector<const preset_actions::Preset*> list;
    {
        std::stringstream ss(*args.actions);
        std::string name;
        while (std::getline(ss, name, ',')) {
            const auto l = name.find_first_not_of(" \t");
            const auto r = name.find_last_not_of(" \t");
            name = (l == std::string::npos) ? std::string{} : name.substr(l, r - l + 1);
            if (name.empty()) continue;
            const auto* p = preset_actions::find_preset(name);
            if (!p) {
                LOG_ERROR("unknown preset: '" << name << "' (known: fist|palm|v|ok)");
                return 2;
            }
            list.push_back(p);
        }
    }
    if (list.empty()) {
        LOG_ERROR("--actions: empty preset list");
        return 2;
    }

    // M7 rev2: build 0/1/2 XHandDriver instances based on --hand. Each driver
    // owns one CDC-ACM port. Post-open sanity check catches cabling swaps
    // (left port returned a Right hand etc.) — plan rev2 §3.4'.
    const bool want_left  = (args.hand != cli::HandSelect::Right);
    const bool want_right = (args.hand != cli::HandSelect::Left);

    std::optional<XHandDriver> driver_left, driver_right;
    try {
        if (want_left) {
            driver_left.emplace(xc.left_serial_port, xc.baud_rate, xc.pid);
            driver_left->open();
            if (!driver_left->has_left()) {
                LOG_ERROR("xhand.left_serial_port=" << xc.left_serial_port
                          << " did not discover a Left hand (got "
                          << (driver_left->has_right() ? "Right" : "none") << ")");
                return 2;
            }
        }
        if (want_right) {
            driver_right.emplace(xc.right_serial_port, xc.baud_rate, xc.pid);
            driver_right->open();
            if (!driver_right->has_right()) {
                LOG_ERROR("xhand.right_serial_port=" << xc.right_serial_port
                          << " did not discover a Right hand (got "
                          << (driver_right->has_left() ? "Left" : "none") << ")");
                return 2;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("XHandDriver: " << e.what());
        return 2;
    }

    std::atomic<bool> shutdown_flag{false};
    safety::install_signal_handlers(shutdown_flag);

    const auto hold = std::chrono::milliseconds(
        static_cast<long long>(args.hold * 1000.0));

    for (const auto* p : list) {
        if (shutdown_flag.load()) break;

        auto rad = preset_actions::deg_to_rad(p->deg);
        safety::clamp_in_place(rad);

        try {
            if (driver_left)  driver_left ->send_left (rad);
            if (driver_right) driver_right->send_right(rad);
        } catch (const std::exception& e) {
            LOG_ERROR("send: " << e.what());
            break;
        }

        LOG_INFO("Action " << p->name << ": sent 12 joints, OK");

        const auto deadline = std::chrono::steady_clock::now() + hold;
        while (!shutdown_flag.load() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (driver_left)  { try { driver_left ->shutdown(); } catch (const std::exception& e) { LOG_WARN("shutdown(left): "  << e.what()); } }
    if (driver_right) { try { driver_right->shutdown(); } catch (const std::exception& e) { LOG_WARN("shutdown(right): " << e.what()); } }
    return 0;
}

}  // namespace

// M8a Step A.3 — calibrate-udcap mode implementation. External linkage so it
// can be called from main(); declared in calibrate_udcap.hpp.
//
// Loop body deliberately mirrors the FULL mode skeleton (try_recv at 100 Hz,
// safety::install_signal_handlers, calib==3 gate handled inside try_recv).
// The mapper is constructed but its apply_one() is intentionally NOT used —
// we replay weighted+sign+offset directly so the captured value is the
// degree-domain pre-clamp number that the operator pastes back as input_range.
int run_calibrate_udcap(const cli::Args& args) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(args.config_path);
    } catch (const std::exception& e) {
        LOG_ERROR("calibrate-udcap requires --config: " << e.what());
        return 2;
    }

    // Reuse the same loader the FULL mode uses so udcap.host / port / etc.
    // come from a single source.
    UdcapConfig udcap_cfg = load_udcap_config(root);

    // JointMapper enforces mapping.use_new_retarget presence — calibration
    // does not care about the flag's value (it never calls map_left/right),
    // but the schema check catches a missing flag early.
    JointMapper mapper(args.config_path);

    std::optional<UdcapReceiver> receiver;
    try {
        receiver.emplace(udcap_cfg.host, static_cast<uint16_t>(udcap_cfg.port));
    } catch (const std::exception& e) {
        LOG_ERROR("UdcapReceiver: " << e.what());
        return 2;
    }

    const bool want_left  = (args.hand != cli::HandSelect::Right);
    const bool want_right = (args.hand != cli::HandSelect::Left);

    std::atomic<bool> shutdown_flag{false};
    safety::install_signal_handlers(shutdown_flag);

    CalibStats stats{};

    using namespace std::chrono;
    const auto loop_start = steady_clock::now();
    const auto deadline   = loop_start +
        microseconds(static_cast<long long>(args.calibrate_duration * 1'000'000.0));
    const auto period     = microseconds(10'000);   // 100 Hz, same as FULL mode
    auto next_tick        = loop_start;
    auto next_progress    = loop_start + seconds(5);
    bool first_packet_logged = false;

    LOG_INFO("calibrate-udcap: capturing for "
             << std::fixed << std::setprecision(1) << args.calibrate_duration
             << "s on " << udcap_cfg.host << ":" << udcap_cfg.port
             << " (hand="
             << (args.hand == cli::HandSelect::Left  ? "left"
                : args.hand == cli::HandSelect::Right ? "right" : "both")
             << ")");
    LOG_INFO("operator script: 5s palm-open, 5s neutral, then 3s max-flex "
             "per finger (thumb/index/mid/ring/pinky), then 5s full fist");

    // Hot loop. Stats update math (lines below) mirrors apply_one's pre-clamp
    // section exactly so the captured ranges are directly compatible with
    // the M8b input_range field.
    auto record_hand = [&](const std::array<double, 24>& src,
                           std::array<MinMax, 24>& src_stats,
                           const std::array<JointConfig, 12>& cfg,
                           std::array<MinMax, 12>& joint_stats,
                           long& frame_counter) {
        for (size_t i = 0; i < 24; ++i) src_stats[i].update(src[i]);
        for (size_t j = 0; j < 12; ++j) {
            const auto& jc = cfg[j];
            double acc = 0.0;
            for (size_t i = 0; i < jc.sources.size(); ++i) {
                acc += jc.weights[i] * src[jc.sources[i]];
            }
            const double deg = static_cast<double>(jc.sign) * acc + jc.offset;
            joint_stats[j].update(deg);
        }
        ++frame_counter;
    };

    while (!shutdown_flag.load()) {
        const auto now = steady_clock::now();
        if (now >= deadline) break;

        if (auto f = receiver->try_recv()) {
            if (!first_packet_logged) {
                LOG_INFO("first packet from " << receiver->last_sender_ip());
                first_packet_logged = true;
            }
            if (want_left)  record_hand(f->l, stats.left_src,
                                         mapper.left_config(),  stats.left_joint,
                                         stats.frames_left);
            if (want_right) record_hand(f->r, stats.right_src,
                                         mapper.right_config(), stats.right_joint,
                                         stats.frames_right);
        }

        if (now >= next_progress) {
            const double elapsed = duration<double>(now - loop_start).count();
            LOG_INFO("calibrate: elapsed=" << std::fixed << std::setprecision(1)
                     << elapsed << "s  frames_left=" << stats.frames_left
                     << "  frames_right=" << stats.frames_right);
            next_progress = now + seconds(5);
        }

        next_tick += period;
        std::this_thread::sleep_until(next_tick);
    }

    // Dump YAML fragment to stdout. Format mirrors the per-joint entry shape
    // in config.yaml so it can be pasted under mapping.<hand>.<joint>.
    auto dump_fragment = [&](const char* hand_label,
                             const std::array<MinMax, 24>& src_stats,
                             const std::array<MinMax, 12>& joint_stats,
                             long frame_count) {
        std::cout << "# ----- " << hand_label
                  << " (" << frame_count << " frames) -----\n";
        std::cout << "# 24 source min/max (sanity check; should each have "
                  << "max-min >= 30deg after the full operator script):\n";
        for (size_t i = 0; i < 24; ++i) {
            std::cout << "#   " << hand_label[0] << i
                      << ":  [" << std::fixed << std::setprecision(2)
                      << src_stats[i].mn << ", " << src_stats[i].mx << "]"
                      << (src_stats[i].has_data ? "" : "  (NO DATA)")
                      << "\n";
        }
        std::cout << "# Paste under mapping." << hand_label << ".<joint>:\n";
        for (size_t j = 0; j < 12; ++j) {
            std::cout << "#   " << JointMapper::kJointOrder[j]
                      << ":  input_range: ["
                      << std::fixed << std::setprecision(2)
                      << joint_stats[j].mn << ", " << joint_stats[j].mx << "]"
                      << (joint_stats[j].has_data ? "" : "  # NO DATA")
                      << "\n";
        }
    };

    std::cout << "# ===== calibrate-udcap result =====\n"
              << "# duration=" << args.calibrate_duration << "s "
              << "frames_left=" << stats.frames_left << " "
              << "frames_right=" << stats.frames_right << "\n"
              << "# (paste the relevant blocks into config.yaml; for M8b only\n"
              << "#  the 9 four-finger joints are needed; thumb_bend / thumb_rota1\n"
              << "#  / thumb_rota2 ranges are still useful as M8c reference)\n";
    if (want_left)  dump_fragment("left",  stats.left_src,  stats.left_joint,  stats.frames_left);
    if (want_right) dump_fragment("right", stats.right_src, stats.right_joint, stats.frames_right);

    if ((want_left  && stats.frames_left  == 0) ||
        (want_right && stats.frames_right == 0)) {
        LOG_ERROR("calibrate-udcap: 0 frames captured for requested hand(s); "
                  "check UDCAP is running and CalibrationStatus == 3");
        return 2;
    }
    LOG_INFO("calibrate-udcap: done");
    return 0;
}

int main(int argc, char** argv) {
    cli::Args args;
    try {
        args = cli::parse(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Argument error: " << e.what() << "\n\n";
        cli::print_usage(std::cerr, argv[0]);
        return 2;
    }

    if (args.help) {
        cli::print_usage(std::cout, argv[0]);
        return 0;
    }
    if (args.actions) {
        // M8a Step A.3: calibrate-udcap branches off before the preset path
        // because it needs the full config (udcap + mapping sections) and
        // does NOT open the XHand. It is the only `--actions` value that is
        // not a comma-separated preset list.
        if (args.is_calibrate_udcap()) {
            return run_calibrate_udcap(args);
        }
        // M5c: actions mode tolerates missing/invalid config — defaults + --port
        // are sufficient to drive the XHand. Plan §3.2.
        YAML::Node root;
        try {
            root = YAML::LoadFile(args.config_path);
        } catch (const std::exception&) {
            // Silent fall-through to defaults; XHandConfig{} + --port covers
            // the canonical M3-equivalent command shape.
        }
        XHandConfig xc = root.IsNull() ? XHandConfig{} : load_xhand_config(root);
        if (args.port_override) {
            // M7 rev2: --port is a single-side override; ambiguous with --hand both.
            if (args.hand == cli::HandSelect::Both) {
                LOG_ERROR("--port cannot be combined with --hand both; "
                          "use config.yaml left_serial_port / right_serial_port");
                return 2;
            }
            if (args.hand == cli::HandSelect::Left)  xc.left_serial_port  = *args.port_override;
            if (args.hand == cli::HandSelect::Right) xc.right_serial_port = *args.port_override;
        }
        return run_actions(args, xc);
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(args.config_path);
    } catch (const std::exception& e) {
        LOG_ERROR("failed to load config " << args.config_path << ": " << e.what());
        return 2;
    }
    LOG_INFO("config loaded: " << args.config_path);

    UdcapConfig udcap_cfg = load_udcap_config(root);
    XHandConfig xhand_cfg = load_xhand_config(root);
    if (args.port_override) {
        // M7 rev2: same semantics as run_actions — single-side override.
        if (args.hand == cli::HandSelect::Both) {
            LOG_ERROR("--port cannot be combined with --hand both; "
                      "use config.yaml left_serial_port / right_serial_port");
            return 2;
        }
        if (args.hand == cli::HandSelect::Left)  xhand_cfg.left_serial_port  = *args.port_override;
        if (args.hand == cli::HandSelect::Right) xhand_cfg.right_serial_port = *args.port_override;
    }

    const char* mode_str =
          args.mock          ? "MOCK (no UDP, no XHand)"
        : args.receiver_only ? "RECEIVER_ONLY (no XHand)"
        :                      "FULL (UDP + XHand)";
    LOG_INFO("mode: " << mode_str);

    std::atomic<bool> shutdown_flag{false};
    safety::install_signal_handlers(shutdown_flag);

    JointMapper mapper(args.config_path);

    // Mock: parse example.json once (sibling of config.yaml).
    std::optional<UdcapFrame> mock_frame;
    if (args.mock) {
        auto cfg_dir = std::filesystem::path(args.config_path).parent_path();
        auto example_path = (cfg_dir.empty() ? std::filesystem::path(".") : cfg_dir)
                              / "example.json";
        std::string bytes;
        try {
            bytes = read_text_file(example_path.string());
        } catch (const std::exception& e) {
            LOG_ERROR("--mock requires example.json next to config: " << e.what());
            return 2;
        }
        mock_frame = parse_udcap_payload(bytes);
        if (!mock_frame) {
            LOG_ERROR("failed to parse mock example.json");
            return 2;
        }
        LOG_INFO("loaded " << example_path.string() << " (mock source)");
    }

    std::optional<UdcapReceiver> receiver;
    if (!args.mock) {
        try {
            receiver.emplace(udcap_cfg.host, static_cast<uint16_t>(udcap_cfg.port));
        } catch (const std::exception& e) {
            LOG_ERROR("UdcapReceiver: " << e.what());
            return 2;
        }
    }

    // M7 rev2 (ADR-039): two-port split — instantiate one XHandDriver per
    // requested side, each on its own CDC-ACM port. Post-open has_left() /
    // has_right() sanity-check catches cabling swaps. Receiver-only / mock
    // skip both. Fail-closed: any side failing to open exits 2 (driver dtors
    // run mode=0 + close on whichever sides did open before the failure).
    const bool want_left  = (args.hand != cli::HandSelect::Right);
    const bool want_right = (args.hand != cli::HandSelect::Left);

    std::optional<XHandDriver> driver_left, driver_right;
    if (!args.mock && !args.receiver_only) {
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

    // M6: Startup gate (plan §2.2 / §3.1, ADR-036). Skip in mock (uses
    // synthesized frame). In FULL or receiver-only, block until first calibrated
    // frame or timeout. Gate failure → exit 2; in FULL mode the std::optional
    // XHandDriver destructor still runs shutdown() (mode=0 + close_device).
    std::optional<UdcapFrame> primed_frame;
    if (!args.mock) {
        primed_frame = wait_first_valid_frame(
            *receiver,
            std::chrono::seconds(udcap_cfg.startup_timeout_s),
            shutdown_flag);
        if (!primed_frame) return 2;
    }

    safety::Watchdog wdog(std::chrono::milliseconds(udcap_cfg.timeout_ms));

    int rate_hz = std::max(1, xhand_cfg.update_rate_hz);
    auto period = std::chrono::microseconds(1'000'000 / rate_hz);

    auto loop_start = std::chrono::steady_clock::now();
    auto next_tick  = loop_start;

    long tick = 0;
    long valid_frames = 0;
    bool first_packet_logged = false;
    LatencyStats latency_stats;

    // M6 (plan §3.2): per-frame send cache for stale-resend; recovery tracking.
    // last_warn_ts is initialized far in the past so the first stale WARN
    // fires immediately on entry to the stale branch.
    std::optional<std::array<double, 12>> last_left_rad, last_right_rad;
    bool was_stale = false;
    auto last_warn_ts = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    auto duration_elapsed = [&]() {
        if (args.duration <= 0) return false;
        using namespace std::chrono;
        auto e = duration<double>(steady_clock::now() - loop_start).count();
        return e >= args.duration;
    };

    while (!shutdown_flag.load() && !duration_elapsed()) {
        auto tick_start = std::chrono::steady_clock::now();
        ++tick;

        std::optional<UdcapFrame> frame_opt;
        if (args.mock) {
            frame_opt = *mock_frame;
            frame_opt->recv_ts = tick_start;
        } else if (primed_frame) {
            // M6: consume the gate-supplied first frame so the first loop
            // tick is productive (don't waste it on another try_recv).
            frame_opt = std::move(primed_frame);
            primed_frame.reset();
        } else {
            frame_opt = receiver->try_recv();
        }

        if (frame_opt) {
            ++valid_frames;
            wdog.update(frame_opt->recv_ts);

            // M6: stale → fresh transition. last_warn_ts was the time of the
            // most recent stale WARN; gap from that to now approximates the
            // outage length seen by the operator.
            if (was_stale) {
                using namespace std::chrono;
                const auto gap_ms =
                    duration<double, std::milli>(tick_start - last_warn_ts).count();
                LOG_INFO("watchdog: recovered after " << gap_ms << "ms");
                was_stale = false;
            }

            if (!first_packet_logged && !args.mock) {
                LOG_INFO("first packet from " << receiver->last_sender_ip());
                first_packet_logged = true;
            }

            std::optional<std::array<double, 12>> left_rad, right_rad;
            try {
                if (args.hand != cli::HandSelect::Right) {
                    auto v = mapper.map_left(frame_opt->l);
                    safety::clamp_in_place(v);
                    left_rad = v;
                    if (driver_left) {
                        driver_left->send_left(v);
                        last_left_rad = v;     // M6: cache for stale resend
                    }
                }
                if (args.hand != cli::HandSelect::Left) {
                    auto v = mapper.map_right(frame_opt->r);
                    safety::clamp_in_place(v);
                    right_rad = v;
                    if (driver_right) {
                        driver_right->send_right(v);
                        last_right_rad = v;    // M6: cache for stale resend
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("send: " << e.what());
                shutdown_flag.store(true);
                break;
            }

            // Plan §3.2: sample receive→post-send latency once per frame,
            // only in FULL mode (any driver engaged). recv_ts is steady_clock.
            if (driver_left || driver_right) {
                using namespace std::chrono;
                const auto t1 = steady_clock::now();
                const double ms =
                    duration<double, std::milli>(t1 - frame_opt->recv_ts).count();
                latency_stats.add(ms);
            }

            if (args.mock) {
                std::cout << "[tick " << std::setw(3) << std::setfill(' ') << tick << "]";
                if (left_rad)  std::cout << " L: " << fmt_joints_rad(*left_rad);
                if (right_rad) std::cout << " R: " << fmt_joints_rad(*right_rad);
                std::cout << std::endl;
            } else if (args.receiver_only) {
                using namespace std::chrono;
                double elapsed = duration<double>(tick_start - loop_start).count();
                double fps = elapsed > 0 ? static_cast<double>(valid_frames) / elapsed : 0.0;
                std::cout << "[recv " << std::setw(3) << std::setfill(' ') << valid_frames << "] "
                          << "L: " << fmt_raw24(frame_opt->l, 'l')
                          << " | R: " << fmt_raw24(frame_opt->r, 'r')
                          << " | calib L=" << frame_opt->calib_left
                          << " R=" << frame_opt->calib_right
                          << " | fps=" << std::fixed << std::setprecision(1) << fps
                          << std::endl;
            }
        } else if ((driver_left || driver_right) && wdog.has_seen_frame() && wdog.is_stale(tick_start)) {
            // M6 (plan §2.1 / §3.2, ADR-035): stale — resend last commanded
            // rad to keep the position controller's last setpoint alive over
            // RS485; WARN log rate-limited to 1Hz so 30-min stress still fits.
            // Stale ticks do NOT update latency_stats (no fresh recv_ts).
            // M7 rev2: route per side to its own driver.
            try {
                if (driver_left  && last_left_rad)  driver_left ->send_left (*last_left_rad);
                if (driver_right && last_right_rad) driver_right->send_right(*last_right_rad);
            } catch (const std::exception& e) {
                LOG_ERROR("stale resend: " << e.what());
                shutdown_flag.store(true);
                break;
            }
            using namespace std::chrono;
            if (tick_start - last_warn_ts >= seconds(1)) {
                LOG_WARN("watchdog: no UDP for >"
                         << udcap_cfg.timeout_ms << "ms, holding last position");
                last_warn_ts = tick_start;
            }
            was_stale = true;
        }
        // else: no frame, but either no driver (mock / receiver-only) or
        //   watchdog not yet stale — nothing to do this tick.

        next_tick += period;
        std::this_thread::sleep_until(next_tick);
    }

    // M7 rev2: each side shuts down its own driver. Independently wrapped so a
    // failure on one side cannot prevent mode=0 + close on the other.
    if (driver_left) {
        try { driver_left->shutdown(); }
        catch (const std::exception& e) { LOG_WARN("shutdown(left): " << e.what()); }
    }
    if (driver_right) {
        try { driver_right->shutdown(); }
        catch (const std::exception& e) { LOG_WARN("shutdown(right): " << e.what()); }
    }

    if (!latency_stats.empty()) {
        std::ostringstream lat;
        latency_stats.summary(lat);
        LOG_INFO(lat.str());
    }

    using namespace std::chrono;
    double total = duration<double>(steady_clock::now() - loop_start).count();
    std::ostringstream tail;
    tail << "exited after " << std::fixed << std::setprecision(1) << total << "s, "
         << tick << " ticks, " << valid_frames << " valid frames";
    if (receiver) tail << ", " << receiver->parse_errors() << " parse errors";
    LOG_INFO(tail.str());
    return 0;
}
