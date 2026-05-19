#include "xhand_driver.hpp"

#include <cctype>
#include <cstring>
#include <stdexcept>
#include <string>

#include "data_type.hpp"
#include "logging.hpp"

XHandDriver::XHandDriver(const std::string& port, int baud, const XHandPID& pid)
    : port_(port), baud_(baud), pid_(pid) {}

XHandDriver::~XHandDriver() {
    if (opened_) {
        // Destructor must not throw — swallow any shutdown error.
        try { shutdown(); } catch (...) {}
    }
}

void XHandDriver::open(bool require_both) {
    auto err = ctl_.open_serial(port_, static_cast<uint32_t>(baud_));
    if (!err) {
        throw std::runtime_error("open_serial(" + port_ + ", " + std::to_string(baud_) +
                                  ") failed: " + std::to_string(err.error_code) + " " +
                                  err.error_message);
    }
    opened_ = true;

    LOG_INFO("XHand SDK version: " << ctl_.get_sdk_version());
    LOG_INFO("Serial: " << port_ << " @ " << baud_ << " baud");

    auto ids = ctl_.list_hands_id();
    if (ids.empty()) {
        throw std::runtime_error("list_hands_id() returned empty — no XHand on bus");
    }

    for (uint8_t id : ids) {
        auto [terr, htype] = ctl_.get_hand_type(id);
        if (!terr) {
            LOG_WARN("get_hand_type(" << static_cast<int>(id) << ") failed: "
                     << terr.error_code << " " << terr.error_message);
            continue;
        }
        char c = htype.empty()
                   ? 'N'
                   : static_cast<char>(std::toupper(static_cast<unsigned char>(htype[0])));
        if (c == 'L') {
            hand_id_left_ = id;
            LOG_INFO("hand_id=" << static_cast<int>(id) << " type=Left");
        } else if (c == 'R') {
            hand_id_right_ = id;
            LOG_INFO("hand_id=" << static_cast<int>(id) << " type=Right");
        } else {
            LOG_WARN("hand_id=" << static_cast<int>(id) << " unrecognized type='" << htype << "'");
        }
    }

    // M7 / ADR-040: fail-closed when caller required both hands but only one
    // (or neither) was discovered. Pre-M7 callers (--hand left|right, --actions)
    // pass require_both=false and keep their tolerant behavior.
    if (require_both && !(hand_id_left_ && hand_id_right_)) {
        std::string missing;
        if (!hand_id_left_)  missing += "Left ";
        if (!hand_id_right_) missing += "Right";
        throw std::runtime_error(
            "open(require_both=true): missing hand(s) on bus: " + missing);
    }
}

void XHandDriver::send_one(uint8_t hand_id, const std::array<double, 12>& rad) {
    HandCommand_t cmd;
    std::memset(&cmd, 0, sizeof(cmd));
    for (int i = 0; i < 12; ++i) {
        cmd.finger_command[i].id       = static_cast<uint16_t>(i);
        cmd.finger_command[i].kp       = static_cast<int16_t>(pid_.kp);
        cmd.finger_command[i].ki       = static_cast<int16_t>(pid_.ki);
        cmd.finger_command[i].kd       = static_cast<int16_t>(pid_.kd);
        cmd.finger_command[i].position = static_cast<float>(rad[i]);
        cmd.finger_command[i].tor_max  = static_cast<int16_t>(pid_.tor_max);
        cmd.finger_command[i].mode     = static_cast<uint16_t>(pid_.mode);
    }
    auto err = ctl_.send_command(hand_id, cmd);
    if (!err) {
        // ADR-017: log not crash on transient send errors (e.g., M5a-observed CRC).
        LOG_WARN("send_command(hand_id=" << static_cast<int>(hand_id) << "): "
                 << err.error_code << " " << err.error_message);
    }
}

void XHandDriver::send_left(const std::array<double, 12>& rad) {
    if (!hand_id_left_) {
        throw std::runtime_error("send_left: left hand not discovered on bus");
    }
    send_one(*hand_id_left_, rad);
}

void XHandDriver::send_right(const std::array<double, 12>& rad) {
    if (!hand_id_right_) {
        throw std::runtime_error("send_right: right hand not discovered on bus");
    }
    send_one(*hand_id_right_, rad);
}

void XHandDriver::shutdown() {
    if (!opened_) return;
    HandCommand_t cmd;
    std::memset(&cmd, 0, sizeof(cmd));
    for (int i = 0; i < 12; ++i) {
        cmd.finger_command[i].id   = static_cast<uint16_t>(i);
        cmd.finger_command[i].mode = 0;   // passive (data_type.hpp:123 PASSIVE_MODE)
    }
    if (hand_id_left_)  ctl_.send_command(*hand_id_left_,  cmd);
    if (hand_id_right_) ctl_.send_command(*hand_id_right_, cmd);
    LOG_INFO("Shutdown: mode=0 (passive)");

    ctl_.close_device();
    opened_ = false;
    LOG_INFO("Device closed");
}
