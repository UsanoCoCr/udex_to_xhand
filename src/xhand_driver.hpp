#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "xhand_control.hpp"

struct XHandPID {
    int kp{100};
    int ki{0};
    int kd{0};
    int tor_max{300};
    int mode{3};   // 0=passive, 3=position, 5=force (FingerMode enum, data_type.hpp:122)
};

class XHandDriver {
 public:
    XHandDriver(const std::string& port, int baud, const XHandPID& pid);
    ~XHandDriver();

    XHandDriver(const XHandDriver&) = delete;
    XHandDriver& operator=(const XHandDriver&) = delete;

    // Opens serial, enumerates hands, identifies L/R via get_hand_type.
    // Throws std::runtime_error on serial-open failure or empty hand list.
    // Per plan §3.3 deviation (user-approved 2026-05-18): does NOT read XHand
    // calibration state — XHand SDK has no hand-level CalibrationStatus field;
    // UDCAP-side calib is enforced in UdcapReceiver::try_recv.
    void open();

    bool has_left()  const { return hand_id_left_.has_value(); }
    bool has_right() const { return hand_id_right_.has_value(); }

    // Sends 12 joint positions (radians) to one hand.
    // Throws std::runtime_error if that hand was not discovered (matches Python M3).
    void send_left (const std::array<double, 12>& rad);
    void send_right(const std::array<double, 12>& rad);

    // Sends mode=0 (passive) to every discovered hand, then close_device().
    // mode=0 is NOT a full power-off — see ADR-018.
    void shutdown();

 private:
    void send_one(uint8_t hand_id, const std::array<double, 12>& rad);

    std::string port_;
    int baud_{0};
    XHandPID pid_;
    xhand_control::XHandControl ctl_;
    std::optional<uint8_t> hand_id_left_;
    std::optional<uint8_t> hand_id_right_;
    bool opened_{false};
};
