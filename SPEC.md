# SPEC: UDCAP Gloves → XHand Dexterous Hand Real-time Teleoperation

## 1. Project Overview

**Goal**: Use UDEX Real UDCAP gloves to teleoperate two Galaxea (星动纪元) XHand dexterous hands in real-time, enabling grasping and manipulation tasks.

**Project Type**: Internal tool — needs to be stable and reliable for daily team use.

**Acceptance Criteria**: Operator wearing UDCAP gloves can control dual XHands to **pick up objects**.

---

## 2. System Architecture

```
┌──────────────────┐       UDP/JSON        ┌──────────────────────────────────────────┐
│  Windows PC      │      port 9000        │  Linux PC (Ubuntu 22/24 x86_64)          │
│                  │ ───────────────────>   │                                          │
│  UDCAP Software  │   60/90/120 Hz        │  ┌─────────────┐    ┌─────────────────┐  │
│  (HandDriver)    │   L+R hand data       │  │ UDP Receiver │───>│ Joint Mapper    │  │
│                  │                       │  └─────────────┘    │ (24→12 per hand)│  │
└──────────────────┘                       │                     └────────┬────────┘  │
                                           │                              │           │
                                           │              ┌───────────────┴────────┐  │
                                           │              │    XHand Controller    │  │
                                           │              │  (Python SDK, RS485)   │  │
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

| Index | Joint | Axis | Range (deg) | Notes |
|-------|-------|------|-------------|-------|
| l0 | Thumb CM | Pitch | [-70, 10] | Flexion toward palm = negative |
| l1 | Thumb CM | Yaw | [-35, 20] | Abduction/adduction |
| l2 | Thumb CM | Roll | [0, 47] | Opposition |
| l3 | Thumb MP | Pitch | [-60, 0] | Flexion |
| l4 | Thumb IP | Pitch | [-60, 0] | Flexion |
| l5 | Index MP | Pitch | [-80, 0] | Flexion |
| l6 | Index MP | Yaw | [-25, 0] | Abduction (spread) |
| l7 | Index PIP | Pitch | [-100, 0] | Flexion |
| l8 | Index DIP | Pitch | [-80, 0] | Flexion |
| l9 | Middle MP | Pitch | [-80, 0] | Flexion |
| l10 | Middle MP | Yaw | [-4.5, 4.5] | Small spread range |
| l11 | Middle PIP | Pitch | [-100, 0] | Flexion |
| l12 | Middle DIP | Pitch | [-80, 0] | Flexion |
| l13 | Ring MP | Pitch | [-80, 0] | Flexion |
| l14 | Ring MP | Yaw | [0, 15] | Spread |
| l15 | Ring PIP | Pitch | [-100, 0] | Flexion |
| l16 | Ring DIP | Pitch | [-80, 0] | Flexion |
| l17 | Pinky MP | Pitch | [-80, 0] | Flexion |
| l18 | Pinky MP | Yaw | [0, 35] | Spread |
| l19 | Pinky PIP | Pitch | [-100, 0] | Flexion |
| l20 | Pinky DIP | Pitch | [-80, 0] | Flexion |
| l21 | Wrist | Pitch | — | **EXCLUDED** |
| l22 | Wrist | Yaw | — | **EXCLUDED** |
| l23 | Wrist | Roll | — | **EXCLUDED** |

> **IMPORTANT**: The l0-l23 mapping above is inferred from the joint angle range table in the UDCAP documentation. The exact parameter-to-joint mapping needs **experimental verification** during bring-up. This is the single highest-risk item in the project.

Right hand parameters (`r0`-`r23`) follow the same structure. Per UDCAP docs, right-hand values may need sign negation (left-hand coordinate system is the reference).

### 3.2 XHand Input (per hand, 12 DOF)

Source: `xhand_controller` Python SDK, `HandCommand_t` with 12 `FingerCommand_t`.

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
| thumb_bend_joint (J0) | l0 (Thumb CM Pitch) + l3 (MP Pitch) + l4 (IP Pitch) | Weighted sum or dominant axis — **needs tuning** |
| thumb_rota_joint1 (J1) | l1 (Thumb CM Yaw) | Direct map with range rescaling |
| thumb_rota_joint2 (J2) | l2 (Thumb CM Roll) | Direct map with range rescaling |

**Index** (UDCAP 4 DOF → XHand 3 DOF):

| XHand Joint | ← UDCAP Source | Mapping Strategy |
|-------------|----------------|------------------|
| index_bend_joint (J3) | l6 (Index MP Yaw) | Direct map — lateral spread |
| index_joint1 (J4) | l5 (Index MP Pitch) | Direct map with range rescaling |
| index_joint2 (J5) | l7 (Index PIP Pitch) + l8 (DIP Pitch) | Average or weighted sum — coupled |

**Middle / Ring / Pinky** (UDCAP 4 DOF → XHand 2 DOF each):

| XHand Joint | ← UDCAP Source | Mapping Strategy |
|-------------|----------------|------------------|
| xxx_joint1 | MP Pitch (l9/l13/l17) | Direct map with range rescaling |
| xxx_joint2 | PIP Pitch + DIP Pitch | Average or weighted sum |

> MP Yaw (l10/l14/l18) for middle/ring/pinky is **discarded** — XHand has no corresponding DOF for these fingers.

### 4.2 Range Normalization

UDCAP outputs degrees with negative = flexion. XHand expects radians with positive = flexion (based on preset actions where fist = 110°).

General formula:
```python
xhand_rad = clamp(-udcap_deg * scale_factor, joint_min_rad, joint_max_rad) * (pi / 180)
```

The sign inversion and scale factor are per-joint and must be determined experimentally.

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
| **Graceful shutdown** | On Ctrl+C: set mode=0 (passive/powerless), close device. |
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
- **Wrist control** — UDCAP wrist data (l21-l23) is ignored
- **Force feedback** — XHand fingertip sensor data is not transmitted back to the glove
- **3D visualization / GUI** — Pure command-line tool
- **Motion recording/playback** — No trajectory logging for later replay
- **ROS2 integration** — Use Python SDK directly, not the ROS2 wrapper (reduces dependencies)
- **Arm control** — Only hand joints, no robotic arm integration
- **Multi-user** — Single operator, single pair of gloves

---

## 7. Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | Python 3.10 or 3.12 |
| XHand SDK | `xhand_controller` (from provided .whl, Linux x86_64) |
| Communication | UDP socket (stdlib) + RS485 (via SDK) |
| Configuration | YAML or JSON config file |
| OS | Ubuntu 22.04 or 24.04 x86_64 |
| Dependencies | `xhand_controller`, `pyyaml` (config), stdlib only otherwise |

---

## 8. File Structure (Proposed)

```
udex_to_xhand/
├── main.py                    # Entry point, CLI args
├── udcap_receiver.py          # UDP receiver, JSON parsing
├── joint_mapper.py            # 24→12 mapping logic + config
├── xhand_driver.py            # XHand SDK wrapper (open/send/close)
├── safety.py                  # Watchdog, clamping, shutdown
├── config.yaml                # Mapping parameters, PID gains, IP/port
├── SPEC.md                    # This file
├── xhand_control_sdk_py/      # XHand Python SDK (provided)
├── xhand_control_ros2/        # XHand ROS2 SDK (reference only)
├── udcap关节文档/              # UDCAP joint documentation
├── URDF/                      # XHand URDF models (reference)
└── xhand中文文档/              # XHand Chinese documentation
```

---

## 9. Verification Plan

### Phase 1: Bring-up (单手, 开环)
1. **UDP data validation**: Confirm UDCAP data arrives on Linux, parse all 24 params correctly, verify l0-l23 mapping against hand movements (manually flex each finger while logging values).
2. **XHand SDK smoke test**: Connect one XHand via RS485, run preset actions (fist/palm/V/OK), confirm SDK works.
3. **Single-finger mapping**: Map ONE finger (index) from UDCAP → XHand, verify directional correctness and range.
4. **Single-hand full mapping**: Map all 12 joints for one hand, tune parameters.

### Phase 2: Integration (双手)
5. **Dual XHand connection**: Connect both hands on RS485, verify both hand IDs are discoverable and independently controllable.
6. **Dual-hand teleoperation**: Map both hands simultaneously, test basic gestures.
7. **Left/right mirroring**: Verify sign conventions for right hand.

### Phase 3: Robustness
8. **Watchdog test**: Kill UDCAP → verify XHand holds last position.
9. **Latency measurement**: Log end-to-end latency (UDCAP timestamp → XHand command sent). Target: <50ms.
10. **Stress test**: 30-minute continuous operation, monitor for drift, packet loss, or SDK errors.
11. **Grasping test**: Attempt to pick up objects of varying sizes (pen, cup, ball).

### Acceptance Test
- **Pass**: Operator wearing UDCAP gloves can pick up a cup with dual XHands.

---

## 10. Risk Register

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| 1 | **Joint mapping inaccuracy** — 24→12 mapping produces unnatural motions, especially for thumb | HIGH | HIGH | Make mapping fully configurable (config.yaml). Start with simple linear mapping, iterate with operator feedback. Budget significant tuning time. |
| 2 | **UDCAP parameter ordering unknown** — l0-l23 may not follow the assumed thumb→index→middle→ring→pinky order | MEDIUM | HIGH | First task in Phase 1: experimentally verify each parameter by flexing one finger at a time and recording which l-values change. Document the verified mapping. |
| 3 | **RS485 bus contention with dual hands** — Sending commands to two hands on shared bus may introduce latency or collisions | MEDIUM | MEDIUM | Monitor cycle time. If >10ms per cycle, switch to EtherCAT or use two separate USB-to-serial adapters. |
| 4 | **UDP packet loss / jitter** — WiFi or congested network causes dropped frames or variable latency | MEDIUM | MEDIUM | Use wired Ethernet between Windows and Linux. Implement smoothing filter to interpolate over missing frames. |
| 5 | **Sign convention mismatch** — UDCAP negative=flexion vs XHand positive=flexion may vary per axis and per hand | HIGH | MEDIUM | Configurable sign per joint in config.yaml. Systematic testing in Phase 1. |
| 6 | **XHand PID tuning** — Default kp=100 may cause oscillation or sluggish response at teleoperation speeds | MEDIUM | LOW | Expose PID params in config. Start conservative (low kp), increase gradually. |

---

## 11. Configuration Schema (config.yaml)

```yaml
udcap:
  host: "0.0.0.0"
  port: 9000
  timeout_ms: 200          # Watchdog timeout

