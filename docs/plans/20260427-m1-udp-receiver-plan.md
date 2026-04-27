# M1: Real UDP Receiver Plan

**Date**: 2026-04-27
**Milestone**: M1 — Real UDP Receiver
**Status**: Complete

## Context

M0 complete (stub pipeline end-to-end). M1 replaces the UDP stub in `udcap_receiver.py` with a real non-blocking UDP socket, so the system can receive live UDCAP data from the Windows PC. Only **one source file changes** substantially. No XHand hardware needed.

---

## 1. File List

| File | Action | Responsibility |
|------|--------|----------------|
| `udcap_receiver.py` | **MODIFY** | Replace `receive()` stub with non-blocking UDP socket; add drain-to-latest; harden `_parse()`; add `close()`; rewrite `__main__` for standalone operation |
| `main.py` | **MODIFY (minimal)** | Fix busy-spin: ensure sleep runs on `data is None`; add `receiver.close()` in shutdown |
| `config.yaml` | NO CHANGE | Already has `udcap.host`, `udcap.port`, `udcap.timeout_ms` |
| `example.json` | NO CHANGE | Still used by `--mock` path |

---

## 2. Data Flow (Real UDP Mode)

```
Windows PC (UDCAP HandDriver)
    |  UDP JSON, port 9000, 60-120 Hz
    v
UdcapReceiver.__init__()
    |  socket.bind(("0.0.0.0", 9000))
    |  socket.setblocking(False)
    v
UdcapReceiver.receive()              <-- called by main.py at ~100Hz
    |
    +-- Drain loop: recvfrom()       <-- read ALL queued packets, keep LAST only
    |   +-- BlockingIOError -> exit loop
    |
    +-- json.loads(latest_raw)       <-- JSONDecodeError -> return None (skip frame)
    |
    +-- _parse(raw_dict)             <-- malformed -> return None (skip frame)
    |   Build {Name: Value} lookup (ADR-001)
    |   Extract l0-l23, r0-r23, CalibrationStatus
    |
    +-- return {
          "left":        [24 floats, degrees],
          "right":       [24 floats, degrees],
          "calib_left":  int,
          "calib_right": int,
        }
```

### No-data path

```
recvfrom() -> BlockingIOError -> latest_raw is None -> return None
main.py -> data is None -> sleep remainder of interval -> continue
```

### Multiple-packets-queued path (drain-to-latest)

```
recvfrom() -> packet1 (stale)   <- discard
recvfrom() -> packet2 (stale)   <- discard
recvfrom() -> packet3 (latest)  <- keep
recvfrom() -> BlockingIOError   <- exit loop
_parse(packet3) -> return latest data only
```

---

## 3. Key Design Decisions

### 3.1 Non-blocking socket with `setblocking(False)`

`receive()` must return immediately (data or None) -- the 100Hz control loop owns timing. Using `setblocking(False)` raises `BlockingIOError` when no data is ready, which we catch and return `None`. No socket timeout needed -- `timeout_ms: 200` is for the M5 watchdog, not for socket config.

### 3.2 Drain-to-latest

For teleoperation we always want the most recent pose. The drain loop reads all queued packets and only parses the last one. Bounded by kernel buffer (~130 packets max); in practice drains 0-2 per call.

### 3.3 CalibrationStatus: report but don't filter

`receive()` returns CalibrationStatus as-is in the dict. It does NOT skip frames based on CalibrationStatus -- that per-hand filtering belongs to the control loop (M4/M5). Standalone mode prints CalibStatus for operator visibility.

### 3.4 Harden `_parse()` with try/except

Current `_parse()` can raise `StopIteration` (empty dict), `KeyError` (malformed Parameter entries), or `TypeError`. Wrap the body in try/except, return `None` on any error. This is the "skip frame on parse error" safety requirement from CLAUDE.md.

### 3.5 Fix main.py busy-spin on `data is None`

Current M0 code: `if data is None: continue` skips the sleep, causing 100% CPU when no UDP packets arrive. Fix: restructure so sleep always runs regardless of whether data was received.

---

## 4. Changes to `udcap_receiver.py`

### 4.1 New imports

`socket`, `time`, `argparse` (all stdlib).

### 4.2 `__init__` -- real mode path

- Create `socket.AF_INET, socket.SOCK_DGRAM`
- Bind to `(config["host"], config["port"])`
- `sock.setblocking(False)`
- Store `self._sock`, `self._last_addr = None`

### 4.3 `receive()` -- real mode path

