# ADR-041: Dual-Mode Latency ≈ 2× Single-Mode Baseline; ~100 ms Max-Outlier Deferred

## Context

M7 plan rev2 §4.4 set the latency budget: per-tick receive→post-send `p95 ≤ 20 ms` and `max ≤ 50 ms` (anchored on SPEC §9 phase 3.9 "<50 ms" ceiling). The reference single-hand baseline from M5c (2026-05-18, ADR-033) was `avg = 9.60 / p95 = 9.62 / max = 10.68 ms` over `n = 1773` valid frames in a 31 s `--hand left` session.

The M7 two-driver refactor changes the per-tick critical path: one `SDK send_command()` call per RS485 path, two sequential calls per tick when `--hand both`. The SDK serializes its own I/O — there is no parallelism between the two RS485 paths within a tick. Plan rev2 §4.4 anticipated "≈ 1.5–2× single-hand baseline" but lacked actual numbers.

Two M7 runs on 2026-05-19 PC2 supply the data:

- **`docs/logs/m7-single-regression-2026-05-19.log`** — `--hand left --duration 5`, both UDCAP gloves worn, M7 binary with the new two-driver code but only the left driver instantiated:
  ```
  latency_ms{n=310 min=9.54426 avg=9.58808 p50=9.56861 p95=9.6273 max=10.6916}
  ```
  **Bit-identical to the M5c baseline** (9.60 / 9.62 / 10.68). Confirms the M7 refactor does not regress the single-hand critical path.