xhand:
  protocol: "RS485"        # or "EtherCAT"
  serial_port: "/dev/ttyUSB0"
  baud_rate: 3000000
  left_hand_id: 0
  right_hand_id: 1
  control_mode: 3          # 0=passive, 3=position, 5=force
  default_kp: 100
  default_ki: 0
  default_kd: 0
  default_tor_max: 300
  update_rate_hz: 100

# Per-joint mapping: udcap_indices, weights, sign, offset, clamp
# Format: [udcap_param_index, ...], [weight, ...], sign_multiplier, offset_deg, [min_deg, max_deg]
mapping:
  left:
    thumb_bend:    { sources: [0, 3, 4], weights: [0.4, 0.3, 0.3], sign: -1, offset: 0, clamp: [-10, 110] }
    thumb_rota1:   { sources: [1],       weights: [1.0],            sign: -1, offset: 0, clamp: [-10, 110] }
    thumb_rota2:   { sources: [2],       weights: [1.0],            sign:  1, offset: 0, clamp: [0, 50] }
    index_bend:    { sources: [6],       weights: [1.0],            sign: -1, offset: 0, clamp: [-10, 30] }
    index_joint1:  { sources: [5],       weights: [1.0],            sign: -1, offset: 0, clamp: [0, 110] }
    index_joint2:  { sources: [7, 8],    weights: [0.6, 0.4],       sign: -1, offset: 0, clamp: [0, 110] }
    mid_joint1:    { sources: [9],       weights: [1.0],            sign: -1, offset: 0, clamp: [0, 110] }
    mid_joint2:    { sources: [11, 12],  weights: [0.6, 0.4],       sign: -1, offset: 0, clamp: [0, 110] }
    ring_joint1:   { sources: [13],      weights: [1.0],            sign: -1, offset: 0, clamp: [0, 110] }
    ring_joint2:   { sources: [15, 16],  weights: [0.6, 0.4],       sign: -1, offset: 0, clamp: [0, 110] }
    pinky_joint1:  { sources: [17],      weights: [1.0],            sign: -1, offset: 0, clamp: [0, 110] }
    pinky_joint2:  { sources: [19, 20],  weights: [0.6, 0.4],       sign: -1, offset: 0, clamp: [0, 110] }
  right:
    # Same structure, potentially different signs for mirroring
    # TBD after experimental verification
