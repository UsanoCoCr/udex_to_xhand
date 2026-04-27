# M0: Skeleton — Stub Pipeline Plan

**Date**: 2026-04-27
**Status**: ✅ Complete

## Context

First milestone of the UDCAP→XHand teleoperation project. Goal: all modules exist as stubs, `main.py` main loop runs end-to-end. No hardware needed. This establishes the file structure and data flow that subsequent milestones replace piece-by-piece.

---

## 1. File List

| File | Status | Responsibility |
|------|--------|----------------|
| `config.yaml` | NEW | All tunable params: UDP port, XHand serial config, per-joint mapping (stub values), PID defaults |
| `udcap_receiver.py` | NEW | Stub: load `example.json` once, return l0-l23 / r0-r23 each call |
| `joint_mapper.py` | NEW | Stub: take first 12 of 24 params, flip sign, deg→rad |
| `safety.py` | NEW | Stub: `clamp(values, limits)` with loose range [-2, 2] rad |
| `xhand_driver.py` | NEW | Stub: `send()` is no-op, `close()` is no-op |
| `main.py` | NEW | Entry point: argparse, 100Hz loop, chain all modules |

---

## 2. Data Flow

```
example.json
    │
    ▼
UdcapReceiver.receive()
    │  returns: { "left": [l0..l23], "right": [r0..r23] }  (24 floats each, degrees)
    │
    ▼
JointMapper.map(hand, udcap_24)          ×2 (left, right)
    │  stub: take indices [0:12], multiply by -1, convert deg→rad
    │  returns: [j0..j11]  (12 floats, radians)
    │
    ▼
safety.clamp(joints_12, STUB_LIMITS)
    │  returns: [j0..j11] clamped to [-2, 2] rad
    │
    ▼
XHandDriver.send(hand_id, joints_12)     (no-op in stub)
    │
    ▼
main.py prints: [tick NNN] L: [...12 values] R: [...12 values]
```

---

## 3. Key Decisions

See `docs/decisions/` for full ADRs:

1. **ADR-001**: UDCAP parameter lookup by name string, not array index
2. **ADR-002**: Stub mapper applies sign flip (`*-1`) instead of pure passthrough
3. **ADR-003**: config.yaml ships full mapping schema upfront (stub ignores it)
4. **ADR-004**: xhand_driver.send() is no-op; main.py owns stdout printing

---

## 4. Verification Results

| Check | Result |
|-------|--------|
| `python main.py --mock --duration 3` | 252 ticks / 3.0s (~84Hz on macOS), L: 12 + R: 12 radian values |
| Values in radians, magnitude < 2.0 | All clamped correctly |
| Consistent values per tick (mock replay) | Confirmed |
| Clean exit, no traceback | Confirmed |
| `python udcap_receiver.py` standalone | 24+24 params, CalibStatus L=3 R=3 |
| `python xhand_driver.py --action fist` | 12 radian values printed |
| `safety.clamp([5, -5, 0.5], ...)` | `[2, -2, 0.5]` |

Note: ~84Hz instead of 100Hz due to macOS `time.sleep` overhead (~4ms/cycle). Expected to be closer to 100Hz on Linux target.