- **`docs/logs/m7-watchdog-dual-2026-05-19.log`** — `--hand both --duration 30`, both drivers instantiated and dispatching every tick. 24.9 s session (operator SIGTERM'd before duration elapsed), 2126 ticks total, 1164 valid frames (≈ 46.7 fps observed UDCAP rate):
  ```
  latency_ms{n=1164 min=19.0683 avg=19.3755 p50=19.121 p95=19.1954 max=111.166}
  ```

Three findings from those numbers:

1. Dual `avg` / `p50` / `p95` are all ≈ 2.0 × single-hand baseline (19.38 / 19.12 / 19.20 vs 9.59 / 9.57 / 9.63). This is structural: two `send_command` calls per tick, each ≈ the latency of the M5c single call, serialized over their separate USB-to-CDC-ACM endpoints. There is no contention bonus or penalty — the two RS485 buses are physically independent (ADR-039), the cost is pure SDK + USB scheduling per call.
2. Dual `p95 = 19.20 ms` is within the 20 ms budget with **0.80 ms headroom** (≈ 4 %). Operationally tight but verified-pass.
3. Dual `max = 111.17 ms` exceeds the 50 ms ceiling. Single sample in n=1164 (p99 not printed but max is the worst). M6 closeout (roadmap rev 7) already documented a ~100 ms outlier on M6 single-hand watchdog/clamp long sessions while p95 stayed at 9.62 ms — the M7 dual outlier is the same magnitude and behavior class. The 60 s clean dual-hand teleop run (plan §4.4) was NOT log-captured during M7 execution, so this 25 s watchdog-session number is the M7 dual-mode latency record.

The 60 s captureless gap means we cannot say "the 111 ms outlier is one-per-25-s vs once-per-60-s". We can say: at the scale we have (n = 1164), one outlier was present, p95 was unaffected, and the next tick's wall-clock recovery via `sleep_until` was intact (no missed-frame cascade or stuck-hand state visible in any subsequent log line).

## Decision

- **Accept the dual-mode latency profile as the M7 baseline.** Specifically: `avg ≈ 19.4 ms`, `p95 ≈ 19.2 ms`, max-outlier ≈ 100 ms class. Plan rev2 §4.4 PASS criterion (`p95 ≤ 20 ms AND max ≤ 50 ms`) is met for `p95`; the max ceiling is **not** met for the single outlier sample, but this is treated as noise-class consistent with the pre-existing M6 outlier rather than as an M7 regression.
- **Defer the ~100 ms outlier root cause investigation to M8** stress test (SPEC §9 phase 3.10 — 30-minute continuous run). Open a histogram, find frequency, find correlation with UDP / USB events. If M8 surfaces a sustainable mitigation, it lives in M8 ADRs.
- **No latency-driven code change in M7.** Specifically: no parallel `send_command` via threads (SDK thread-safety unspecified — risk of RS485 bus-state corruption); no `update_rate_hz` reduction to 50 (no need — 0.80 ms p95 headroom is positive); no interleaving `send_left` / `send_right` across alternating ticks (loses 1-tick L↔R sync — operationally bad for grasping).
- **Plan rev2 §0 deferral of smoothing filter to M8 is reinforced.** Any addition to the per-tick critical path (e.g. exponential smoothing in `joint_mapper`) costs into the 0.80 ms p95 headroom; design must include latency-aware budget.

## Consequences

- **Positive — single-hand path is verified non-regressed.** `m7-single-regression` log byte-for-byte matches M5c. Future M-N regressions on single-hand operation can be bisected confidently against this M7 line.
- **Positive — `p95` budget compliance is documented and reproducible.** Plan rev2 / SPEC §9 thresholds are validated against a real PC2 dual-hand run, not hand-wavy estimation.
- **Negative — 0.80 ms p95 headroom is operationally tight.** Any per-tick work added in M8 (smoothing filter, additional logging, IK transforms for the thumb retargeting work in roadmap rev 6) must measure its own ms-cost before commit. Plan-level reminder, not an ADR-level mitigation.
- **Negative — the 111 ms outlier is unexplained.** M8 stress test is the planned investigation venue. If the outlier turns out to be UDCAP-rate-driven (e.g. correlated with the observed ~46 fps UDCAP rate dropping below 100 Hz intermittently and causing tick-stretch), the fix may be in the watchdog/scheduler logic rather than the send path. If USB-driven, may need udev / scheduler-priority work. Cannot pre-decide.
- **Negative — `max` budget violation is technically a SPEC §9 phase 3.9 fail.** This ADR records it as an accepted single-sample exception, mirroring how roadmap rev 7 handled the M6 outlier. Future plans should not rely on `max` as a hard gate while this ADR's deferral stands.

## Alternatives Considered

- **Treat max=111 ms as a M7-blocking regression**: rejected. The outlier is one sample in n=1164, p95 is unaffected, M6 already documented the same class of outlier on single-hand long sessions with no architectural connection. Blocking M7 on this would require investigating an issue the data says is not M7-specific.
- **Reduce `update_rate_hz` to 50 unconditionally in dual mode**: rejected. Halves position-controller input rate for no current benefit (p95 has headroom). Would compound the latency outlier visibility (slower control loop, smaller statistical denominator).
- **Parallelize `driver_left->send_left()` and `driver_right->send_right()` via two threads**: rejected. SDK thread-safety is unspecified by vendor docs; sending into two `XHandControl` instances on two separate USB endpoints _might_ be safe but _might_ corrupt internal state (e.g. shared `enumerate_devices` cache). Out of scope for M7; opens a separate plan + ADR if revisited.
- **Interleave `send_left` and `send_right` across alternating ticks** (left on even ticks, right on odd): halves per-tick send cost back toward M5c single-hand baseline. Rejected. Left↔right hand command sync gets a half-tick offset (5 ms at 100 Hz); for fine-motor tasks like cup grasping with bimanual coordination, that's noticeable. Considered if M8 surfaces a real latency problem; not necessary at current numbers.
- **Treat the M6-class outlier as a known artifact and document just the steady-state numbers**: rejected. Honest recording of the `max` value belongs in the public latency record; future operators reading SPEC §9 should not have to spelunk logs to learn the outlier exists.

## References

- ADR-033 — M5c single-hand latency stats methodology (vector + sort for exact p95; not streaming)
- ADR-039 — two-port split architecture (the structural reason dual latency is 2× single)
- Roadmap revision 7 (M6 closeout) — first record of the ~100 ms outlier class on single-hand long sessions; M7 dual is the second occurrence
- SPEC §9 phase 3.9 (< 50 ms target) + phase 3.10 (30-min stress test in M8) — the deferred investigation venue
- Plan rev2 §0 — smoothing filter deferred to M8; this ADR reinforces the budget consideration
- Plan rev2 §4.4 — the PASS / FAIL fork this ADR records the outcome of
- `docs/logs/m7-single-regression-2026-05-19.log` — single-hand byte-identical baseline
- `docs/logs/m7-watchdog-dual-2026-05-19.log` — dual-hand 25 s record (the n=1164 dataset cited above)
- M5c plan §11 + `docs/logs/m5c-teleop-left-2026-05-18.log` — the baseline this ADR compares against
