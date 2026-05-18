# SPEC: UDCAP Gloves → XHand Dexterous Hand Real-time Teleoperation

## 1. Project Overview

**Goal**: Use UDEX Real UDCAP gloves to teleoperate two Galaxea (星动纪元) XHand dexterous hands in real-time, enabling grasping and manipulation tasks.

**Project Type**: Internal tool — needs to be stable and reliable for daily team use.

**Acceptance Criteria**: Operator wearing UDCAP gloves can control dual XHands to **pick up objects**.

---

## 2. System Architecture

```
┌──────────────────┐       UDP/JSON        ┌──────────────────────────────────────────┐
│  Windows PC      │      port 9000        │  Linux host (Unitree G1 PC2, aarch64)    │
│                  │ ───────────────────>   │  Single C++17 binary `udex_to_xhand`     │
│  UDCAP Software  │   60/90/120 Hz        │  ┌─────────────┐    ┌─────────────────┐  │
│  (HandDriver)    │   L+R hand data       │  │ UDP Receiver │───>│ Joint Mapper    │  │
│                  │                       │  │  + JSON parse│    │ (24→12 per hand)│  │
└──────────────────┘                       │  └─────────────┘    └────────┬────────┘  │
                                           │                              │           │
                                           │              ┌───────────────┴────────┐  │
                                           │              │      XHand Driver      │  │
                                           │              │ (xhand_control C++ SDK,│  │
                                           │              │     RS485, aarch64)    │  │
                                           │              └───┬───────────────┬────┘  │
                                           │                  │               │       │
                                           │            ┌─────┴──┐     ┌─────┴──┐    │
                                           │            │ XHand  │     │ XHand  │    │
                                           │            │ Left   │     │ Right  │    │
                                           │            └────────┘     └────────┘    │
                                           └──────────────────────────────────────────┘
```

### Communication Decisions

| Link | Protocol | Rationale |
|------|----------|-----------|
| UDCAP → Linux | UDP JSON, port 9000 | Already working (test.py). Non-blocking, stateless. |
| Linux → XHand | **RS485** (3Mbps, USB-to-serial) | Simpler setup than EtherCAT (no root/网卡 config). Bandwidth sufficient for dual-hand at 100Hz (~5Kbits/cycle vs 3Mbps available). Upgrade path to EtherCAT exists if needed. |

### Control Loop (target 100Hz)

```
while running:
    1. recv UDP packet (non-blocking, take latest)
    2. parse JSON → extract l0-l23, r0-r23
    3. joint_mapping(udcap_24) → xhand_12  (per hand)
    4. unit conversion: degrees → radians
    5. safety clamp: enforce per-joint limits
    6. send_command() to left XHand
    7. send_command() to right XHand
    8. watchdog: if no UDP packet for >200ms, hold last position
```

---

## 3. Data Formats

### 3.1 UDCAP Output (per hand, 24 parameters)

Source: UDP JSON, field `"Parameter"` array.

Mapping verified per UDCAP official JSON SDK documentation (M2; ADRs 009/010/011/012/013).
Per-finger ordering is **distal → proximal** (DIP first, then PIP, then MCP Pitch / Yaw).
MCP Roll indices are non-contiguous: located at l20–l22 for thumb / index / pinky only.

