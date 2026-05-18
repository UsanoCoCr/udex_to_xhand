// UDCAP → XHand teleoperation main loop.
//
// Modes:
//   --mock           : no UDP, no serial; loop example.json (M5b smoke)
//   --receiver-only  : UDP + parse + print; no XHand
//   (default)        : UDP + mapper + XHand drive
//
// Per CLAUDE.md "Architecture" §, this binary replaces main.py + udcap_receiver.py +
// joint_mapper.py + xhand_driver.py + safety.py from M0-M4.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include <yaml-cpp/yaml.h>

#include "cli.hpp"
#include "joint_mapper.hpp"
#include "logging.hpp"
#include "safety.hpp"
#include "udcap_receiver.hpp"
#include "xhand_driver.hpp"

namespace {

struct UdcapConfig {
    std::string host{"0.0.0.0"};
    int port{9000};
    int timeout_ms{200};
};

struct XHandConfig {
    std::string serial_port{"/dev/ttyACM0"};
    int baud_rate{3000000};
    int update_rate_hz{100};
    XHandPID pid{};
};

UdcapConfig load_udcap_config(const YAML::Node& root) {
    UdcapConfig c;
    auto u = root["udcap"];
    if (u) {
        if (u["host"])       c.host       = u["host"].as<std::string>();
        if (u["port"])       c.port       = u["port"].as<int>();
        if (u["timeout_ms"]) c.timeout_ms = u["timeout_ms"].as<int>();
    }
    return c;
}

XHandConfig load_xhand_config(const YAML::Node& root) {
    XHandConfig c;
    auto x = root["xhand"];
    if (x) {
        if (x["serial_port"])     c.serial_port    = x["serial_port"].as<std::string>();
        if (x["baud_rate"])       c.baud_rate      = x["baud_rate"].as<int>();
        if (x["update_rate_hz"])  c.update_rate_hz = x["update_rate_hz"].as<int>();
        if (x["default_kp"])      c.pid.kp         = x["default_kp"].as<int>();
        if (x["default_ki"])      c.pid.ki         = x["default_ki"].as<int>();
        if (x["default_kd"])      c.pid.kd         = x["default_kd"].as<int>();
        if (x["default_tor_max"]) c.pid.tor_max    = x["default_tor_max"].as<int>();
        if (x["control_mode"])    c.pid.mode       = x["control_mode"].as<int>();
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

}  // namespace

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
        std::cerr << "ERROR: --actions (preset-action mode) is M5c scope; "
                  << "not implemented in M5b. Use the vendor test_serial from M5a "
                  << "(xhand_control_sdk/tests/build/test_serial) for now.\n";
        return 2;
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
    if (args.port_override) xhand_cfg.serial_port = *args.port_override;

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

    std::optional<XHandDriver> driver;
    if (!args.mock && !args.receiver_only) {
        try {
            driver.emplace(xhand_cfg.serial_port, xhand_cfg.baud_rate, xhand_cfg.pid);
            driver->open();
        } catch (const std::exception& e) {
            LOG_ERROR("XHandDriver: " << e.what());
            return 2;
        }
    }

    safety::Watchdog wdog(std::chrono::milliseconds(udcap_cfg.timeout_ms));

    int rate_hz = std::max(1, xhand_cfg.update_rate_hz);
    auto period = std::chrono::microseconds(1'000'000 / rate_hz);

    auto loop_start = std::chrono::steady_clock::now();
    auto next_tick  = loop_start;

    long tick = 0;
    long valid_frames = 0;
    bool first_packet_logged = false;

    if (!args.mock) LOG_INFO("waiting for first packet...");

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
        } else {
            frame_opt = receiver->try_recv();
        }

        if (frame_opt) {
            ++valid_frames;
            wdog.update(frame_opt->recv_ts);
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
                    if (driver) driver->send_left(v);
                }
                if (args.hand != cli::HandSelect::Left) {
                    auto v = mapper.map_right(frame_opt->r);
                    safety::clamp_in_place(v);
                    right_rad = v;
                    if (driver) driver->send_right(v);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("send: " << e.what());
                shutdown_flag.store(true);
                break;
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
        }

        next_tick += period;
        std::this_thread::sleep_until(next_tick);
    }

    if (driver) {
        try { driver->shutdown(); }
        catch (const std::exception& e) { LOG_WARN("shutdown: " << e.what()); }
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
