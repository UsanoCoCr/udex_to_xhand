# ADR-049: Thumb Retargeting — Algorithm A (Per-Joint Affine + Offset) Selected via Path A Iterative Tuning; Prototype B/C Deferred

## Context

Roadmap revision #6 (2026-05-19) flagged that the thumb side could not reuse the four-finger mapping path. XHand's thumb zero-pose points laterally — roughly orthogonal to the four-finger palm — and is not coaligned with UDCAP's thumb zero. Per-joint weighted-sum + sign + offset (the M2/M4/M7 path) is effectively a copy-rotation; for the thumb this produces a tip that swings but a base that stays on the palm-side, never opposing the four fingers. Cup acceptance (CLAUDE.md "Acceptance = pick up a cup") needs the thumb to oppose; therefore the thumb needs a retarget that's *aware* of the coordinate-frame mismatch.

Plan §0 in-scope #2 + §3 M8c proposed three candidate algorithms and a Python prototype workflow:

- **A. Per-joint affine + offset** — reuse the M8b four-finger schema (`input_range` + `output_range` + existing `offset`) on the three thumb joints. No new mapper code.
- **B. Coupled affine** — `thumb_rota1` and `thumb_rota2` take signed weighted sums of `{l3, l20}`; current schema supports negative weights, so equivalent to a linear rotation between UDCAP and XHand thumb frames. No new mapper code.
- **C. Tip-direction reprojection** — build a 3×3 rotation matrix from UDCAP `(l2, l3, l20)` Euler angles, extract thumb-tip direction, IK-solve to XHand `(bend, rota1, rota2)` against the URDF. New code, new scipy dependency, new `dedicated_thumb_pipeline` flag.

Plan §0 proposed "A → B → C gradient escalation" and reserved `scripts/thumb_retarget_prototype.py` + `scripts/record_udcap_thumb_sequences.py` (M8c Step C.1 + C.3) to compare A/B/C qualitatively on a 60s UDCAP thumb recording before committing to a final algorithm.

What actually happened in Path A:

- **Round 1** (commit `8262593`): A applied with `thumb_rota2 output_range [15, 50]` (mild palm bias) and `thumb_bend` / `thumb_rota1` left at M7 baseline. PC2 visual: four fingers still under-closed, thumb still on the outside of the hand.
- **Round 2** (commit `472da5a`): A with `thumb_rota2` bias `15 → 30°` and four-finger `output_range[1]` pushed to clamp `110°`. PC2 visual: four fingers closer, thumb still on the outside. Operator: "thumb 非常明显 still on the outside".
- **Round 3** (commit `064801a`): URDF lookup revealed `thumb_rota2` mechanical max = 89.95° (M7 clamp was `[0, 50]` = 56% of range, well below URDF — ADR-055). Lifted `thumb_rota2 clamp` to `[0, 90]` + `output_range [40, 90]`. PC2 visual: thumb still on side, no obvious change between rounds 2 and 3.
- **Round 4** (commit `b28bac4`): hypothesis update — the visible "thumb on side" is not (only) a `thumb_rota2` issue. At rest, UDCAP `l0=l1=l2 ≈ 0` → weighted-sum 0 → `thumb_bend` command 0° → thumb base extends straight out laterally. `thumb_rota2` rotates only the tip about an already-extended base, can't bring the base toward the palm. Fix: `thumb_bend input_range [0, 60]` + `output_range [40, 100]`, so the bend axis is commanded to 40° even at UDCAP zero. PC2 visual: thumb position acceptable (operator: "从视觉上没有太大问题了").

The Python prototype (commit `b31abf6`) was written and committed, but it was never run on a recorded UDCAP sequence to choose A/B/C. Algorithm A was reached via PC2 iterative visual feedback (rounds 1–4) directly. The prototype + recorder stay in the repo for future re-investigation, but no A/B/C comparison plot was needed to converge.

## Decision

- **Thumb algorithm A is the chosen retargeting form**: per-joint affine `input_range` + `output_range`, reusing the M8b four-finger schema (ADR-048). No new mapper code, no new dependency, no thumb-dedicated pipeline.
- **Three thumb joints get explicit configuration**:
  - `thumb_bend`: `input_range [0, 60]` + `output_range [40, 100]` — +40° rest bias to swing the base toward the palm (per round-4 hypothesis, ADR-056).
  - `thumb_rota2`: `input_range [-1.0, 41.0]` (from calibration: UDCAP l20 max ≈ 41°) + `output_range [40, 90]` + `clamp [0, 90]` (URDF-lifted, ADR-055) — opposes the four fingers about the MCP-Roll axis.
  - `thumb_rota1`: untouched from M7 baseline. UDCAP `l3` input is narrow and clamps quickly; no clear lever during rounds 1–4.
- **Gated by `mapping.use_new_retarget`** (ADR-053). Flag=false → thumb takes the M7 weighted-sum path, byte-identical to `mapper_baseline_m7_frozen.json` (ADR-054).
- **Algorithm B is unused but not removed.** `scripts/thumb_retarget_prototype.py` keeps the B implementation (signed weighted sum of `{l3, l20}`); if A turns out to fail under cup-acceptance (M8e, deferred), the prototype is the next investigation step before committing to algorithm C.
- **Algorithm C is explicitly out of scope.** No URDF/scipy integration, no `dedicated_thumb_pipeline` flag. Plan §0 reserved a "M8c-extended" milestone for C — that reservation stays, but Path A consumed it via A.
- **Path A iteration (PC2 visual feedback) is the decision-making channel for M8.** The plan's prototype-first / data-driven A/B/C selection assumed offline plotting could distinguish A from B before hardware. In practice, the visual was the discriminating signal: only the live XHand on PC2 surfaces "thumb on side" as a base-position vs. tip-rotation issue, which is not visible from a per-joint output plot.

