# ADR-055: URDF as Mechanical-Limit Ground Truth — Round-3 Reversal (Narrow `input_range`, Don't Raise `clamp`)

## Context

M8b round 2 (commit `472da5a`, 2026-05-25) pushed four-finger `output_range[1]` to the M7 clamp ceiling `110°` and `thumb_rota2 output_range` to `[30, 50]`. PC2 visual verify (operator): four fingers still don't reach palm closure; thumb still on the outside. Operator hypothesized the *clamp* was the bottleneck — "if `output_range[1]` is already at clamp and we're still under-reaching, the clamp must be cutting us off."

Before round 3 committed a clamp raise, a URDF lookup (`URDF/XHAND 1/XHAND1_URDF_ver 1.3/xhand1_*/urdf/xhand_*.urdf`, the vendor-shipped joint specifications) overturned the diagnosis:

| Joint axis            | URDF mechanical max | M7 clamp `[min, max]` | M8b r2 `output_range[1]` |
|-----------------------|---------------------|------------------------|---------------------------|
| 4-finger `joint1`     | **109.95°**         | [0, 110]               | 110 (= clamp ceiling)     |
| 4-finger `joint2`     | **109.95°**         | [0, 110]               | 110 (= clamp ceiling)     |
| `thumb_rota2`         | **89.95°**          | [0, 50]                | 50                        |
| `thumb_bend`          | **104.97°**         | [-10, 110]             | (not yet touched in r2)   |

Two findings emerged:

1. **Four-finger clamp was already at the URDF ceiling.** Raising it from `[0, 110]` to `[0, 130]` (as path-A-runbook §242 Example C proposed for the "rare UDCAP overshoot" case) would have crashed the joint-limit hard stop — actively dangerous, not just unhelpful. The "fingers don't close" symptom was not "clamp cuts us off" — the binary already commanded the full range. The real bottleneck was `input_range[1]` (operator's worn-UDCAP max) being *higher* than the operator's normal-fist UDCAP value: M8a calibration captured `joint1 ≈ 80°` and `joint2 ≈ 92°` (peak full-flex), but a "normal" fist reached only UDCAP 65–75°, which after affine maps to 80–90% of the clamp ceiling — not 100%. To get a normal fist to *reach* the ceiling, `input_range[1]` needs to come *down* to the normal-fist value, not up.

2. **`thumb_rota2` clamp was *under* the URDF ceiling.** M7 clamp `[0, 50]` used only 56% of URDF range. Path A runbook §213 had proposed `clamp [0, 90]` (matching URDF) earlier; rounds 1 and 2 kept it at `[0, 50]` purely to preserve the flag=false guard (ADR-054 frozen reference output for `thumb_rota2` under example.json `l20 = -0.6/1.2` lies inside both `[0, 50]` and `[0, 90]` bands, so the raise is byte-safe). Round 3 lifted to URDF max, gaining 40° of opposition headroom that had been on the table all along.

The mistake was inverted symmetry: assuming "fingers underclose" → "raise clamp" was the symmetric fix to "thumb on outside" → "raise output bias." It wasn't. The four-finger problem was in the *input* range (UDCAP under-fill), not the *output* range; the thumb problem was in the *output* range (clamp left URDF headroom on the table), not the input.

Round 3 (commit `064801a`) committed both reversals together: tighten four-finger `input_range[1]` 80→72 / 92→82, raise `thumb_rota2 clamp` to `[0, 90]` + `output_range [40, 90]`. PC2 visual: four fingers closer to palm; thumb still on outside (which led to round 4's `thumb_bend` hypothesis, ADR-056).

## Decision

- **Treat the vendor-shipped URDF (`URDF/XHAND 1/XHAND1_URDF_ver 1.3/xhand1_*/urdf/xhand_*.urdf`) as the authoritative mechanical-limit ground truth.** Not the M7 clamp values (which were experimental and conservative), not SPEC §3.1 mapping table (which was theoretical for UDCAP, not for XHand), not the operator's intuition during tuning.
- **Before raising any `clamp[max]`, look up the URDF max for that joint.** If `clamp[max] < URDF_max`, raising up to URDF is safe. If `clamp[max] == URDF_max`, the clamp is not the bottleneck — look elsewhere (input range, sign, weights).
- **The standard fix for "joint doesn't reach desired output" is, in priority order:**
  1. **Tighten `input_range[1]`** — bring the mapped-from-100% UDCAP value down to the operator's actual normal-use peak, so a normal motion saturates the output.
  2. **Open `output_range[1]`** up to the URDF max (= `clamp[max]` after step 3).
  3. **Raise `clamp[max]`** only if it's currently below URDF max AND the URDF lookup confirms headroom.
  4. **Adjust `sign` / `weights` / `offset`** if the affine domain is fundamentally misaligned — but at that point the symptom is "moves the wrong way" or "doesn't move at all", not "doesn't reach."
- **Document the URDF max alongside `clamp` in `config.yaml` comments** when the clamp is set to anything other than URDF max, so a future reader knows where the headroom is. The round-3 commit message + comment block in `config.yaml` (lines 62-104) records the URDF lookup explicitly.
- **The "raise clamp" intuition was wrong; codify the reversal in this ADR** so future tuning sessions start from the URDF-first checklist, not from the symmetric-fix instinct.

## Consequences

- **Positive — round-3 commit avoided a hardware-damaging clamp raise.** If the operator had committed `clamp [0, 130]` per the Example C ladder from the runbook, the first fist-flex on PC2 would have driven four fingers into the URDF stop at 109.95°, producing chatter / current spike / possibly position-control loss. The URDF lookup pre-empted this.
- **Positive — `thumb_rota2` gained 40° of opposition headroom in round 3** that had been left on the table since M2. Round 1's `[0, 50]` was an unnecessary conservatism inherited from early M2 tuning when URDF data wasn't being consulted; round 3 corrected it.
- **Positive — calibration data (M8a) becomes the input-range tuning baseline, but its peak isn't necessarily the right `input_range[1]`**. Calibration measures *maximum reachable* (operator gives full effort); normal use is lower. The right `input_range[1]` is the *normal-use peak*, not the *maximum-effort peak*. M8b round 3 picked 72/82 (close to 80% of calibration peak) explicitly to map normal motion to full XHand range. This insight wasn't in M8a Step A.6 — runbook Phase 2 diagnostics should be updated to note "tighten input_range[1] from calibration peak by ~10–20% if normal-use closure is the target."
- **Negative — operator must consult URDF on every clamp decision.** URDF files are in the repo (`URDF/XHAND 1/XHAND1_URDF_ver 1.3/`), but they're not summarized anywhere as a quick reference table. This ADR provides a partial one (four-finger joint1/joint2, thumb_rota2, thumb_bend); a fuller `docs/xhand-joint-limits.md` would be useful but is out of M8 scope.
- **Negative — URDF authority assumes the URDF is accurate.** Vendor-shipped URDF values are nominal — actual physical joint stops could differ by manufacturing tolerance. If real joint chatter occurs at e.g. 108° on a `[0, 110]` clamp = `[0, 109.95]` URDF, the URDF is slightly optimistic; we'd back off `clamp[max]` to 105° empirically. This hasn't happened in M8a–M8b, but it's a known risk.
- **Negative — round 3 changed `clamp` for `thumb_rota2`** — a flag=false-affecting field. The commit message verifies that `example.json l20 = -0.6/1.2` falls inside both old `[0, 50]` and new `[0, 90]` bands, so flag=false output is byte-identical (the raised ceiling is never hit by the fixture). This holds *for the current example.json* but not for any future fixture refresh where `l20` could pass 50°. If `example.json` is ever updated with a thumb-opposition pose, the frozen reference would need a deliberate re-freeze (per ADR-054's escape hatch). Logged here so the future reader knows what to recheck.

## Alternatives Considered

- **Trust the operator's intuition** ("clamp must be the bottleneck"): rejected. Round 3 is the exact case where intuition was wrong and the URDF was right. The cost of a wrong clamp raise (hardware damage) is much higher than the cost of consulting the URDF (30 seconds, one grep).
- **Use the M7 clamp values as the ground truth** (don't change them in M8): rejected for `thumb_rota2` — `[0, 50]` was leaving 40° of mechanical range unused, directly bottlenecking opposition. M7's clamps were chosen conservatively in M2/M4 without URDF cross-reference; M8 has the URDF + the cycle budget to re-tune.
- **Auto-import URDF limits at startup**: tempting (`urdf-parser-py` is small), but rejected for M8 scope. The URDF files are large XML files with full joint hierarchy + collision + visual data; parsing them requires a new C++ dependency (`urdfdom` or similar) and adds a startup-time data path that doesn't currently exist. M8 sticks with hand-curated `clamp` values in `config.yaml` + URDF-confirmed-by-grep in commit messages.
- **Use the URDF as a startup-time *sanity check*** (warn if `clamp[max] > URDF_max`): a softer alternative — no parsing dependency if implemented as a Python script in `scripts/`. Useful, but out of M8 scope. Candidate for M9's startup-validation pass.
- **Pre-emptively raise all four-finger `clamp[max]` to `URDF_max + ε`**: rejected. The URDF max is the *physical* limit; commanding above it relies on the joint controller to clamp gracefully. Vendor SDK might or might not — and even if it does, the resulting current spike + audible joint-stop chatter is operator-confusing.

## References

- `URDF/XHAND 1/XHAND1_URDF_ver 1.3/xhand1_*/urdf/xhand_*.urdf` — vendor URDF files, authoritative joint limits
- `config.yaml` lines 53-104 — operator's inline narrative of rounds 1 → 2 → 3 retune logic with URDF cross-references
- Commit `064801a` (M8b round 3) — full commit message includes the URDF table + explicit reversal rationale
- Commit `472da5a` (M8b round 2) — the "push output_range to clamp" change that round 3 had to course-correct
- `docs/plans/20260521-m8-path-a-runbook.md` Phase 2 (diagnosis table) — current version routes most "joint doesn't reach" cases to "Example A: affine rescale", consistent with this ADR; Example C ("raise clamp") is correctly tagged as "极少见 / UDCAP > 110°" — the rare case where input genuinely exceeds output, not the M8b-actual case where input is under-filled
- `docs/plans/20260521-m8-tuning-acceptance-plan.md` §3 M8b Step B.6–B.7' — original plan flow assumed calibration → input_range = calibration peak; this ADR adds the "normal-use peak < calibration peak" insight that round 3 surfaced
- ADR-020 — degree-domain clamping inside mapper (URDF-bounded clamp is applied here)
- ADR-021 — two-layer clamping defense in depth (URDF is the upper bound on layer 1; safety re-clamp is layer 2)
- ADR-048 — affine rescale schema (the mechanism this ADR's "tighten input_range" prescription operates through)
- ADR-053 — `use_new_retarget` master flag
- ADR-054 — frozen M7 reference fixture (the byte-identical assertion that this ADR's `thumb_rota2 clamp` raise had to preserve)
- ADR-056 — `thumb_bend` rest bias (round 4's continuation of the URDF-driven retune; uses `thumb_bend` URDF max = 104.97° as the `output_range[1]` ceiling)