| Index | Joint        | Axis  | Notes                                          |
|-------|--------------|-------|------------------------------------------------|
| l0    | Thumb DIP    | Pitch | Flexion                                        |
| l1    | Thumb PIP    | Pitch | Flexion                                        |
| l2    | Thumb MCP    | Pitch | Flexion                                        |
| l3    | Thumb MCP    | Yaw   | Abduction                                      |
| l4    | Index DIP    | Pitch | Flexion                                        |
| l5    | Index PIP    | Pitch | Flexion                                        |
| l6    | Index MCP    | Pitch | Flexion                                        |
| l7    | Index MCP    | Yaw   | Abduction (lateral spread)                     |
| l8    | Middle DIP   | Pitch | Flexion                                        |
| l9    | Middle PIP   | Pitch | Flexion                                        |
| l10   | Middle MCP   | Pitch | Flexion                                        |
| l11   | Middle MCP   | Yaw   | **DISCARDED** — no XHand DOF                   |
| l12   | Ring DIP     | Pitch | Flexion                                        |
| l13   | Ring PIP     | Pitch | Flexion                                        |
| l14   | Ring MCP     | Pitch | Flexion                                        |
| l15   | Ring MCP     | Yaw   | **DISCARDED** — no XHand DOF                   |
| l16   | Pinky DIP    | Pitch | Flexion                                        |
| l17   | Pinky PIP    | Pitch | Flexion                                        |
| l18   | Pinky MCP    | Pitch | Flexion                                        |
| l19   | Pinky MCP    | Yaw   | **DISCARDED** — no XHand DOF                   |
| l20   | Thumb MCP    | Roll  | Opposition (non-contiguous source, ADR-013)    |
| l21   | Index MCP    | Roll  | **DISCARDED** — no XHand DOF                   |
| l22   | Pinky MCP    | Roll  | **DISCARDED** — no XHand DOF                   |
| l23   | Gesture flag | —     | Always -1; **not a joint** (ADR-012)           |

> **Note**: The UDCAP JSON SDK does **not** expose wrist parameters at all (ADR-010).
> The pre-M2 SPEC assumed l21–l23 were wrist data; they are MCP Roll / gesture flag.
> Wrist control remains out of scope (§6), but for a different reason than originally stated.

Right-hand parameters (`r0`-`r23`) follow the same structure. The starting hypothesis is
that sign conventions mirror the left hand (`config.yaml` currently uses identical signs
for both); right-hand verification is M7 scope.

### 3.2 XHand Input (per hand, 12 DOF)

Source: vendor C++ SDK in `xhand_control_sdk/` (aarch64 `libxhand_control.so` + headers). `HandCommand_t` with 12 `FingerCommand_t` — struct layout defined in `xhand_control_sdk/include/data_type.hpp`.

| Joint ID | Name | Physical Meaning | Notes |
|----------|------|------------------|-------|
| 0 | thumb_bend_joint | Thumb flexion (IP/MCP) | |
| 1 | thumb_rota_joint1 | Thumb rotation axis 1 | Opposition-related |
| 2 | thumb_rota_joint2 | Thumb rotation axis 2 | Opposition-related |
| 3 | index_bend_joint | Index finger lateral spread | Abduction |
| 4 | index_joint1 | Index proximal flexion | MCP |
| 5 | index_joint2 | Index distal flexion | PIP+DIP coupled |
| 6 | mid_joint1 | Middle proximal flexion | MCP |
| 7 | mid_joint2 | Middle distal flexion | PIP+DIP coupled |
| 8 | ring_joint1 | Ring proximal flexion | MCP |
| 9 | ring_joint2 | Ring distal flexion | PIP+DIP coupled |
| 10 | pinky_joint1 | Pinky proximal flexion | MCP |
| 11 | pinky_joint2 | Pinky distal flexion | PIP+DIP coupled |

**Units**: radians (SDK converts internally: `position_degrees * pi / 180`)

**Overall joint range**: -90° to 110° (per `HandAnglePose_t` comment in SDK), but actual limits vary per joint.

**Control parameters per joint**:
- `kp`: Proportional gain (default 100)
- `ki`: Integral gain (default 0)
- `kd`: Derivative gain (default 0)
- `tor_max`: Max torque (default 300 mA)
- `mode`: 0=passive, 3=position control, 5=force control
- `position`: Target position in radians

---

## 4. Joint Mapping: UDCAP 24 → XHand 12

This is the core algorithm. The mapping must reduce 24 DOF to 12 DOF per hand.

### 4.1 Proposed Initial Mapping

**Thumb** (UDCAP 5 DOF → XHand 3 DOF):

| XHand Joint | ← UDCAP Source | Mapping Strategy |
|-------------|----------------|------------------|
| thumb_bend_joint (J0) | l0 (DIP) + l1 (PIP) + l2 (MCP Pitch) | Weighted sum 0.3 / 0.3 / 0.4 (M4-verified, left hand) |
| thumb_rota_joint1 (J1) | l3 (Thumb MCP Yaw) | Direct map |
| thumb_rota_joint2 (J2) | l20 (Thumb MCP Roll) | Direct map; **non-contiguous source** (ADR-013) |

