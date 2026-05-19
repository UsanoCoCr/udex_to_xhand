# ADR-038: "watchdog: recovered after Nms" Reports Time Since Last WARN, Not Total Stale Duration

## Context

M6 plan §3.2 designed the watchdog recovery log line as a single emission on the stale → fresh transition:

```cpp
if (was_stale) {
    auto gap_ms = duration<double, std::milli>(tick_start - last_warn_ts).count();
    LOG_INFO("watchdog: recovered after " << gap_ms << "ms");
    was_stale = false;
}
```

`last_warn_ts` is the same `std::chrono::steady_clock::time_point` that the 1Hz rate limiter (ADR-035) uses to gate `LOG_WARN` emissions. The reuse was intentional in the plan — one less piece of state to thread through the loop — but the consequence was not spelled out:

- **N = freshness of recovery relative to the most recent WARN**, not total outage duration.
- **N is bounded above by the WARN cadence**: with 1Hz WARNs, N ∈ [0, 1000ms].

Plan §4.2 P1 expected behavior was "`watchdog: recovered after Nms` (N 约等于 UDCAP 停发的毫秒数)". On PC2 (`docs/logs/m6-watchdog-2026-05-19.log:11-21`) the operator turned UDCAP off for roughly 10 seconds and saw 10 `[WARN] no UDP for >200ms` lines (correctly rate-limited per ADR-035), then on recovery:

```
[INFO] watchdog: recovered after 87.2445ms
```

87ms is the time from the 10th WARN (≈10s into the outage) to the first fresh frame. **The total outage was ~10s; the log line says 87ms.** Plan §4.2 P1 expectation was wrong about this; the safety behavior (hand held, link kept warm, single recovery acknowledgment) was right.

## Decision

**Keep the current `tick_start - last_warn_ts` formula. Treat the log line as a recovery-acknowledgment, not as outage-duration telemetry.** No code change. The operator can estimate total outage as `(number of stale WARN lines × 1 second) ± 1s` because ADR-035 fixes the WARN cadence at 1Hz.

We deliberately **do not** introduce a separate `stale_start: std::optional<steady_clock::time_point>` (capture on `was_stale = true` transition, recompute gap from it, reset on recovery). The four-line change would yield a more accurate log message, but it adds state to the main loop just for log precision, and the precise number is already recoverable from the existing log via `grep -c '^\[WARN\] watchdog'`.

## Consequences

- **正面**: Main loop's per-stale state stays minimal: `last_left_rad`, `last_right_rad`, `was_stale`, `last_warn_ts`. No fifth variable to maintain or reason about.
- **正面**: The recovery line answers the operationally useful question — "did the controller see fresh data again, and how recently relative to my last WARN" — without claiming to be an SLO metric.
- **正面**: N ∈ [0, 1000ms] makes the log line trivially bounded and visually distinguishable from latency or other ms-scale metrics in the same file.
- **负面**: Plan §4.2 P1 expected text is materially wrong about what N represents. Fixed in plan §8 Execution Record + this ADR. M7 / M8 plans that copy P1 verbiage must amend.
- **负面**: Operator must count WARN lines to estimate total stale duration. Mitigated by ADR-035's fixed 1Hz cadence (count ≈ seconds of outage, ±1s).
- **负面**: Future M8 stress test (30 min continuous) may want true outage histograms. Reopening this ADR with a `stale_start` field is the natural extension; deferred.

## Alternatives Considered

- **Track `stale_start` separately and emit true outage duration**: Most accurate, ~4 lines of code. Rejected for M6 closeout because (a) main loop has already been validated by PC2 fault-injection runs (P1–P5) and CLAUDE.md "Executing actions with care" advises against late code changes after acceptance, (b) the WARN-line count provides the same information at the cost of one `grep`. Candidate for M8 if stress testing surfaces a real need.
- **Emit `total_stale_ms = N_warns × 1000` as a hint in the recovery line**: Rejected. Mixes fixed-cadence assumption into a log message that should describe observed events, not inferred ones. If ADR-035 ever moves off 1Hz the hint silently goes wrong.
- **Drop the "recovered after Nms" line entirely**: Rejected. The recovery acknowledgment is operationally valuable (signals the outage has actually ended, distinct from "no further WARNs because operator paused everything"). The imprecise number is better than no number.
- **Reset `last_warn_ts` to `steady_clock::now() - 10s` on every recovery so the next stale event's first WARN fires immediately**: Already in the code (`main.cpp` initializes `last_warn_ts` to past on startup) for the startup case. Does not apply to the recovery semantic — only affects the next stale entry's first WARN latency.

## References

- ADR-035: watchdog stale resend @ 100Hz, LOG_WARN @ 1Hz (the cadence this ADR depends on)
- M6 plan `docs/plans/20260519-m6-safety-hardening-plan.md` §3.2 (the `last_warn_ts` reuse)
- M6 plan §4.2 P1 (the materially wrong expectation about N)
- `docs/logs/m6-watchdog-2026-05-19.log:11-21` (10 WARNs, recovered=87ms — the observation that prompted this ADR)
- `src/main.cpp` 397-403 (the emit site, post-M6 commit f8931f6)
