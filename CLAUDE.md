# CLAUDE.md

## What is this

UDCAP gloves → XHand dexterous hand real-time teleoperation.
Windows PC runs UDCAP (HandDriver), sends hand joint data as UDP/JSON to a Linux host (Unitree G1 PC2, aarch64),
which maps 24 UDCAP DOF → 12 XHand DOF per hand and sends position commands via RS485.
Dual-hand (left + right). Internal tool. Acceptance = pick up a cup.
Full spec: SPEC.md. Roadmap: @docs/plans/00-roadmap.md. Decisions: @docs/decisions/.

## Architecture

Single C++17 binary `udex_to_xhand` on G1 PC2 (aarch64 Linux), ~100Hz real-time control loop.

```
Windows (UDCAP HandDriver)
  → UDP JSON, port 9000, 60-120Hz
G1 PC2, aarch64 Linux (this project, C++17 + cmake)
  → src/udcap_receiver.{hpp,cpp}  : non-blocking UDP recv, JSON parse (nlohmann_json)
  → src/joint_mapper.{hpp,cpp}    : 24→12 mapping per hand, config-driven (yaml-cpp)
  → src/xhand_driver.{hpp,cpp}    : wraps xhand_control::XHandControl (RS485, vendor C++ SDK)
  → src/safety.{hpp,cpp}          : watchdog, clamp, graceful shutdown
  → src/main.cpp                  : control loop ~100Hz, CLI entry point
  → config.yaml                   : all tunable params (mapping, PID, ports)
```

**Vendor SDK**: `xhand_control_sdk/` ships aarch64 `libxhand_control.so` + C++ headers; linked via `find_package(xhand_control HINTS xhand_control_sdk/share)`. No Python wheel route on aarch64.

**Migration status**: M0–M4 prototype was Python (`main.py`, `udcap_receiver.py`, `joint_mapper.py`, `xhand_driver.py`, `safety.py` — moved to `legacy_python/` in M5b). M5b lands the C++ runtime stack as the authoritative implementation: single binary `udex_to_xhand` built from `src/*.{hpp,cpp}` + top-level `CMakeLists.txt`. Only `legacy_python/joint_mapper.py` retains a live role — it is the snapshot oracle for `tests/test_mapper_snapshot.cpp` (regenerated via `scripts/dump_mapper_baseline.py`). The rest have zero runtime callers; full removal is a post-M5c candidate. See roadmap §M5 and `legacy_python/README.md`.

## Key data flow

- UDCAP: `l0`-`l23` (left) / `r0`-`r23` (right), degrees, negative = flexion
- XHand: 12 joints per hand, radians, positive = flexion
- Mapping sign flip + range rescale defined per-joint in config.yaml
- UDCAP param→joint mapping verified in M2 (SPEC.md §3.1; ADRs 009-013); canonical values in config.yaml

## XHand joints (authoritative: `xhand_control_sdk/include/data_type.hpp` + `xhand_control.hpp`; cross-reference `xhand_control_ros2.hpp:68-72`)

```
0: thumb_bend    3: index_bend    6: mid_1     8: ring_1    10: pinky_1
1: thumb_rota1   4: index_1       7: mid_2     9: ring_2    11: pinky_2
2: thumb_rota2   5: index_2
```

## Commands

```bash
# One-time: install system deps on PC2
sudo apt update && sudo apt install -y \
    cmake g++ libcurl4-openssl-dev libssl-dev \
    nlohmann-json3-dev libyaml-cpp-dev

# M5a — verify vendor SDK builds on PC2
cd xhand_control_sdk/tests && mkdir -p build && cd build
cmake .. && make
./test_serial            # interactive; vendor default port /dev/ttyUSB0, override to /dev/ttyACM0 (ADR-014)

# M5b — build this project
mkdir -p build && cd build && cmake .. && make
ls ./udex_to_xhand       # expected: ELF aarch64

# Run teleoperation
./udex_to_xhand --config ../config.yaml

# Sub-tests
./udex_to_xhand --config ../config.yaml --receiver-only --duration 10
./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok
```

## Code conventions

- C++17, cmake ≥ 3.10
- Deps: vendor `xhand_control` (in `xhand_control_sdk/`), `nlohmann_json`, `yaml-cpp`, `libcurl`, `libssl`
- No ROS2 dependency — use `xhand_control_sdk/` directly (not `xhand_control_ros2/`)
- All mapping params in `config.yaml`, never hardcoded
- Units: degrees at UDCAP boundary, radians at XHand boundary, convert once in `joint_mapper`
- Position commands in radians: `degrees * M_PI / 180.0f`
- XHand `send_command` takes `HandCommand_t` with 12 `FingerCommand_t` (id, kp, ki, kd, position, tor_max, mode) — struct layout in `xhand_control_sdk/include/data_type.hpp`
- Default PID: kp=100, ki=0, kd=0, tor_max=300, mode=3
- Serial: `/dev/ttyACM0` (CDC-ACM per ADR-014), baudrate 3000000

## Workflow

1. Read SPEC.md before starting any work
2. Plan before code — discuss approach in conversation first
3. One module at a time, test each before integrating
4. Phase order: UDP receiver → XHand driver → mapper → safety → main loop
5. Joint mapping values are experimental — expect iteration

## Constraints — do NOT

- Do NOT add wrist control (UDCAP JSON SDK does not expose wrist data; l21–l23 are MCP Roll / gesture flag — see SPEC.md §6)
- Do NOT read XHand sensors / implement force feedback
- Do NOT add GUI or visualization
- Do NOT add motion recording/playback
- Do NOT use ROS2 (`xhand_control_ros2/` exists in repo as reference only)
- Do NOT add arm control
- Do NOT hardcode joint mapping — everything through config.yaml
- Do NOT add a pybind11 / ctypes / cffi layer over the C++ SDK — runtime stack is pure C++ (rationale: roadmap §M5 revision 2; memory `feedback_no_unnecessary_ffi.md`)
- Do NOT reintroduce Python into the realtime control loop — Python is for offline scripts / experiments only
- Do NOT send commands to XHand before (a) `list_hands_id()` reports the expected hand(s) AND (b) the most recent UDCAP frame has `L_/R_CalibrationStatus == 3`. The CalibrationStatus check is a **UDCAP-side** field (see SPEC §5); the XHand SDK does not expose a hand-level calibration status (the `CalibrationStatus` enum in `data_type.hpp` is a per-finger calibration-error code, not a state). Enforce (a) in `XHandDriver::open()`, (b) in `UdcapReceiver::try_recv()`.
- Do NOT send commands without clamping to joint limits first

## Safety (non-negotiable)

- Watchdog: hold last position if no UDP for >200ms
- Clamp all joint positions to per-joint [min, max] before send_command
- Ctrl+C or SIGTERM → set mode=0 → close_device (mode=0 is NOT full power-off; ADR-018)
- Skip frame on JSON parse error — never send stale/corrupt data
- Startup: open device → verify IDs → set mode=3 → wait for first valid packet