**Index** (UDCAP 4 DOF → XHand 3 DOF):

| XHand Joint | ← UDCAP Source | Mapping Strategy |
|-------------|----------------|------------------|
| index_bend_joint (J3) | l7 (Index MCP Yaw) | Direct map — lateral spread |
| index_joint1 (J4) | l6 (Index MCP Pitch) | Direct map — proximal flexion |
| index_joint2 (J5) | l5 (PIP) + l4 (DIP) | Weighted sum 0.6 / 0.4 — coupled distal flexion |

**Middle / Ring / Pinky** (UDCAP 4 DOF → XHand 2 DOF each):

| XHand Joint | ← UDCAP Source | Mapping Strategy |
|-------------|----------------|------------------|
| xxx_joint1 | MCP Pitch (l10 / l14 / l18) | Direct map — proximal flexion |
| xxx_joint2 | PIP + DIP (l9+l8 / l13+l12 / l17+l16) | Weighted sum 0.6 / 0.4 — coupled distal flexion |

> **Discarded sources** (no corresponding XHand DOF, ADR-012):
> - MCP Yaw for middle / ring / pinky: l11, l15, l19
> - MCP Roll for index, pinky: l21, l22
> - Gesture flag (l23) — not a joint

### 4.2 Range Normalization

UDCAP outputs degrees with negative = flexion. XHand expects radians with positive = flexion (based on preset actions where fist = 110°).

General formula (language-neutral; implemented in C++ inside `joint_mapper`):
```
xhand_rad = clamp(-udcap_deg * scale_factor, joint_min_deg, joint_max_deg) * (M_PI / 180.0)
```

The sign inversion and scale factor are per-joint and must be determined experimentally.
Clamping happens in the degree domain before unit conversion (ADR-020 / ADR-021).

### 4.3 Right Hand Mirroring

Per UDCAP docs, the coordinate system is left-hand based. For right hand (`r0`-`r23`):
- Some axes may need sign negation
- XHand distinguishes left/right hands via `get_hand_type()`
- The mapping logic should be parameterized, not hardcoded

---

## 5. Safety Mechanisms