- Drain loop: `while True: recvfrom()` -> catch `BlockingIOError` to break
- Keep only last raw bytes
- `json.loads()` -> catch `JSONDecodeError` / `UnicodeDecodeError` -> return None
- `self._parse()` -> return None on any error
- Store source address in `self._last_addr`

### 4.4 `_parse()` -- harden

- Wrap existing body in `try: ... except (StopIteration, KeyError, TypeError, ValueError, AttributeError): return None`
- `AttributeError` needed for cases like `{"bad": "string_value"}` where `raw[key]` is a string, not a dict

### 4.5 New `close()` method

- Close socket if not None

### 4.6 New `last_addr` property

- Return `self._last_addr` (for standalone display)

### 4.7 Rewrite `__main__` block

- `argparse`: `--host`, `--port`, `--duration`, `--mock`
- FPS counter, print throttle (~10 lines/sec), CalibStatus display
- Final summary on exit

---

## 5. Changes to `main.py`

### 5.1 Fix busy-spin

Ensure the timing/sleep logic (lines 72-75) is always reached, regardless of whether `receive()` returned data.

### 5.2 Add `receiver.close()`

After `driver.close()`, add `receiver.close()` to clean up the socket on shutdown.

---

## 6. Test Strategy

### Test 1: Mock regression (no hardware) -- PASSED
```bash
python main.py --mock --duration 3
```
Expected: identical to M0 -- ~250 ticks, same values every tick, clean exit.
Result: 253 ticks, values match M0 output, clean exit.

### Test 2: Standalone mock (no hardware) -- PASSED
```bash
python udcap_receiver.py --mock --duration 2
```
Expected: prints example.json values, CalibStatus L=3 R=3, exits after duration.
Result: 168 frames, CalibStatus L=3 R=3, FPS ~84, clean exit.

### Test 3: JSON parse failure (requires LAN, manual test)

**Terminal 1** -- start receiver:
```bash
python udcap_receiver.py --port 9000 --duration 10
```

**Terminal 2** -- send garbage + valid data:
```python
import socket, json

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
TARGET = ("<Linux IP>", 9000)

# Send garbage (not JSON)
sock.sendto(b'not json', TARGET)

# Send wrong structure (valid JSON, wrong schema)
sock.sendto(json.dumps({"bad": "structure"}).encode(), TARGET)

# Send valid UDCAP data
with open("example.json") as f:
    sock.sendto(f.read().encode(), TARGET)

sock.close()
```

Expected: receiver does not crash, only prints the 1 valid frame, garbage packets silently ignored.

### Test 4: Real UDCAP standalone (requires UDCAP hardware)
```bash
python udcap_receiver.py --port 9000 --duration 10
```
Expected:
```
Listening on UDP 0.0.0.0:9000...
[192.168.x.x] L: l0=-47.6 l1=-60.0 ... l23=-1.0
[192.168.x.x] R: r0=-9.5 r1=-46.1 ... r23=-1.0
CalibStatus: L=3 R=3 | FPS: 89.2

Received 892 frames in 10.0s (avg 89.2 FPS)
```
Verify:
- Values change when operator moves gloved fingers
- FPS in 60-120 range
- CalibStatus shows L=3 R=3 when calibrated
- No crash on 10s sustained running

### Test 5: Integration with main.py (real UDP, stub XHand)
```bash
python main.py --config config.yaml --duration 5
```
Expected: tick lines with L/R joint values (radians) that change with glove movement. XHand driver still stub (no-op send).

### Test 6: Ctrl+C clean shutdown
```bash
python udcap_receiver.py --port 9000
# Wait 2 seconds, press Ctrl+C
```
Expected: prints summary line, no traceback, port immediately re-bindable.

---

## 7. Verification Commands

### Automated (no hardware, already passed):
```bash
python main.py --mock --duration 3
python udcap_receiver.py --mock --duration 2
```

### Manual (requires LAN / UDCAP hardware):
```bash
# Primary M1 done-definition:
python udcap_receiver.py --port 9000 --duration 10
```
Pass criteria: 24+24 params printed, values track glove, FPS 60-120, CalibStatus L=3 R=3, no crash, clean exit.

---

## 8. Deferred (not M1 scope)

- CalibrationStatus filtering (M4/M5)
- Watchdog timer (M5)
- Structured logging (M5)
- Smoothing filter (M7)
- Real XHand commands (M3)
- Real joint mapping (M4)
- Parameter verification (M2)
