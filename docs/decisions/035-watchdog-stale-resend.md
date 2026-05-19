# ADR-035: Watchdog Stale Reaction — Resend Last Cmd @ 100Hz, LOG_WARN @ 1Hz

## Context

M5b shipped `safety::Watchdog` with `update()` / `is_stale()` / `has_seen_frame()` (`src/safety.{hpp,cpp}`) but `main.cpp` only ever called `update()` — the staleness reaction was deferred to M6 (`src/safety.hpp:20` comment, M5b plan §3.3 acceptance gap).

SPEC.md §5 mandates the behavior: "If no valid UDP packet received for >200ms, hold last commanded position. Log warning." CLAUDE.md "Safety" section repeats it. The roadmap §M6 test 1 is the verification.

The question is **how** to "hold last position". Three operational shapes were considered before committing code:

- (A) **Stop sending entirely** and rely on the XHand's internal position controller to hold the most recent setpoint until a new frame arrives.
- (B) **Resend the last successfully-sent radian array at the same 100Hz cadence**, so the RS485 link stays warm and the setpoint is constantly re-asserted.
- (C) **Switch to mode=0 (passive)** on stale and let the operator decide when to resume — same exit path used by SIGINT/SIGTERM (ADR-018).

LOG cadence is a separate but related sub-decision: every 10 ms (one per tick), once per stale event, or rate-limited.

## Decision

**Take (B): keep sending `last_left_rad` / `last_right_rad` every tick during stale. WARN log rate-limited to 1Hz inside `main.cpp`.** Implementation in `main.cpp` else-branch of `if (frame_opt)`; `safety::Watchdog` API unchanged. Recovery (first fresh frame after stale) emits a single `LOG_INFO "watchdog: recovered after Nms"`.

## Consequences

- **正面**: SDK error feedback stays live during outages — if RS485 actually breaks during the outage, `send_command` surfaces an error_code through ADR-017's log-not-crash path on the very next tick; under option (A) the operator would see nothing until the outage ends.
- **正面**: No reliance on undocumented XHand internal hold semantics. Vendor docs don't specify whether the controller decays the setpoint after silence; M5c didn't observe long enough to know. Option (B) sidesteps the question.
- **正面**: Behaviour is identical between "UDCAP packet loss bursts" (sub-200ms) and "UDCAP stopped" (>200ms) — controller always sees the same setpoint stream, just at different freshness levels. No transition glitch.
- **正面**: 1Hz WARN means a 30-minute stress test (M8) caps stale log volume at ~1800 lines worst-case, well under file/stderr saturation.
- **负面**: Stale period accumulates send_command CRC/busy errors if RS485 itself degrades. Mitigated by ADR-017 (log not crash) and Risk R1 in M6 plan §9 (operator counts WARN lines in P1 log).
- **负面**: A genuine grasp-with-no-glove scenario keeps the hand clenched indefinitely — by design (operator must explicitly Ctrl+C / SIGTERM to release per ADR-018). Documented in SPEC §5 nota bene.

## Alternatives Considered

- **(A) Stop sending**: Rejected — relies on undocumented controller hold semantics; loses RS485-error visibility during stale; introduces a "send vs not-send" branch in the SDK's view of the stream.
- **(C) Drop to mode=0 on stale**: Rejected — equivalent to a forced release-on-stale, dangerous if the hand is mid-grasp. SPEC §5 explicitly says "hold last position", not "release".
- **LOG_WARN every tick**: Rejected — 100Hz × 30min = 180k lines per stale event; floods stderr and any tee'd log.
- **LOG_WARN once per stale event**: Rejected — stale → recover → stale cycles would silently keep happening with only the first one visible; operator can't gauge outage severity from log.
- **Move rate-limiter into `safety::Watchdog`**: Rejected — rate-limiting is a logging concern, not a state-machine concern; pulling logger handles into the safety library defeats the unit-testability that ADR-021 / M6 plan §3.4 are built on.

## References

- SPEC.md §5 (safety mechanisms)
- CLAUDE.md "Safety (non-negotiable)"
- `docs/plans/00-roadmap.md` §M6 test 1
- `docs/plans/20260519-m6-safety-hardening-plan.md` §2.1, §3.2
- ADR-017 (log-not-crash on send errors)
- ADR-018 (mode=0 not powerless)
- ADR-021 (two-layer clamping; same defense-in-depth thinking)