```

---

## 12. Open Questions (TBD during Phase 1)

1. **UDCAP l0-l23 exact mapping verification** — Must experimentally verify which parameter index corresponds to which joint. The table in Section 3.1 is our best guess, not confirmed.
2. **XHand per-joint angle limits** — The overall range is -90°~110° but individual joints likely have tighter limits. Need to query or measure.
3. **Right hand sign convention** — UDCAP docs say "right hand may need sign negation". Need to test which axes.
4. **RS485 dual-hand addressing** — Can two XHands share one serial port? Or do we need two USB adapters?
5. **Optimal PID parameters** — kp=100 may not be appropriate for teleoperation. Need to tune for responsiveness vs smoothness.
6. **Smoothing filter** — May need a low-pass filter or interpolation to handle UDP jitter. Design TBD based on Phase 1 measurements.

---

## 13. Reference Materials

| Resource | Location |
|----------|----------|
| XHand Python SDK + example | `xhand_control_sdk_py/` |
| XHand ROS2 SDK (reference) | `xhand_control_ros2/` |
| XHand joint names (authoritative) | `xhand_control_ros2/.../xhand_control_ros2.hpp:68-72` |
| XHand data types (C++ header) | `xhand_control_ros2/.../data_type.hpp` |
| XHand Chinese docs (PDF) | `xhand中文文档/` |
| XHand URDF models | `URDF/` |
| UDCAP joint documentation | `udcap关节文档/` |
| UDCAP hand model + bone data | `udcap关节文档/HandDriver Initial Hand Mode Joint Position.txt` |
| UDCAP online docs | https://udexreal.gitbook.io/udexreal-docs/docs-cn/ruan-jian/handdriver-shou-bu-mo-xing-guan-jie-he-shu-ju-shuo-ming |
| UDCAP example UDP data | `example.json` |
| UDP receiver prototype | `test.py` |