## Consequences

- **Positive — no code change for thumb retargeting.** Same `JointConfig` schema, same `apply_one` path. Mapper hot path is identical regardless of whether the configured joint is a finger or a thumb axis. M8 ships zero new mapper logic for thumbs beyond what M8b (ADR-048) already added.
- **Positive — kept the Python prototype + recorder in-tree as future tools.** `scripts/record_udcap_thumb_sequences.py` + `scripts/thumb_retarget_prototype.py` are zero-dependency stdlib + matplotlib; they can replay any future thumb-related session offline without rebuilding C++. If cup acceptance fails under algorithm A, B is one CLI flag away.
- **Positive — decision audit trail is the commit graph.** Rounds 1 → 4 each commit-message documents the hypothesis, the change, the verify-script result, and the if-this-fails-then-X playbook. Future re-tuning starts from this trail, not from a separate decision doc.
- **Negative — algorithm A is local-optimum proven for "looks acceptable", not for cup acceptance.** M8e (5-of-5 grasp test) was deferred per user decision. If under cup, algorithm A produces insufficient opposition force or pinch alignment, the next round needs either (a) further A-axis tuning (push `thumb_bend` bias to 50–60°, or open `thumb_rota1`) or (b) escalate to B.
- **Negative — Path A is operator-effort-intensive.** Four rounds × per-hand visual verify + hypothesis-update commit-write. The plan's prototype-first path would have done the A/B comparison once, offline. Iteration is cheaper per round but bounded only by operator patience; without a stop criterion harder than visual judgment, it could drift indefinitely. M8 stopped at "looks acceptable" by user call; a stricter acceptance (M8e cup test) is the next gate.
- **Negative — choosing A leaves the coordinate-frame mismatch unmodeled.** Algorithm A doesn't *know* that XHand thumb zero ≠ UDCAP thumb zero — it just applies an affine remap that *happens to* compensate when calibrated empirically. If a different operator's UDCAP fit shifts the bias requirements, A needs re-tuning; B's signed-weight form would absorb that more gracefully (the rotation is in the weights, not in per-joint offset). Live with this until cup acceptance forces the issue.

## Alternatives Considered

- **Algorithm B (coupled affine, signed weights of `{l3, l20}`)**: rejected for M8 — A was visually sufficient at round 4. The prototype is wired up; if A fails under cup, B is the next step before C.
- **Algorithm C (tip-direction IK reprojection)**: rejected as plan §0 stated — needs URDF integration + scipy + new pipeline branch. Out of scope unless A and B both fail. Reopens "M8c-extended".
- **Stop after round 3 and revert to algorithm B**: tempting after round 3 showed no thumb improvement, but round-3 commit message correctly predicted that `thumb_bend` not yet touched was the missing axis (ADR-056). Continuing to round 4 with A was the right call.
- **Run the Python prototype on a recorded session before round 2**: would have produced offline A/B comparison plots; might have selected B earlier. Skipped because round-1 visual was usable enough to keep iterating in real-time, and the prototype required a 60s UDCAP recording trip that the operator did not schedule.
- **Bake the thumb-bend rest bias directly into `offset` (instead of `input_range/output_range`)**: would also accomplish the +40° rest bias and keep `output_range` available for span control. Rejected because we want the affine path to express both rest bias and full-curl mapping in one place (`output_range[0]` = rest, `output_range[1]` = full curl) — easier to retune.

## References

- `config.yaml` (lines 105-138 L, 158-167 R) — thumb three-joint configuration after round 4
- `scripts/thumb_retarget_prototype.py` — A/B/C prototype, kept for future re-investigation
- `scripts/record_udcap_thumb_sequences.py` — UDCAP recorder, kept
- Commit `8262593` — round 1: minimal `thumb_rota2 [15, 50]` palm bias
- Commit `472da5a` — round 2: push four-finger output to clamp + `thumb_rota2` bias 15→30
- Commit `064801a` — round 3: URDF lookup → `thumb_rota2 clamp [0, 90]` + `output [40, 90]`; four-finger `input_range` tightened
- Commit `b28bac4` — round 4: `thumb_bend` rest bias hypothesis test → success per visual
- ADR-013 — thumb roll is non-contiguous (l3 + l20); confirmed `thumb_rota2 ← l20` source choice
- ADR-048 — four-finger affine rescale schema (reused for thumb)
- ADR-053 — `use_new_retarget` master flag
- ADR-055 — URDF as mechanical-limit ground truth (round-3 reversal)
- ADR-056 — `thumb_bend` rest bias as the swing-toward-palm mechanism (round-4 hypothesis update)
- `docs/plans/20260521-m8-tuning-acceptance-plan.md` §0 in-scope #2 + §3 M8c — original A/B/C ladder + prototype-first decision path
- `docs/plans/20260521-m8-path-a-runbook.md` — operator-runnable iterative tuning loop that replaced the prototype-first path
