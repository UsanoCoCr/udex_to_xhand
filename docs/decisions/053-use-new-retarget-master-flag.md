# ADR-053: `mapping.use_new_retarget` Master Flag — Required Field + Single-Master + Load-Phase Gating

## Context

M8b (ADR-048) extended the mapper schema with optional `input_range` / `output_range` for affine rescale; M8c (ADR-049) reused the same schema for thumb retargeting. Both depend on the same arithmetic in `apply_one`, applied between `sign*acc+offset` and `clamp`. The combined extension covers 9 four-finger joints + 3 thumb joints = 12 joints × 2 hands = 24 mapping entries that can carry the new fields.

Two operational requirements drove a back-compat gating mechanism:

1. **Hardware-floor rollback in seconds.** Mid-session tuning on PC2 needs a guaranteed instant return to the M7 baseline that the operator already knows works ("flag a switch, restart binary"). Reverting via `git revert` is too slow if a config edit causes XHand chatter or hits a joint stop.
2. **M5b/M7 snapshot test (ADR-030) must keep asserting byte-identical Python ↔ C++** with the same fixture, *even after the new schema lands*. If the gating mechanism leaks (e.g. flag=false still runs the affine path because `input_range` happened to be present), the snapshot starts diverging in deep floating-point noise and the regression guard collapses.

Several gating designs were possible:

- **Implicit by field presence**: `input_range`/`output_range` present → use new path; absent → use M7 path. Pro: zero explicit flag. Con: forgetting to delete a half-pasted `input_range` silently changes behavior; rolling back from M8 to M7 means hand-deleting 24 entries from config.
- **CLI override** (`--use-new-retarget=true|false`): pro: live A/B without restarting fixture pipeline. Con: two sources of truth for the same setting (config + CLI) — debugging "why is the output different" gets harder with two knobs.
- **Per-pipeline flags** (`use_affine_rescale` for M8b, `use_thumb_retarget` for M8c): pro: independent toggle. Con: partial-on intermediate states (four-finger new + thumb old) are hard to reason about during a problem, and "is this M8b or M8c that broke it?" diagnosis happens at 100Hz on hardware.
- **Hot path runtime branch** (`if (flag) apply_affine() else skip`): pro: easy. Con: adds a per-tick branch × 24 joints × 100Hz = 2,400 branches/sec on the realtime control path. The mapper hot path is currently branch-free in the inner loop.

What landed (commit `2bbe998`):

## Decision

- **Add a single required boolean field `mapping.use_new_retarget` at the top of the mapping block in `config.yaml`.** No default — the YAML key MUST be present and explicitly `true` or `false`. Missing key → `JointMapper` constructor throws with the exact remediation message (`"mapping.use_new_retarget required (set to false to keep M7 baseline; true enables M8 retarget pipeline)"`). Same enforcement in `legacy_python/joint_mapper.py`.
- **One flag controls all of M8's new retarget paths** — both M8b four-finger affine rescale and M8c thumb retargeting. No `use_affine_rescale` / `use_thumb_retarget` sub-flags. Simplifies the operator's mental model ("am I on the new path or the old path?") and eliminates intermediate states from on-call diagnosis.
- **No CLI override.** `config.yaml` is the sole source of truth. Live A/B is a `sed -i 's/use_new_retarget: true/use_new_retarget: false/' ../config.yaml` + binary restart — one edit, one restart, one source.
- **Gating happens in `load_hand` (cold path), not in `apply_one` (hot path).** When `use_new_retarget == false`, `load_hand` parses `input_range`/`output_range` (so schema typos still fail fast even when not used) and then unconditionally `reset()`s both `std::optional` fields on every `JointConfig`. `apply_one`'s `if (jc.input_range && jc.output_range)` branch is then dead at runtime — the optional is empty, the branch is never taken, the realtime 100Hz tick is byte-identical to M5b/M7.
- **Schema validation (`size == 2`, both-or-none) is independent of the flag** — config typos fail at startup regardless of whether the gated path is on. This keeps the schema check authoritative even on a flag=false session.
- **`JointMapper` exposes `use_new_retarget() const` getter + `left_config()` / `right_config()`** so the calibrate-udcap sub-mode (ADR-048) can compute the pre-clamp expression without going through `apply_one`. Other code paths do not consult the flag — by design, only `load_hand` reads it.

## Consequences

