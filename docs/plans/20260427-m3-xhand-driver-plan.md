# M3: Real XHand Driver (Single Hand) — Implementation Plan

**Date**: 2026-04-27
**Milestone**: M3 — Real XHand Driver（单手）
**Status**: Planning
**Depends on**: M0 (file structure)

---

## 1. File List

| File              | Action     | Responsibility                                                                                                                                                           |
| ----------------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `xhand_driver.py` | **MODIFY** | Replace stub with real SDK wrapper: open_serial, list_hands_id, get_hand_type, send_command, mode=0 shutdown, close_device. Mock mode preserved for no-hardware testing. |
| `config.yaml`     | NO CHANGE  | Already has all needed params: serial_port, baud_rate, hand IDs, kp/ki/kd/tor_max, control_mode                                                                          |
| `main.py`         | NO CHANGE  | Already calls `driver.send("left", radians)` and `driver.close()` — interface unchanged                                                                                  |
| `safety.py`       | NO CHANGE  | Clamp logic used by main.py before calling driver — not driver's concern                                                                                                 |

**Zero new files.** M3 is a drop-in replacement of the stub inside `xhand_driver.py`.

---

## 2. SDK API Surface (from C++ headers, exposed via Python binding)

Source: `xhand_control_ros2/.../xhand_control.hpp` + `data_type.hpp`

```
XHandControl()                                    # constructor
  .enumerate_devices("RS485") → ["/dev/ttyACM0"]  # discover ports
  .open_serial(port, baudrate) → ErrorStruct       # connect
  .list_hands_id() → [0, 1, ...]                  # discover connected hands
  .get_hand_type(hand_id) → (ErrorStruct, "L"/"R") # left or right
  .get_sdk_version() → "1.1.7"
  .read_version(hand_id, joint_id=0) → (ErrorStruct, str)
  .send_command(hand_id, HandCommand_t) → ErrorStruct
  .read_state(hand_id, force_update) → (ErrorStruct, HandState_t)
  .close_device()                                  # close connection (no args)

HandCommand_t:
  .finger_command[0..11] → FingerCommand_t
    .id: int         # joint index 0-11
    .kp: int         # proportional gain (default 100)
    .ki: int         # integral gain (default 0)
    .kd: int         # derivative gain (default 0)
    .position: float # target position in RADIANS
    .tor_max: int    # max torque mA (default 300)
    .mode: int       # 0=passive, 3=position, 5=force
```

**Note**: SDK example `exam_close_device()` only prints but never calls `device.close_device()`. This is an example bug — we must call `close_device()`.

---

## 3. Data Flow

### 3.1 Standalone mode (M3 done-definition)

```
CLI: python xhand_driver.py --port /dev/ttyACM0 --actions fist,palm,v,ok
    │
    ▼
open_serial(port, 3000000) ─── ErrorStruct.error_code == 0?
    │                              no → print error, exit(1)
    ▼
list_hands_id() → [hand_id, ...]
    │
    ▼
get_hand_type(hand_id) → "L" or "R"
    │
    ▼
print: "Connected: hand_id=N, type=Left/Right, SDK=x.x.x"
    │
    ▼
For each action in [fist, palm, v, ok]:
    │  build HandCommand_t:
    │    12 x FingerCommand_t { id=i, kp=100, ki=0, kd=0,
    │                           position=PRESETS[action][i]*π/180,
    │                           tor_max=300, mode=3 }
    │  send_command(hand_id, command)
    │  print: "Action fist: sent 12 joints, OK"
    │  sleep(1.0)
    ▼
Shutdown:
    │  send mode=0 for all 12 joints (passive/powerless)
    │  close_device()
    ▼
print: "Device closed."
```

### 3.2 Integration mode (called from main.py, unchanged interface)

```
main.py:
    driver = XHandDriver(config["xhand"], mock=False)
                │
                ▼
          __init__ (real mode):
              XHandControl()
              open_serial(config serial_port, baud_rate)
              list_hands_id() → discover IDs
              get_hand_type(id) → build {"left": int_id, "right": int_id} map
              Build template HandCommand_t with config kp/ki/kd/tor_max/mode
                │
                ▼
    driver.send("left", [12 floats in radians])
                │
                ▼
          send() (real mode):
              Look up numeric hand_id from "left"
              For i in 0..11: command.finger_command[i].position = radians[i]
              device.send_command(hand_id, command)
                │
                ▼
    driver.close()
                │
                ▼
          close() (real mode):
              Send mode=0 command to all connected hand IDs
              device.close_device()
```

### 3.3 Mock mode (existing behavior, preserved)

```
__init__: no SDK, no serial
send(): no-op (main.py handles printing)
close(): no-op
```

---

## 4. Key Design Decisions

### 4.1 Auto-discover hand_id ↔ left/right mapping

`__init__` calls `list_hands_id()` then `get_hand_type()` for each discovered ID. Builds a `{"left": 0, "right": 1}` dict. If `send("left", ...)` is called but no left hand was discovered, raise an error.

Rationale: Config has `left_hand_id: 0` as a hint, but the actual hardware mapping should be confirmed via `get_hand_type()`. This catches miswired setups.

### 4.2 Pre-build template HandCommand_t in __init__

Create one HandCommand_t per hand with kp/ki/kd/tor_max/mode pre-filled from config. In `send()`, only update the 12 `.position` fields. Avoids rebuilding 12 FingerCommand_t every tick at 100Hz.

### 4.3 Shutdown = mode=0 then close_device

On `close()`, first send a command with all joints `mode=0` (passive — hand goes limp). Then call `close_device()` to release the serial port. This matches CLAUDE.md safety: "Ctrl+C → set mode=0 → close_device".

