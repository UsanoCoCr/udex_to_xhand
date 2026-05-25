# ADR-056: `thumb_bend` Rest-Bias (+40°) Is the Swing-Toward-Palm Mechanism, Not `thumb_rota2` Alone

## Context

After M8b round 3 (commit `064801a`, ADR-055) lifted `thumb_rota2 clamp` to `[0, 90]` and pushed `output_range` to `[40, 90]`, the operator on PC2 still reported: "大拇指还是有问题，明显在手侧面，我认为一开始的offset也需要修改，现在大拇指还是基本一直保持在手侧面，没有明显变化" (thumb still clearly on the side of the hand, no obvious change between rounds 2 and 3).

This was unexpected. Round 3 had:

- Lifted `thumb_rota2 clamp` from `[0, 50]` to `[0, 90]` (URDF physical max, +80% range).
- Pushed `output_range[0]` from `30°` to `40°` (initial palm bias at UDCAP `l20=0`).
- Pushed `output_range[1]` from `50°` to `90°` (full URDF range at UDCAP `l20=41°`).

All three changes should have rotated the thumb toward the palm visibly. They did not, by the operator's eye.

Two readings of the failed verification were possible:

- **(a) `thumb_rota2` *is* the correct opposition axis (per ADR-013's `thumb_rota2 ← l20` source identification), but the 40° rest bias isn't enough — needs 60–75°.** Solution: push `output_range[0]` further (40 → 60–75) and possibly raise `clamp[0]` from `0` to keep the affine span sensible.
- **(b) The "thumb on side" issue isn't a `thumb_rota2` insufficiency — it's that `thumb_bend` sits at 0° at rest.** At open palm, UDCAP `l0 = l1 = l2 ≈ 0` → weighted-sum (`0.3·l0 + 0.3·l1 + 0.4·l2`) = 0 → `sign × acc + offset = 0` → `thumb_bend` command 0° → thumb base extended straight out laterally. The opposition rotation `thumb_rota2` rotates only the tip about an already-extended base; rotating the tip doesn't bring the base toward the palm. The thumb visibly "swings inside" only if the *base* (the bend joint) moves first.

Round 4 (commit `b28bac4`) tested hypothesis (b) without backing off hypothesis (a): keep round-3 `thumb_rota2 output_range [40, 90]` unchanged, and additionally add `thumb_bend input_range [0, 60]` + `output_range [40, 100]`. At UDCAP rest, `thumb_bend` is commanded to 40° (vs. M7: 0°); at full UDCAP curl (weighted+sign ≈ 60), `thumb_bend` reaches 100° (vs. M7: 60°). Whole response curve shifted +40° upward while still spanning the full UDCAP range.

URDF check (ADR-055 reusable methodology): `thumb_bend` URDF mechanical max = `104.97°`. Round-4 `output_range[1] = 100°` sits 5° under URDF max — safe.

PC2 visual verify after round 4 (user: "大拇指和四指虽然可能后续还需要微调，但从视觉上没有太大问题了"): thumb position acceptable. Hypothesis (b) confirmed — or at least, the additive (a)+(b) interpretation works at the visual level. Cup acceptance (M8e, deferred) is the next gate to distinguish "looks OK" from "actually opposing".

## Decision

- **`thumb_bend` is the primary swing-toward-palm axis for the thumb retargeting**, not (only) `thumb_rota2`. UDCAP's resting glove position has `l0/l1/l2 ≈ 0` — the unmodified weighted-sum produces a `thumb_bend` command of 0°, which leaves the XHand thumb base laterally extended. To make the thumb visibly oppose the four fingers, `thumb_bend` must be biased away from its M7 zero-rest pose.
- **Implementation**: per-joint affine with rest bias.
  - `thumb_bend` (both L + R): `input_range: [0, 60]`, `output_range: [40, 100]`. At UDCAP rest the affine maps `0 → 40°`; at full curl `60 → 100°`. Pre-flex of 40° is applied unconditionally, independent of the UDCAP curl input. `clamp [-10, 110]` (M7, unchanged) — `output_range[1] = 100` sits 4.97° under URDF max, leaving headroom.
- **`thumb_rota2` round-3 configuration kept unchanged** — the opposition rotation still contributes (additive to the bend pre-flex), and the URDF-lifted clamp + output range stays in effect. This means at full UDCAP opposition (l20 = 41° + l0/l1/l2 ≈ 60°), the thumb commands ~100° bend + ~90° rota2 — close to URDF-bounded extreme opposition, which is the desired cup-acceptance pose.
- **`thumb_rota1` remains M7 baseline** — UDCAP `l3` input range is narrow and the M7 mapping has no clear stuck-axis symptom. If round 5 (post-M8e, deferred) finds thumb pad orientation is off, `thumb_rota1` is the next axis to tune.
- **The "thumb on side" diagnostic update is committed here** as a future reference: when an XHand operator reports "thumb on the outside of the hand" or "thumb won't oppose," the first axis to examine is `thumb_bend` rest pose, not `thumb_rota2` rotation. This reverses the natural intuition ("opposition" → "opposition axis = rota2") and matches the URDF-revealed mechanical truth that `thumb_bend` controls base position, not just curl.

## Consequences

- **Positive — thumb retargeting reached visual acceptance in round 4 without needing algorithm B or C.** ADR-049's "algorithm A is enough" conclusion depends on this hypothesis update; without `thumb_bend` rest bias, algorithm A on `thumb_rota2` alone would not have moved the base.
- **Positive — codifies a non-obvious diagnostic for future thumb tuning sessions.** A different operator (different hand size, different glove fit) re-tuning the thumb will start from `thumb_bend` rest bias as the primary axis, not retread the M8b round-1→2→3 path that kept `thumb_bend` untouched.
- **Positive — `thumb_bend` was the only thumb axis still pristine-M7 going into round 4** (`rota1` and `rota2` had been touched in rounds 1–3). The hypothesis was therefore the cheapest to test — single axis, single round.
- **Negative — visual acceptance ≠ cup acceptance.** The thumb pad orientation in round 4 is not measured against any quantitative target. If under cup the thumb pad rotates incorrectly (twist around the bend axis), `thumb_rota1` is the unblocked dial — but no data drives that decision yet. M8e (deferred) is the gate that will surface this.
- **Negative — +40° rest bias is a globally-applied offset.** At UDCAP rest the thumb pre-flexes 40° even when the operator is doing something else with the other four fingers; if the operator wants the thumb intentionally relaxed-and-extended (e.g. for sign-language gestures), there is no way to suppress the bias short of flipping `use_new_retarget` to false. Acceptable for cup-grasp use case; possibly limiting for broader gesture vocabulary later.
- **Negative — additive interpretation of (a)+(b) is unfalsified.** Round 4 didn't isolate which axis (bend or rota2) was contributing how much. If a future round needs to back off, the operator may have to A/B-test by reverting `thumb_rota2` to round-2 config and re-running, to see whether the bend bias alone is sufficient. This is acceptable iteration cost (one revert, one commit, one PC2 verify).
- **Negative — `thumb_bend` clamp is `[-10, 110]` while `output_range` is `[40, 100]`** — the affine produces values in `[40, 100]` and the clamp is wider than the affine range. This is benign (clamp doesn't cut anything off) but the clamp now plays no active role on this joint; future operators tightening `clamp` should know it's currently a no-op for this axis.

## Alternatives Considered

- **Push `thumb_rota2 output_range[0]` from 40 → 60 or 75°** (hypothesis (a) alone): would have rotated the thumb tip further but not moved the base; per the round-4 commit message, "opposition rotation alone doesn't reposition the base; the thumb tip rotates but the base still sticks out." Rejected because the symptom (base on side) and the proposed fix (rotate the tip more) didn't match mechanically.
- **Add a fixed `offset: +40` to `thumb_bend`** instead of `input_range`/`output_range`: equivalent at rest (commands `+40°` at UDCAP=0) but misaligned at curl (would command `+100°` at curl=60 vs. M7 `+60°` — a +40° shift, identical to round-4 outcome). Functionally equivalent. Rejected because `input_range` / `output_range` keeps all thumb-axis configuration in the same schema as the four fingers (ADR-048), with `offset` reserved for the original M7 semantic (per-joint sign-corrected zero shift).
- **Bias via `weights`** (negative weight on `l0/l1/l2`): would invert the relationship, not bias-shift it. Rejected — wrong mechanism.
- **Wait for M8c thumb prototype A/B comparison** before changing `thumb_bend`: would have produced offline plots but couldn't have surfaced "thumb base on side" — that's a 3D physical observation. Round-4 visual verify on PC2 is the right venue. The prototype scripts remain in-tree (ADR-049) for any future thumb session.
- **Add a UI / config knob for "neutral thumb pose"** (rest, opposition, extended) with mode switches: out of scope. M8 has one operating mode (cup grasp); modes are post-M9.

## References

- Commit `b28bac4` (M8b round 4) — full commit message states hypothesis (a) vs (b) + the additive interpretation
- `config.yaml` lines 105-138 (left), 158-167 (right) — thumb three-joint configuration after round 4
- Commit `064801a` (M8b round 3) — the round whose visual failure surfaced this hypothesis
- `URDF/XHAND 1/XHAND1_URDF_ver 1.3/xhand1_*/urdf/xhand_*.urdf` — `thumb_bend` URDF max = 104.97°
- `docs/logs/m8a-calibrate-left-2026-05-21.log` + `m8a-calibrate-right-2026-05-21.log` — UDCAP `l0/l1/l2` near-zero at rest confirmed empirically
- ADR-013 — thumb roll source is non-contiguous (l3 + l20); `thumb_rota2 ← l20` correctly identified, but ADR-013 didn't claim rota2 alone is the opposition mechanism
- ADR-048 — four-finger affine rescale schema (reused on `thumb_bend`)
- ADR-049 — algorithm A chosen for thumb retargeting; this ADR is the round-4 hypothesis update that closed the algorithm-A path
- ADR-053 — `use_new_retarget` master flag (round 4 commit verifies flag=false byte-identical preserved)
- ADR-054 — frozen M7 reference fixture (round 4 didn't touch `thumb_bend.offset / sign / clamp`, so frozen reference output is unchanged)
- ADR-055 — URDF-grounded retune methodology (round 4 applies the same URDF-first lookup for `thumb_bend`)