- **Positive — hardware rollback is one `sed` away.** M8 Path A runbook Appendix A makes this the official rollback gate. No `git revert`, no rebuild, no fixture regen. Restart binary, hand goes back to M7 behavior immediately.
- **Positive — `apply_one` stays branch-free.** 100Hz × 24 joints × 2 hands × N seconds of teleop sees zero added flag-check work. M5c's `latency_ms avg=9.6 p95=9.6 max=10.7` and M7's `avg=19.4 p95=19.2` baselines remain undisturbed by M8 schema additions on the hot path (verified by the M8 final single-regression log).
- **Positive — fail-fast on missing key prevents silent fallback.** If a future commit drops the `use_new_retarget` line by accident, every `--config` consumer (main binary, snapshot test, calibrate-udcap) throws at startup with the explicit message. Compare to "default to false on missing" which would silently downgrade everyone to M7 the moment the line goes missing.
- **Positive — single-source-of-truth simplifies operator triage.** When teleop misbehaves, the first diagnostic question is "what does `grep use_new_retarget config.yaml` say?" — one place, two values, definitive.
- **Negative — `config.yaml` requires a top-level `use_new_retarget` key forever**, even in pre-M8 contexts (e.g. running M5b/M6/M7 snapshots against the current binary). Old fixtures committed before M8a do not have this key; running them now fails-fast at startup. Mitigation: the frozen reference fixture `mapper_baseline_m7_frozen.json` was regenerated *after* the flag landed (ADR-054); all snapshot work uses the post-A.0 fixture which assumes the flag exists.
- **Negative — adding a second M-future retarget path (e.g. a new wrist module or a velocity-aware mapper) would mean overloading this flag again or introducing a second one.** The "single master" choice is M8-local; if a future milestone genuinely needs a separate independently-toggleable retarget, a new flag is right, but the flag layout becomes plural. Defer that until it actually happens.
- **Negative — load-phase reset means a `JointMapper` constructed under flag=false cannot be "promoted" to flag=true without re-construction.** If we ever wanted hot-reload of the flag, we'd need to either re-parse on toggle or always retain both forms in memory. For M8 (restart-on-edit is the workflow) this is fine; M9 introduces ROS2 and may bring hot-reload pressure, but that's its own ADR if it happens.

## Alternatives Considered

- **Default to `false` if key missing**: rejected. The whole point of the flag is to prevent unintentional path swap; defaulting to "safe M7" on missing key sounds nice but means a config error that drops the key silently disables M8 — operator thinks new path is on, hardware says no, debug session is long. Required-field + fail-fast is louder and easier to fix.
- **Two flags (one per retarget pipeline)**: rejected per Context. Partial-on intermediate states make on-hardware diagnosis harder, and there's no M8 use case for "four fingers new, thumb old" or vice versa — they share the same `input_range`/`output_range` schema and depend on calibration data captured in the same operator session.
- **CLI flag instead of (or in addition to) YAML key**: rejected. Two sources of truth for the same setting; configuration semantics get muddy when CLI says false and YAML says true. The CLI is for orchestration (`--config`, `--hand`, `--duration`), the YAML is for parameters. Keep them separate.
- **Hot-path runtime branch (`if (flag) ...`)**: rejected. Adds 2,400 branches/sec on the realtime loop with zero realtime upside. Cold-path reset is functionally identical and free at runtime.
- **Implicit field-presence gating**: rejected because half-edits of config (paste 12 lines, forget to commit, restart binary) would produce ambiguous state. Required flag forces an explicit operator decision per session.
- **Per-joint flag (`use_new_retarget: true` per joint)**: rejected. 24 per-joint flags + one global = N+1 places to look during triage. Single global flag is the right granularity for the M8 ✅ acceptance gate.

## References

- `src/joint_mapper.hpp:64-103` — `bool use_new_retarget_` member + `use_new_retarget() const` getter + `left_config() / right_config()` getters
- `src/joint_mapper.cpp:13-39` — constructor reads & validates the flag before calling `load_hand`
- `src/joint_mapper.cpp:122-128` — `load_hand`'s `if (!use_new_retarget) { jc.input_range.reset(); jc.output_range.reset(); }` cold-path reset
- `src/joint_mapper.cpp::apply_one` — hot path: `if (jc.input_range && jc.output_range) { ... affine ... }` — dead branch when flag=false because the optional is always empty
- `legacy_python/joint_mapper.py` — mirror: required-field check + reset on load
- `config.yaml` top-of-mapping — current value (`true` after M8b round 4)
- Commit `2bbe998` — A.0 plumbing landed
- `docs/plans/20260521-m8-tuning-acceptance-plan.md` §0 in-scope #0 — design rationale
- `docs/plans/20260521-m8-path-a-runbook.md` Appendix A — one-line rollback gate
- ADR-030 — committed-fixture-with-SHA snapshot policy (the regression that this flag protects)
- ADR-037 — snapshot regen on schema change (paired discipline: every config schema change includes a fixture regen)
- ADR-048 — M8b four-finger affine rescale (the schema this flag gates)
- ADR-049 — M8c thumb retargeting (also gated by this flag)
- ADR-054 — frozen M7 reference fixture + auto-verify guard (the test that proves this flag's correctness)