| Mechanism | Implementation |
|-----------|----------------|
| **Watchdog timeout** | If no valid UDP packet received for >200ms, hold last commanded position. Log warning. |
| **Joint clamping** | All position commands clamped to per-joint [min, max] before sending. |
| **Startup sequence** | 1) Open device, 2) Verify hand IDs and types, 3) Set mode=3 (position control), 4) Wait for first valid UDP packet before moving. |
| **Graceful shutdown** | On Ctrl+C **or SIGTERM** (ADR-023): set mode=0, close device. Note: mode=0 does not fully de-energize fingers (ADR-018) — physical power gating is out of scope. |
| **Invalid data guard** | If JSON parse fails or parameter count wrong, skip frame (don't send stale data). |
| **CalibrationStatus check** | Only forward data when UDCAP CalibrationStatus == 3 (calibrated). |

---

## 6. Scope

### In Scope
- Real-time teleoperation of dual XHands via UDCAP gloves
- Joint mapping from UDCAP 24-DOF to XHand 12-DOF (per hand)
- Safety mechanisms (watchdog, clamping, graceful shutdown)
- Configuration file for mapping parameters (tunable without code changes)
- CLI-based operation (no GUI needed)
- Logging for debugging (latency, dropped frames, joint values)

### Explicitly Out of Scope
- **Wrist control** — UDCAP JSON SDK does not expose wrist data at all (ADR-010); l21–l23 are MCP Roll / gesture flag, not wrist
- **Force feedback** — XHand fingertip sensor data is not transmitted back to the glove
- **3D visualization / GUI** — Pure command-line tool
- **Motion recording/playback** — No trajectory logging for later replay
- **ROS2 integration** — Use the vendor C++ SDK in `xhand_control_sdk/` directly, not the ROS2 wrapper (reduces dependencies)
- **Python in the realtime path** — M5 rewrites the runtime stack from Python to C++; no pybind11 / ctypes / IPC shim back to Python in the 100Hz loop (rationale: roadmap §M5 revision 2)
- **Arm control** — Only hand joints, no robotic arm integration
- **Multi-user** — Single operator, single pair of gloves

---

## 7. Tech Stack

| Component | Technology |
|-----------|-----------|
| Language (runtime) | **C++17** (cmake ≥ 3.10). Single binary `udex_to_xhand`. |
| Vendor SDK | `xhand_control_sdk/` — aarch64 `libxhand_control.so` + C++ headers, linked via `find_package(xhand_control HINTS xhand_control_sdk/share)`. No Python wheel on aarch64. |
| Communication | POSIX UDP sockets (stdlib) for UDCAP ingress + RS485 for XHand (via SDK), `/dev/ttyACM0` @ 3 Mbps |
| JSON parsing | nlohmann_json (`libnlohmann-json3-dev`) |
| Config parsing | YAML via yaml-cpp (`libyaml-cpp-dev`) |
| Other system libs | libcurl, libssl (transitive SDK deps) |
| OS | Linux. Deployment target: Unitree G1 PC2 (aarch64). Optional development on Ubuntu 22/24 x86_64 (algorithm checks only — runtime parity needs PC2). |
| Auxiliary (non-runtime) | Python 3 scripts under `scripts/` and `experiments/` retained for offline UDCAP parameter inspection / one-off analyses; not loaded by the realtime binary. |

---

## 8. File Structure (Target — post-M5)

```
udex_to_xhand/
├── CMakeLists.txt                # Top-level cmake; finds xhand_control, yaml-cpp, nlohmann_json
├── src/
│   ├── main.cpp                  # Entry point, CLI args, 100Hz control loop
│   ├── udcap_receiver.{hpp,cpp}  # Non-blocking UDP recv + JSON parse
│   ├── joint_mapper.{hpp,cpp}    # 24→12 mapping logic; loads config.yaml
│   ├── xhand_driver.{hpp,cpp}    # Wraps xhand_control::XHandControl (open/list/send/close)
│   └── safety.{hpp,cpp}          # Watchdog, per-joint clamp, signal handlers
├── build/                        # cmake out-of-source build dir (gitignored)
│   └── udex_to_xhand             # Built ELF aarch64 binary
├── config.yaml                   # Mapping parameters, PID gains, IP/port
├── example.json                  # Reference UDCAP packet for offline tests
├── SPEC.md                       # This file
├── CLAUDE.md                     # Agent guidance
├── docs/
│   ├── plans/00-roadmap.md       # Milestones
│   └── decisions/                # ADRs 001-023…
├── xhand_control_sdk/            # Vendor C++ SDK (aarch64): include/, lib/, share/, tests/
├── xhand_control_sdk_py/         # Legacy Python wheel (x86_64 only; not used on PC2)
├── xhand_control_ros2/           # XHand ROS2 SDK (reference only)
├── scripts/                      # Offline helpers (Python; UDCAP param identify, log analysis)
├── experiments/                  # Ad-hoc Python notes (not part of runtime)
├── legacy_python/                # M0-M4 Python prototype (moved here in M5b)
│   ├── main.py                   # reference only; zero runtime callers post-M5b
│   ├── udcap_receiver.py         # reference only
│   ├── joint_mapper.py           # snapshot oracle for tests/test_mapper_snapshot.cpp (still live)
│   ├── xhand_driver.py           # reference only
│   ├── safety.py                 # reference only
│   ├── test_udcap_connection.py  # M0-era one-shot UDP sniffer
│   └── README.md                 # rationale for keeping; removal candidate post-M5c
├── udcap关节文档/                 # UDCAP joint documentation
├── URDF/                         # XHand URDF models (reference)
└── xhand中文文档/                 # XHand Chinese documentation
```

---

## 9. Verification Plan

### Phase 1: Bring-up (单手, 开环) — completed on external dev PC (M0-M4)
1. **UDP data validation**: Confirm UDCAP data arrives on Linux, parse all 24 params correctly, verify l0-l23 mapping against hand movements (manually flex each finger while logging values).
2. **XHand SDK smoke test**: Connect one XHand via RS485, run preset actions (fist/palm/V/OK), confirm SDK works.
3. **Single-finger mapping**: Map ONE finger (index) from UDCAP → XHand, verify directional correctness and range.
4. **Single-hand full mapping**: Map all 12 joints for one hand, tune parameters.

### Phase 1.5: Port to G1 PC2 — C++ Rewrite (M5; ADR-023 + roadmap revision 2)
4a. **Vendor C++ SDK on PC2** (M5a): install build deps (`cmake g++ libcurl4-openssl-dev libssl-dev nlohmann-json3-dev libyaml-cpp-dev`), build `xhand_control_sdk/tests/`, confirm hardware reachable via `./test_serial`.
4b. **Project C++ port** (M5b): rewrite `udcap_receiver`, `joint_mapper`, `xhand_driver`, `safety`, `main` from Python into a single C++17 binary `udex_to_xhand`. `config.yaml` and the M2-verified mapping data carry over unchanged (parsed via yaml-cpp).
4c. **Serial + permissions**: verify `/dev/ttyACM*` enumeration + dialout group access (ADR-014).
4d. **Network path**: configure Windows UDCAP → G1 PC2 UDP route (static IP / firewall / wired link as needed).
4e. **Re-verify Phase 1 items 1–4 on PC2** (M5c): latency should match or improve on dev-PC Python baseline.

### Phase 2: Integration (双手)
5. **Dual XHand connection**: Connect both hands on RS485, verify both hand IDs are discoverable and independently controllable.
6. **Dual-hand teleoperation**: Map both hands simultaneously, test basic gestures.
7. **Left/right mirroring**: Verify sign conventions for right hand.

### Phase 3: Robustness
8. **Safety mechanisms**:
   - **Watchdog**: Kill UDCAP → verify XHand holds last position.
   - **Graceful shutdown**: Ctrl+C and `kill -TERM <pid>` both trigger mode=0 + close_device.
   - **Joint clamp**: Set a tight clamp in config.yaml (e.g. index_joint1 [0°, 30°]); verify XHand respects the limit when glove flexes past it.
9. **Latency measurement**: Log end-to-end latency (UDCAP timestamp → XHand command sent). Target: <50ms.
10. **Stress test**: 30-minute continuous operation, monitor for drift, packet loss, or SDK errors.
11. **Grasping test**: Attempt to pick up objects of varying sizes (pen, cup, ball).

### Acceptance Test
- **Setup**: XHands mounted on G1 arm end-effectors (ADR-023, not handheld).
- **Pass**: Operator wearing UDCAP gloves can pick up a cup with dual XHands; ≥3 of 5 attempts succeed.

---

## 10. Risk Register

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| 1 | **Joint mapping inaccuracy** — 24→12 mapping produces unnatural motions, especially for thumb | HIGH | HIGH | Make mapping fully configurable (config.yaml). Start with simple linear mapping, iterate with operator feedback. Budget significant tuning time. |
| 2 | ~~UDCAP parameter ordering unknown~~ **RESOLVED** (M2; ADRs 009/010/011/012/013) — distal→proximal per finger; MCP Roll non-contiguous at l20–l22; l23 is gesture flag, not a joint | — | — | Retired |
| 3 | **RS485 bus contention with dual hands** — Sending commands to two hands on shared bus may introduce latency or collisions | MEDIUM | MEDIUM | Monitor cycle time. If >10ms per cycle, switch to EtherCAT or use two separate USB-to-serial adapters. PC2 USB port budget is also a precondition (see Risk 8). |
| 4 | **UDP packet loss / jitter** — WiFi or congested network causes dropped frames or variable latency | MEDIUM | MEDIUM | Use wired Ethernet between Windows and Linux. Implement smoothing filter to interpolate over missing frames. |
| 5 | **Sign convention mismatch** — UDCAP negative=flexion vs XHand positive=flexion may vary per axis and per hand | HIGH | MEDIUM | Configurable sign per joint in config.yaml. Left hand verified in M4; right hand verification in M7. |
| 6 | **XHand PID tuning** — Default kp=100 may cause oscillation or sluggish response at teleoperation speeds | MEDIUM | LOW | Expose PID params in config. Start conservative (low kp), increase gradually. |
| 7 | ~~**aarch64 `xhand_controller` wheel availability**~~ **RESOLVED via path change** (roadmap revision 2, 2026-05-16) — vendor delivers `xhand_control_sdk/` (aarch64 C++ SDK + headers + .so), not a Python wheel. Project pivots to a pure C++ runtime; no Python wheel needed. | — | — | Retired. New residual: C++ build correctness on PC2 — covered by M5a. |
| 8 | **PC2 resource / lifecycle constraints** — onboard PC2 has unknown CPU/RAM/USB power budget; may be torn down by robot lifecycle (SIGTERM) | MEDIUM | MEDIUM | M5 profiles resource usage; M6 adds SIGTERM handler equivalent to Ctrl+C (C++ `std::signal` + RAII shutdown). |
| 9 | **Windows → G1 PC2 network path** — new topology vs dev PC; requires static IP / firewall / wired link configuration | MEDIUM | LOW | M5 explicit task: configure and verify UDP path before re-running Phase 1 items on PC2. |
| 10 | **C++ port regressions vs Python M4 baseline** — rewriting M1/M3/M4 in C++ can re-introduce sign/clamp/ordering bugs already fixed in ADRs 020-022 | MEDIUM | MEDIUM | M5b reuses verified `config.yaml` data unchanged; M5c re-runs M3 (fist/palm/V/OK) and M4 (single-hand teleop) acceptance scripts before declaring M5 done. |

---

## 11. Configuration Schema (config.yaml)

```yaml
udcap:
  host: "0.0.0.0"
  port: 9000
  timeout_ms: 200          # Watchdog timeout

xhand:
  protocol: "RS485"        # or "EtherCAT"
  serial_port: "/dev/ttyACM0"   # CDC-ACM device (ADR-014)
  baud_rate: 3000000
  left_hand_id: 0
  right_hand_id: 1
  control_mode: 3          # 0=passive, 3=position, 5=force
  default_kp: 100
  default_ki: 0
  default_kd: 0
  default_tor_max: 300
  update_rate_hz: 100

# Per-joint mapping (M2-verified indices; canonical values live in config.yaml):
#   sources: UDCAP parameter indices to combine
#   weights: per-source weight (sums to ~1.0)
#   sign:    +1 / -1 (UDCAP negative=flexion vs XHand positive=flexion)
#   offset:  degrees, added after weighted sum
#   clamp:   [min_deg, max_deg] enforced before deg→rad conversion
mapping:
  left:
    thumb_bend:    { sources: [0, 1, 2],  weights: [0.3, 0.3, 0.4], sign: -1, offset: 0, clamp: [-10, 110] }
    thumb_rota1:   { sources: [3],        weights: [1.0],            sign: -1, offset: 0, clamp: [-10, 110] }
    thumb_rota2:   { sources: [20],       weights: [1.0],            sign:  1, offset: 0, clamp: [0, 50] }
    index_bend:    { sources: [7],        weights: [1.0],            sign: -1, offset: 0, clamp: [-10, 30] }
    index_joint1:  { sources: [6],        weights: [1.0],            sign: -1, offset: 0, clamp: [0, 110] }
    index_joint2:  { sources: [5, 4],     weights: [0.6, 0.4],       sign: -1, offset: 0, clamp: [0, 110] }
    mid_joint1:    { sources: [10],       weights: [1.0],            sign: -1, offset: 0, clamp: [0, 110] }
    mid_joint2:    { sources: [9, 8],     weights: [0.6, 0.4],       sign: -1, offset: 0, clamp: [0, 110] }
    ring_joint1:   { sources: [14],       weights: [1.0],            sign: -1, offset: 0, clamp: [0, 110] }
    ring_joint2:   { sources: [13, 12],   weights: [0.6, 0.4],       sign: -1, offset: 0, clamp: [0, 110] }
    pinky_joint1:  { sources: [18],       weights: [1.0],            sign: -1, offset: 0, clamp: [0, 110] }
    pinky_joint2:  { sources: [17, 16],   weights: [0.6, 0.4],       sign: -1, offset: 0, clamp: [0, 110] }
  right:
    # Same structure; signs currently mirror left as a starting hypothesis.
    # Verification is M7 scope (see roadmap).
```

---

## 12. Open Questions (TBD during Phase 1)

1. ~~**UDCAP l0-l23 exact mapping verification**~~ **RESOLVED** (M2; ADRs 009/010/011/012/013). §3.1 now reflects the verified mapping.
2. **XHand per-joint angle limits** — The overall range is -90°~110° but individual joints likely have tighter limits. Need to query or measure.
3. **Right hand sign convention** — UDCAP docs say "right hand may need sign negation". Need to test which axes. M7 scope.
4. **RS485 dual-hand addressing** — Can two XHands share one serial port? Or do we need two USB adapters? Also constrained by PC2 USB port count (M7).
5. **Optimal PID parameters** — kp=100 may not be appropriate for teleoperation. Need to tune for responsiveness vs smoothness.
6. **Smoothing filter** — May need a low-pass filter or interpolation to handle UDP jitter. Design TBD based on Phase 1 measurements.
7. ~~**aarch64 `xhand_controller` wheel availability**~~ **RESOLVED** — vendor delivered `xhand_control_sdk/` C++ SDK with aarch64 `.so` + headers (2026-05-16). Project pivots to pure C++ runtime; no Python wheel needed.
8. **G1 PC2 ↔ Windows UDCAP network configuration** — static IP, routing, firewall, wired vs wireless link (M5).
9. **G1 PC2 resource budget** — sustained 100Hz operation CPU/RAM, USB power for one or two XHands (M5/M7).
10. ~~**C++ ↔ legacy Python equivalence**~~ **RESOLVED (M5b)** — `tests/test_mapper_snapshot.cpp` asserts ‖C++ - Python‖∞ < 1e-6 rad against `tests/fixtures/mapper_baseline.json` (generated by `scripts/dump_mapper_baseline.py` from `joint_mapper.py` on `example.json` + `config.yaml`; SHA-256 of input files embedded in the fixture to detect silent drift).

---

## 13. Reference Materials

| Resource | Location |
|----------|----------|
| **XHand C++ SDK (aarch64 — runtime target)** | `xhand_control_sdk/` (`include/`, `lib/libxhand_control.so`, `share/xhand_control/cmake/`, `tests/`) |
| XHand C++ API surface | `xhand_control_sdk/include/xhand_control.hpp` |
| XHand C++ data types (`HandCommand_t`, `FingerCommand_t`, `HandState_t`, …) | `xhand_control_sdk/include/data_type.hpp` |
| XHand C++ usage example | `xhand_control_sdk/tests/src/serial_test.cpp` |
| XHand joint names (authoritative cross-reference) | `xhand_control_ros2/.../xhand_control_ros2.hpp:68-72` |
| XHand Python SDK (x86_64 only; legacy; not used on PC2) | `xhand_control_sdk_py/` |
| XHand ROS2 SDK (reference only — DO NOT link) | `xhand_control_ros2/` |
| XHand Chinese docs (PDF) | `xhand中文文档/` |
| XHand URDF models | `URDF/` |
| UDCAP joint documentation | `udcap关节文档/` |
| UDCAP hand model + bone data | `udcap关节文档/HandDriver Initial Hand Mode Joint Position.txt` |
| UDCAP online docs | https://udexreal.gitbook.io/udexreal-docs/docs-cn/ruan-jian/handdriver-shou-bu-mo-xing-guan-jie-he-shu-ju-shuo-ming |
| UDCAP example UDP data | `example.json` |
| UDP receiver prototype (Python, legacy) | `legacy_python/udcap_receiver.py` (and `legacy_python/test_udcap_connection.py` — M0-era one-shot sniffer) |
| Python prototype reference (M0-M4; pre-C++ rewrite) | `legacy_python/{main,udcap_receiver,joint_mapper,xhand_driver,safety}.py` |
