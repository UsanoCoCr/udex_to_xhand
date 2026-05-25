# ADR-054: Frozen M7 Reference Fixture + Auto-Verify Shell Guard for Long-Term Flag-Gating Regression

## Context

ADR-030 established `tests/fixtures/mapper_baseline.json` as the committed authoritative oracle for `tests/test_mapper_snapshot.cpp` — Python and C++ outputs must match the fixture at ‖Δ‖∞ ≤ 1e-6 rad, with SHA-256 self-checks against `config.yaml` + `example.json` to catch silent config drift. ADR-037 added the discipline: any `config.yaml` schema change requires fixture regeneration in the same commit.

M8 broke ADR-030's "fixture is one frozen snapshot" assumption in a useful way: the *live* fixture should follow `mapping.use_new_retarget`. With flag=true the fixture is the new-algorithm output; with flag=false it's the M7 baseline output. Both states are valid and must be testable. But the M7 baseline output is exactly what ADR-053's flag-gating is supposed to preserve — if flag=false ever drifts off M7, the rollback gate is broken and we may not notice until the operator slams the flag during a problem and the hand still misbehaves.

We needed a guard that asserts "with flag=false, the regenerated fixture is byte-identical to M5b/M7" *without* depending on either:

- The live `mapper_baseline.json` (which after M8b/M8c is now the flag=true fixture, no longer M7),
- Hand-eyeballed numeric diffs (which would silently miss tiny FP changes that compound over time),
- A separate test target in CMake (which would need rebuild + cross-compile coordination between dev Mac and PC2).

Three options surfaced:

1. **Keep two live fixtures** (`mapper_baseline_flag_true.json` + `mapper_baseline_flag_false.json`), both regenerated on every config change. Pro: symmetric. Con: doubles the regen work, both can drift, the guard becomes "are they both correct?" which is no stronger than the original problem.
2. **One frozen reference** (`mapper_baseline_m7_frozen.json`) + one live fixture. Frozen is written once at M8a Step A.0 and never rewritten; live follows whatever `use_new_retarget` says. A shell script forces flag=false momentarily, regens, diffs against the frozen reference, restores state.
3. **Snapshot test asserts both states inline** (run two C++ test cases per session, one with flag=true config, one with flag=false). Pro: catches in CI. Con: requires running the binary in two configurations on every test invocation; coordinating with PC2's build cycle is awkward.

