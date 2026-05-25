# ADR-048: Four-Finger Affine Rescale Schema + `--actions calibrate-udcap` UDP-Only Sub-Mode

## Context

M7 ✅ closed dual-hand integration on PC2 with `latency_ms avg=19.4 p95=19.2` — well inside the 50ms SPEC §9 budget. But the first M7→M8 visual session (operator-feedback, 2026-05-20, captured in roadmap revision #11) showed the four fingers (index / mid / ring / pinky, 9 DOF total) systematically failed to close to the palm under a full fist. The 9-joint mapping inherited from M2/M4 was 1:1 UDCAP-degrees-to-XHand-degrees (weighted-sum + sign + offset + clamp + deg→rad). Per joint, the operator's worn-UDCAP reachable curl was empirically `joint1 ≈ 80°` and `joint2 ≈ 92°`, against an XHand clamp ceiling of `[0, 110]` (per M2). That left every fingertip 17–27° short of full closure — not a PID stall, but mapping output never reaching the clamp.

Two derived needs:

1. **A calibration tool**, because SPEC §3.1's UDCAP ranges turned out to be theoretical — the human hand wearing the glove produces a tighter range. Without per-operator measurement, every M8 retune would be guesswork.
2. **A schema extension**, because the existing `weights / sign / offset / clamp` knobs can't rescale input → output by themselves: shifting `offset` translates, but doesn't stretch; widening `clamp` doesn't help when input never reaches the ceiling. The fix needs an explicit affine `input → output` rescale.

The schema change had a hard back-compat constraint: M5b's snapshot test (ADR-030) asserts byte-identical Python ↔ C++ + against the committed `mapper_baseline.json` fixture at ≤ 1e-6 rad. Any unguarded schema change breaks the fixture. The flag-gating mechanism that protects this is ADR-053; this ADR is about the schema + calibration tool itself.

## Decision

**Affine rescale schema** added to `JointConfig` (`src/joint_mapper.hpp` lines 32-58; `legacy_python/joint_mapper.py` mirrored):

```cpp
struct JointConfig {
    // ... existing fields ...
    std::optional<std::pair<double,double>> input_range;   // empirical UDCAP range (deg)
    std::optional<std::pair<double,double>> output_range;  // target XHand range  (deg)
};
```

- **Both-or-none invariant**: schema validation in `load_hand` rejects the configuration if exactly one of `input_range` / `output_range` is present. Always enforced, regardless of `use_new_retarget` flag — config typos fail fast at startup.
- **Applied between `sign*acc+offset` and `clamp`**, mirroring the path of `apply_one`:
  ```
  acc      = Σ weights[i] * src[sources[i]]            # existing
  shifted  = sign * acc + offset                        # existing
  rescaled = (shifted - in_min) / (in_max - in_min)
             * (out_max - out_min) + out_min            # NEW (gated)
  clamped  = clamp(rescaled, clamp_min, clamp_max)      # existing — ADR-020
  radians  = clamped * π / 180                          # existing — ADR-021
  ```
- **Degenerate-input guard**: if `in_max - in_min < 1e-9`, output is `(out_min + out_max) / 2` (defined value, not NaN). Logged once at startup if it ever hits.
- **Clamp remains the final safety**: even with `output_range` filled, clamp is the last gate before deg→rad. ADR-021 two-layer defense unchanged. Per-joint min/max in `clamp` continues to bound XHand command into the joint's URDF mechanical envelope.

**`--actions calibrate-udcap` sub-mode** added (`src/calibrate_udcap.hpp` + `src/main.cpp::run_calibrate_udcap`; `src/cli.cpp` flag plumbing):

- **UDP-only — does not open the XHand serial port.** Construct `UdcapReceiver` only. Safe to run on PC2 with hands powered down. `--port` is accepted-but-ignored (CLI uniformity).
- **Reuses live `JointMapper`** to compute the per-joint pre-clamp expression `sign * Σ(weights[i]·src[sources[i]]) + offset` for each tick. The captured min/max is therefore directly compatible with `input_range` — no manual remap needed, paste the YAML fragment into `config.yaml` and the affine rescale picks up the numbers exactly.
- **`calib==3` AND-gate honored**: warm-up frames where either side's `CalibrationStatus != 3` are dropped (same `try_recv()` used by the live control loop, ADR-029). Captured ranges reflect only fully-calibrated UDCAP frames.
- **`--calibrate-duration <sec>`** flag, default 30s. Mutually exclusive with `--hold` / `--duration` / `--mock` / `--receiver-only` (the same actions-mode guards used by ADR-032's preset path).
- **Operator script in stderr** at start: `5s palm → 5s neutral → 3s × 5 per-finger flex → 5s fist`. Every 5s a progress line prints frames captured + current per-source min/max snapshot.
- **Output is stdout-pastable YAML**: 12 joint lines (`input_range: [min, max]`) + 24 source lines as sanity check. Each joint range should have `max - min ≥ 30°` after the full operator script; sub-30° ranges trigger a stderr warning.
- **0 frames captured → exit 2** with a hint that UDCAP `CalibrationStatus != 3` (the most common cause).

**Calibration data flow (Phase 1 of the M8 Path A runbook):**

```
PC2: operator wears one hand, UDCAP calib status = 3
PC2: ./udex_to_xhand --actions calibrate-udcap --calibrate-duration 30 \
                     --config ../config.yaml --hand left
     → UDP listener, 30s operator script
     → stdout YAML fragment (12 joint + 24 source ranges)
     → tee log
PC2: repeat for right hand
PC2: merge both fragments into docs/logs/m8a-calibrate-fragment-<date>.log
LOCAL: paste joint ranges into config.yaml (left + right)
LOCAL: regen tests/fixtures/mapper_baseline.json (ADR-030/037)
LOCAL: scripts/verify_flag_false_byte_identical.sh → exit 0
LOCAL: commit + push
PC2: git pull → cmake / make → snapshot test → visual fist verify
```

## Consequences

- **Positive — fix is config-only, no source rebuild on retune.** Once `input_range` / `output_range` columns are in `config.yaml`, every subsequent re-tune is a YAML edit + fixture regen + push + PC2 git-pull. No `cmake .. && make`. PC2 binary re-reads YAML on launch.
- **Positive — calibration capture is a single command** instead of a separate Python script. UDCAP receiver and mapper expression are shared with the live control loop, so what calibrate measures is exactly what `apply_one` will produce pre-clamp.
- **Positive — empirical per-operator calibration** replaces SPEC §3.1's theoretical ranges. Different operators (different glove fit, different hand sizes) re-calibrate in 30s × 2 hands.
- **Positive — back-compat preserved by both-or-none invariant + ADR-053 flag-gating.** Flag=false → both fields are `reset()` in `load_hand` and the affine branch never runs. ADR-030 snapshot test stays at ‖Δ‖∞ ≤ 1e-17 rad against the frozen M7 reference (ADR-054).
- **Negative — operator script discipline matters.** A lazy "palm → fist" recording (skipping per-finger flex) produces narrow per-source ranges that overestimate the affine gain on under-exercised joints. The stderr-printed protocol and the `max - min ≥ 30°` sanity check are the only guards; we did not add an enforced QC step. The runbook (`docs/plans/20260521-m8-path-a-runbook.md` Phase 1b) elevates the protocol to documented form.
- **Negative — Path A runbook + this ADR move trust to `config.yaml`.** With 9 four-finger joints × 2 hands × `(input_range + output_range)` that's 36 new floats. SHA-256 fixture self-check (ADR-030) + `verify_flag_false_byte_identical.sh` (ADR-054) catch *deltas* against committed state; they don't catch operator-time data-entry transposition (e.g. swap `input_range[0]` and `[1]`). Mitigation: PC2 startup logs the loaded values; a future startup-time sanity check (in_min < in_max) could be added if needed.
- **Negative — calibration session is operator-paced.** No automatic "I see you've reached steady-state, advancing to next finger" — the operator follows the stderr cadence. Out-of-sync action vs. cadence produces a usable but suboptimal calibration; rerun is the remedy.

## Alternatives Considered

- **Widen the clamp instead of adding affine rescale**: rejected. Clamp is the URDF-grounded mechanical safety; raising it without confirming URDF headroom risks chatter on the joint stop. ADR-055 documents the round-3 reversal where this alternative was tried and bounced.
- **Move calibration to a standalone Python script (e.g. `scripts/calibrate_udcap.py`)**: rejected. Would re-implement `JointMapper`'s pre-clamp expression and drift out of sync; would require the operator to switch between PC2 (binary) and dev Mac (script). Sharing the C++ mapper guarantees the captured expression matches `apply_one` arithmetically.
- **Implement calibration as a runtime flag of the main control loop** (e.g. `--config config.yaml --hand left --calibrate`): rejected. The control loop opens XHand + sends commands; calibration must NOT send commands (operator's hand isn't yet calibrated and the XHand mode/PID isn't in the calibration target configuration). `--actions calibrate-udcap` shares the actions-mode framework that already gates XHand opening (ADR-032/034) — minimum scope cut.
- **Schema variant: single `gain` + `offset` knob instead of `input_range` + `output_range`**: mathematically equivalent (gain = (out_max - out_min) / (in_max - in_min); offset = out_min - in_min * gain), but the four-tuple form makes the calibration → config mapping mechanical (paste UDCAP min/max as input_range; use clamp as output_range). Rejected for ergonomics.
- **Auto-write calibrated values back into `config.yaml`**: rejected. Generated config drifts from git authority; ADR-030 + ADR-037 deliberately chose committed-fixture-with-SHA over generated-at-build for this reason. Calibrate-udcap stays read-only; operator pastes manually + commits.

## References

- `src/joint_mapper.hpp:32-58` — `JointConfig` schema with `input_range` / `output_range`
- `src/joint_mapper.cpp:13-122` — flag-gating in constructor (ADR-053) + schema validation in `load_hand` + affine application in `apply_one`
- `src/calibrate_udcap.hpp` — `CalibStats` struct + entry point declaration
- `src/main.cpp::run_calibrate_udcap` — UDP-only sampling loop (~80 lines)
- `src/cli.{hpp,cpp}` — `--actions calibrate-udcap` token + `--calibrate-duration` flag
- `legacy_python/joint_mapper.py` — Python oracle mirror; ‖C++ − Python‖∞ ≤ 1e-17 rad
- `docs/plans/20260521-m8-path-a-runbook.md` Phase 1 — operator-runnable calibration sequence
- `docs/plans/20260521-m8-tuning-acceptance-plan.md` §0 in-scope #1 + §3 M8a/M8b — design rationale + step-by-step plan
- `docs/logs/m8a-calibrate-left-2026-05-21.log` + `m8a-calibrate-right-2026-05-21.log` + `m8a-calibrate-fragment-2026-05-21.log` — captured ranges that landed in `config.yaml`
- ADR-020 — degree-domain clamping inside mapper (unchanged; affine runs *before* clamp)
- ADR-021 — two-layer clamping defense in depth
- ADR-029 — UDCAP-side `CalibrationStatus == 3` gate (reused by calibrate-udcap)
- ADR-030 — snapshot fixture committed JSON with SHA-256 self-check
- ADR-031 — `legacy_python/joint_mapper.py` as live oracle
- ADR-032 — preset action table header-only; actions framework reused
- ADR-037 — snapshot fixture regen on config.yaml schema change
- ADR-053 — `use_new_retarget` master flag (the gating mechanism that protects the back-compat path)
- ADR-054 — frozen M7 reference fixture + auto-verify shell guard
- ADR-055 — URDF-grounded retune (input_range narrowing, the round-3 reversal that this schema enabled)
