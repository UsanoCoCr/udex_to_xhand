# ADR-029: CalibrationStatus pre-check is UDCAP-side only — XHand driver does not assert it

Date: 2026-05-18
Status: Accepted
Milestone: M5b

## Context

CLAUDE.md "Constraints — do NOT" before this ADR said:
> Do NOT send commands to XHand before verifying hand IDs and CalibrationStatus==3

The sentence was ambiguous about *which* CalibrationStatus. SPEC §5 (Safety Mechanisms) was explicit:
> CalibrationStatus check: Only forward data when UDCAP CalibrationStatus == 3 (calibrated).

But M5b plan §3.3's first draft tried to map "CalibrationStatus == 3" onto an XHand SDK call inside `XHandDriver::open()`, claiming the SDK had a `HandState_t::CalibrationStatus` field. Investigation in the vendor headers (`xhand_control_sdk/include/data_type.hpp`):
- `HandState_t` (lines 117-120): contains `std::array<FingerState_t, 12> finger_state` + `std::array<SensorData_t, 5> sensor_data`. **No `CalibrationStatus` field.**
- `CalibrationStatus` enum (lines 45-53): values are `CALIBRATION_SUCCESS=0`, `CALIBRATION_ERROR_ANGLE_OUT_OF_RANGE`, `CALIBRATION_ERROR_ADC_OUT_OF_RANGE`, etc. It is a **per-finger calibration-error code**, not a 0..3 state.
- `DeviceInfo_t::is_calibrated` (line 42): `uint8_t` boolean. Not 0..3.

M3 Python `xhand_driver.py:50-63` does no calibration check at all — only `list_hands_id()` non-empty + `get_hand_type()`. M5a `test_serial` likewise performs no calib gating. So the plan-conformant reading of CLAUDE.md was self-contradictory (assert a field that does not exist).

The operator stopped before writing code and surfaced the contradiction (see conversation log 2026-05-18). The fix was approved before any C++ landed.

## Decision

Split the precondition into two independent rules, each enforced where it makes sense:

| Rule | Source     | Enforcement point                                                                                  |
| ---- | ---------- | -------------------------------------------------------------------------------------------------- |
| (a)  | XHand side | `XHandDriver::open()` (`src/xhand_driver.cpp:36-58`) — asserts `list_hands_id()` non-empty + identifies L/R via `get_hand_type` (case-insensitive). No calib field is touched. |
| (b)  | UDCAP side | `UdcapReceiver::try_recv()` (`src/udcap_receiver.cpp:138`) — drops frames where `L_CalibrationStatus != 3` OR `R_CalibrationStatus != 3`. |

SPEC §5's "Wait for first valid UDP packet before moving" is then implicit: `try_recv` returns a non-empty optional only when rule (b) is satisfied, and the main loop only calls `send_*` inside `if (frame_opt) { … }`. So no send happens until both rules hold.

CLAUDE.md "Constraints — do NOT" rewritten in the M5b commit to spell out the split explicitly: "(a) `list_hands_id()` reports the expected hand(s) AND (b) the most recent UDCAP frame has `L_/R_CalibrationStatus == 3`."

## Consequences

**正面**
- Code references real SDK fields only. No phantom `HandState_t::CalibrationStatus`. Compiles cleanly under `-Wall -Wextra -Wpedantic` (M5b §6.4 / §6.8 confirmed no diagnostics).
- Matches M3 Python behavior byte-for-byte at the XHand driver layer. No regression risk from over-validation.
- Single locus of UDCAP calib enforcement (`UdcapReceiver::try_recv`). M6 fault-injection has one place to test (kill UDCAP → watchdog → hold-last-position).
- CLAUDE.md is now unambiguous. Future readers (humans + AI agents like ChatGPT/Gemini reviewers) won't have to guess which side the check belongs on.
- M5b §6.7 (2026-05-18) ran for 10.0s with `calib L=3 R=3` reported on all 616 valid frames; no parse errors. The UDCAP-side enforcement is exercised in steady state with no false negatives.

**负面 / 风险**
- An operator who forgets to start UDCAP HandDriver sees "XHand SDK version 1.4.3 / hand_id=1 type=Left" in the log but no commands ever leave the loop. M5b §6.7 confirmed this is visually obvious: `[INFO] waiting for first packet...` prints up front and no `[recv …]` lines follow. Not a silent failure, but worth noting that "XHand opened OK" no longer implies "ready to teleoperate".
- If a future XHand firmware revision *does* expose a hand-level calib state, this ADR will need a follow-up to add the check. Forward-compatible — doesn't paint anything into a corner.

## Alternatives

1. **Keep plan's original assertion inside `XHandDriver::open()`** — Rejected. Requires inventing a field that does not exist. Options like polling `read_state()` and inspecting `finger_state[i].commboard_err` would be heuristic, not a calibration check. Misleading semantics.
2. **Add an explicit "have-seen-first-calibrated-frame" gate variable in the main loop** — Rejected as redundant. The `if (frame_opt) { … }` conditional already gates sends on calib==3 (because `try_recv` only returns an engaged optional in that case). A parallel variable would spell the same logic twice and risk drift.
3. **Drop UDCAP calib check entirely** (match Python M4 `main.py` literally — Python M4 doesn't enforce calib==3 either, despite SPEC §5) — Rejected. SPEC §5 is the authoritative safety document. M4 missed it; M5b is the right moment to upgrade to the documented behavior. The check costs nothing in practice (M5b §6.7 saw calib L=3 R=3 throughout).
4. **Move both rules into a separate `SafetyGate` / `PreflightCheck` class** — Rejected as premature abstraction (CLAUDE.md "no abstractions beyond what the task requires"). Two `if` checks in two different files is simpler than introducing a coordinating class.