What landed (commit `2bbe998`, M8a Step A.0' in the plan):

## Decision

- **Commit `tests/fixtures/mapper_baseline_m7_frozen.json` as a one-time write.** It is a copy of the fixture state immediately after `use_new_retarget: false` plumbing landed (Step A.0 commit), when the schema had the new flag but no `input_range`/`output_range` fields populated yet — i.e. flag-gated cold-path reset is in effect, M7 path runs unmodified, output is byte-identical to M5b/M7. This file is **the ground truth** for "what flag=false should always produce."
- **Never rewrite the frozen file.** No script, no commit hook, no fixture-regen tool touches it. M9 / M10 / any future milestone that changes `config.yaml` schema must NOT update this file. If a legitimately load-bearing change (e.g. fixing a real bug in M7 path) forces M7 to produce different output, that's a separate decision requiring a new ADR + an explicit, deliberate re-freeze.
- **Write `scripts/verify_flag_false_byte_identical.sh`** as the regression guard, side-effect free:
  - Backs up `config.yaml` + `tests/fixtures/mapper_baseline.json` to `mktemp` files.
  - Installs a `trap EXIT` that unconditionally restores both. The script cannot leave the working tree dirty even on Python error / Ctrl+C / kill.
  - Forces `mapping.use_new_retarget: false` in the live config via a Python `yaml.safe_load → safe_dump` round-trip (preserves comments + insertion order best-effort).
  - Regenerates the fixture into a *scratch* mktemp file (`$REGEN_FIXTURE`) — does NOT overwrite the live fixture.
  - Diffs the `left` and `right` floating-point arrays (not the `_rad`-suffixed names the plan §3 A.0' snippet imagined — the actual fixture schema is bare `left` / `right`; the script's header docstring documents this discrepancy explicitly).
  - Ignores `generated_at` / `config_yaml_sha256` / `example_json_sha256` / `python_version` (these always differ between regens).
  - Exit code 0 + stdout `"flag=false byte-identical to M5b/M7 baseline (OK)"` → guard passes.
  - Exit code 1 + stderr error message → guard fails: flag-gating is broken, do not push.
- **Make the guard runnable on demand, not in CI.** Operator runs it manually (a) at the end of every config-editing commit, (b) at the start of every M-future milestone, (c) whenever the Path A runbook (Phase 3c) prompts it. No GitHub Actions, no pre-commit hook — the project doesn't have CI infrastructure (single deploy target, internal tool, two-person team). The discipline is documented in plan §4.3 + runbook Phase 3c + this ADR.
- **Plan §3 A.0' snippet had a known error**: it referenced `left_rad` / `right_rad` as the diff fields. The actual fixture schema (per `scripts/dump_mapper_baseline.py:77-78`) is bare `left` / `right`. The implemented script uses the real schema and documents the discrepancy in its header comment. ADR-030's SHA-256 self-check + the script's diff together cover both axes (schema drift and numeric drift).

## Consequences

- **Positive — flag-gating regression is one command from any state.** `bash scripts/verify_flag_false_byte_identical.sh` returns in < 0.5s with a definitive answer. Used at M8b round 1, round 2, round 3, round 4 (per commit messages) — passed all four times.
- **Positive — the frozen file is a permanent project artifact.** Even after M9 reads sensors / publishes ROS topics / changes the binary's lifecycle, `mapper_baseline_m7_frozen.json` continues to assert "the deg→rad mapping under flag=false is the M5b/M7 baseline." Long-term protection.
- **Positive — side-effect-free script can be run without git stash.** `trap EXIT` restores both files unconditionally. Operator can run it while in the middle of editing config without losing work.
- **Positive — scratch fixture write (`$REGEN_FIXTURE` mktemp) means the live `mapper_baseline.json` is never momentarily wrong.** A different team member running `test_mapper_snapshot` while the verify script is running sees consistent state.
- **Negative — if the operator forgets to run the guard before pushing**, a flag-gating regression can land on `origin/main`. Mitigation: every commit message in M8b/c documents the verify-script result, so the absence of "verify passed" in a commit message is an immediate flag during review. Future work could add a pre-commit hook in `.git/hooks/pre-commit` (project-local), but this is operator-installed not committed — out of scope.
- **Negative — Python `yaml.safe_dump` round-trip does not perfectly preserve comments.** If the operator edits config.yaml with extensive inline comments (M8b/c does — the operator added rich rationale comments in `config.yaml` rounds 2–4), the trap-restore is the only thing preserving them; if the script crashes outside the `trap EXIT` window (between `cp` and `trap` install) some history could be lost. In practice, the trap is set at line 33 immediately after the mktemp creation at lines 30-32, so the unprotected window is microscopic.
- **Negative — frozen reference is brittle to future legitimate M7-path changes.** If a real bug is found in M7's deg→rad path (e.g. a sign flip in `joint_mapper.cpp::apply_one` that turns out to have been wrong since M2), fixing it would correctly change flag=false output, and the verify script would correctly fail. Resolution would be: write a new ADR explaining the fix, regenerate the frozen reference, document the deliberate re-freeze. Not a silent decision.

## Alternatives Considered

- **Two live fixtures (flag=true + flag=false)**: rejected per Context. Doubles regen work; both drift independently; doesn't strengthen the guard.
- **Two CMake test targets** (snapshot-true + snapshot-false): rejected. Requires two binary configurations or runtime flag flip + retest in the same C++ process; adds build-system complexity for a guard that's better expressed as a one-shot script.
- **Pre-commit hook** instead of manual run: rejected for scope. Operator-installed hooks are not committed to the repo; project-committed hooks via `core.hooksPath = .githooks/` is feasible but introduces a new install step + an enforcement vector that bypasses-on-failure may need a `--no-verify` escape hatch (CLAUDE.md prohibits this). Manual + commit-message-discipline is good enough at current team size.
- **Hash the frozen file's `left` + `right` arrays once and embed the hash inline in `test_mapper_snapshot.cpp` for assertion**: rejected. Tightly couples the C++ test to the frozen file's exact contents; any future fixture-format change (e.g. adding a new metadata field) would require updating the embedded hash. The JSON-array-diff approach is schema-resilient.
- **Just trust ADR-030's existing SHA-256 self-check to catch drift**: insufficient. ADR-030 catches *config* drift (does the fixture's recorded `config_yaml_sha256` match the current `config.yaml`?) and *example* drift, but it does NOT catch flag-gating regressions — if a future commit accidentally makes flag=false produce different output (e.g. by changing `apply_one` arithmetic that the `if (!use_new_retarget) reset()` should have protected against), the SHA-256 check still passes because both config and fixture were updated coherently in the same commit. The frozen reference + verify script catches exactly this case.

## References

- `tests/fixtures/mapper_baseline_m7_frozen.json` — the one-time-written reference (committed at A.0 commit `2bbe998`, never rewritten since)
- `scripts/verify_flag_false_byte_identical.sh` — the guard (89 lines including comments)
- `scripts/dump_mapper_baseline.py:77-78` — fixture schema canonical source (bare `left` / `right` keys)
- Commit `2bbe998` (M8a/M8b: ... flag-gating guard) — A.0 + A.0' co-landing
- `docs/plans/20260521-m8-tuning-acceptance-plan.md` §3 M8a Step A.0 + A.0' — design steps + the `left_rad` typo
- `docs/plans/20260521-m8-path-a-runbook.md` Phase 3c — operator's per-edit verify checkpoint
- Commits `8262593` / `472da5a` / `064801a` / `b28bac4` — every M8b round's commit message confirming verify-script-pass
- ADR-030 — committed-fixture-with-SHA snapshot policy (the foundation this ADR sits on)
- ADR-031 — `legacy_python/joint_mapper.py` as live Python oracle (the regen-er that this script invokes via `dump_mapper_baseline.py`)
- ADR-037 — fixture regen on schema change (paired discipline)
- ADR-053 — `use_new_retarget` master flag (the mechanism this script verifies)
- ADR-048 — affine rescale schema (the change that necessitated this guard)