### 4.4 Error handling on send_command

If `send_command` returns `error_code != 0`, log the error message but **do not crash**. The 100Hz loop should be resilient to occasional send failures (e.g., bus busy). A separate M5 watchdog will handle persistent failures.

### 4.5 No read_state in M3

`read_state()` is available in the SDK but not needed for M3. M3 is open-loop: send positions, don't read back. Sensor feedback is explicitly out of scope (CLAUDE.md: "do NOT read XHand sensors").

---

## 5. Test Strategy

### Test 1: SDK import smoke test (no hardware, any machine)

```bash
python -c "from xhand_controller import xhand_control; print(xhand_control.XHandControl().get_sdk_version())"
```

**Expected**: Prints SDK version string (e.g., `1.1.7`). No crash.

**Purpose**: Confirms SDK .whl is installed correctly and the C extension loads.

**Prerequisite**: `pip install xhand_controller-*-cp310-cp310-*.whl` in conda env.

### Test 2: Mock mode unchanged (no hardware)

```bash
python xhand_driver.py --mock --action fist
```

**Expected**: Same output as M0 stub — prints 12 radian values, "Done." No SDK calls.

**Purpose**: Regression — mock mode still works for development on macOS/non-Linux.

### Test 3: Serial connection + device discovery (requires hardware, no motion)

```bash
python xhand_driver.py --port /dev/ttyACM0 --discover
```

**Expected output**:
```
SDK version: 1.1.7
Connected to /dev/ttyACM0 at 3000000 baud
Hand IDs found: [0]
  hand_id=0: type=Left, firmware=x.x.x
Device closed.
```

**Purpose**: Validates open_serial, list_hands_id, get_hand_type, close_device. **No motion commands sent** — safe for first hardware test.

**Failure modes**:
- `Permission denied` → run `sudo chmod 666 /dev/ttyACM0`
- `open device error` → check USB cable, power
- `list_hands_id` returns empty → hand not powered on or wrong baud rate

### Test 4: Single preset action (requires hardware, CAUSES MOTION)

```bash
python xhand_driver.py --port /dev/ttyACM0 --action palm
```

**Expected**: XHand physically moves to palm pose (fingers open). Terminal prints:
```
Connected: hand_id=0, type=Left, SDK=1.1.7
Action palm: sent 12 joints, OK
Shutdown: mode=0 (passive)
Device closed.
```

**Purpose**: Validates send_command with known-good preset values. Palm is the safest first action (fingers open, low torque risk).

### Test 5: Full preset sequence — M3 done-definition (requires hardware, CAUSES MOTION)

```bash
python xhand_driver.py --port /dev/ttyACM0 --actions fist,palm,v,ok
```

**Expected**: XHand physically executes fist → palm → V → OK, each held for 1 second. Terminal prints:
```
Connected: hand_id=0, type=Left, SDK=1.1.7
Action fist: sent 12 joints, OK
Action palm: sent 12 joints, OK
Action v: sent 12 joints, OK
Action ok: sent 12 joints, OK
Shutdown: mode=0 (passive)
Device closed.
```

**Purpose**: M3 done-definition from roadmap. Confirms SDK install, serial connection, hand_id discovery, position control, and graceful shutdown all work end-to-end.

### Test 6: Graceful shutdown on Ctrl+C (requires hardware)

```bash
python xhand_driver.py --port /dev/ttyACM0 --action fist
# While hand is in fist pose, press Ctrl+C
```

**Expected**: Hand goes limp (mode=0), terminal prints "Shutdown: mode=0 (passive)", "Device closed."

**Purpose**: Validates safety shutdown path. Hand must not stay in fist if operator interrupts.

---

## 6. Verification Commands (Summary)

### Automated (no hardware): -- PASSED --
```bash
python xhand_driver.py --mock --action fist
# Pass: prints 12 radians, "Done.", no crash
```

### Manual (requires XHand hardware + Linux):
```bash
# Step 1: Discover only (safe, no motion)
python xhand_driver.py --port /dev/ttyACM0 --discover

# Step 2: Single safe action
python xhand_driver.py --port /dev/ttyACM0 --action palm

# Step 3: Full sequence (M3 done-definition)
python xhand_driver.py --port /dev/ttyACM0 --actions fist,palm,v,ok
```

**Pass criteria** (from roadmap):
1. XHand executes all four preset actions in sequence
2. Each action held for ~1 second
3. Hand goes limp after sequence completes (mode=0)
4. Terminal shows hand_id, hand_type, SDK version
5. No crash, no error messages on clean run

---

## 7. CLI Interface Design

```
python xhand_driver.py --port /dev/ttyACM0 [options]

Options:
  --port PORT          Serial port (required for real mode)
  --baud BAUD          Baud rate (default: 3000000)
  --mock               Use stub mode (no hardware)
  --discover           Connect, print device info, exit (no motion)
  --action ACTION      Execute one preset: fist|palm|v|ok
  --actions A,B,C      Execute multiple presets in sequence, 1s each
  --hold SECONDS       Hold time per action (default: 1.0)
```

`--action` and `--actions` are mutually exclusive. `--discover` skips all motion.

---

## 8. Deferred (not M3 scope)

- Dual-hand on same serial bus (M6)
- Watchdog / timeout handling (M5)
- Per-joint limit clamping in driver (M5 — currently done in main.py via safety.py)
- Reading hand state / sensor data (explicitly out of scope per CLAUDE.md)
- EtherCAT support (not planned — RS485 sufficient per SPEC.md)
- PID tuning (M7)
