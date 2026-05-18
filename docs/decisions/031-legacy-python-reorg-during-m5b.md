# ADR-031: `legacy_python/` reorg performed during M5b (user-directed) rather than post-M5c

Date: 2026-05-18
Status: Accepted
Milestone: M5b

## Context

Plan §1.3 (original draft) stated:

> `main.py`, `udcap_receiver.py`, `joint_mapper.py`, `xhand_driver.py`, `safety.py`, `test_udcap_connection.py` **stay in repo root**. Removal becomes a candidate task after M5c successfully re-runs M3+M4 acceptance on hardware. Tracked, not done in M5b.

The rationale was conservative: keep things at root during the rewrite window, defer any reorg until the C++ binary had been hardware-validated end-to-end (M5c).

After M5b code landed (commit `a06cfd2`, 2026-05-18), the repo root had become crowded:
- 6 legacy `.py` files at the same level as new `CMakeLists.txt`
- new `src/` directory
- `config.yaml`, `example.json` (shared data)
- `SPEC.md`, `CLAUDE.md` (docs)
- new `tests/`, existing `scripts/`, vendor `xhand_control_sdk/`, `xhand_control_ros2/`

The operator observed this and requested the move mid-M5b (conversation 2026-05-18: "请你为我打包一下这些代码到一个文件夹里，让项目结构看起来更好一些").

## Decision

Move all 6 legacy Python files into a new `legacy_python/` directory **during the M5b commit** (not post-M5c):

```
legacy_python/
├── README.md                       # NEW — rationale + run instructions + removal criterion
├── main.py                         # reference only; zero runtime callers
├── udcap_receiver.py               # reference only
├── joint_mapper.py                 # ★ STILL LIVE — snapshot oracle for tests/test_mapper_snapshot.cpp
├── xhand_driver.py                 # reference only (would fail at `import xhand_controller` on aarch64)
├── safety.py                       # reference only
└── test_udcap_connection.py        # reference only — M0-era one-shot UDP sniffer
```

Moves performed via `git mv` to preserve rename history. Git similarity detection landed all 6 files at 93–100% match.

Three small in-file edits applied to preserve standalone runnability from repo root:

1. `legacy_python/udcap_receiver.py`: `os.path.join(os.path.dirname(__file__), "..", "example.json")` — one level up.
2. `legacy_python/joint_mapper.py` `__main__` block: same `".."` pattern for both `example.json` and `config.yaml`.
3. `scripts/dump_mapper_baseline.py`: `sys.path.insert(0, os.path.join(REPO_ROOT, "legacy_python"))` so `from joint_mapper import …` still resolves.

`CLAUDE.md` "Migration status" + `SPEC.md` §8 file tree + `SPEC.md` §13 reference materials + plan §1.3 + plan §1.1 all updated in the same commit. `legacy_python/README.md` added explaining: what's here, why retained, how to run if needed, when eligible for deletion.

Full removal of `legacy_python/` remains a post-M5c candidate task — the reorg doesn't lock anything in. `joint_mapper.py` will be the last to leave (it remains the snapshot oracle until parity testing is no longer needed, which is "never" until the test itself is retired).

## Consequences

**正面**
- Repo root is uncluttered. After M5b commit, root contains only first-class artifacts: `CMakeLists.txt`, `config.yaml`, `example.json`, `SPEC.md`, `CLAUDE.md`, and canonical directories (`src/`, `tests/`, `scripts/`, `docs/`, `xhand_control_sdk/`, `legacy_python/`, plus untracked vendor reference trees).
- `legacy_python/` signals intent at a glance: "this is reference, not the system." Stronger than orphan `.py` files at root.
- Snapshot oracle still works — `dump_mapper_baseline.py`'s `sys.path` adjustment is one line; verified by M5b §6.6 passing (max diff 0.0e+00 / 1.4e-17 rad, well under 1e-6 tolerance).
- Removal post-M5c remains possible without affecting anything else (the folder is self-contained).
- M5b commit captures the *final* M5b state in one diff. Reviewers don't have to parse "M5b code at root → reorg later" as two separate operations.

**负面 / 风险**
- ADRs 001-022 reference the Python files by bare name (e.g., "joint_mapper.py:80", "udcap_receiver.py:46-54"). After the move, those references are technically at `legacy_python/joint_mapper.py:80` etc. ADRs are historical records — readers may need to know about this move to find the cited lines. Mitigated: the move is recorded here (ADR-031), in plan §1.3 update, in CLAUDE.md / SPEC.md, and prominently in `legacy_python/README.md`.
- Small extra churn in the M5b commit (6 renames + 3 content tweaks + README.md). Acceptable; the alternative is a separate post-M5c "cleanup" commit which fragments the M5b state across two commits.
- Operator running `python xhand_driver.py` from inside `legacy_python/` will fail at `import xhand_controller` — but that was already true at root for aarch64. `legacy_python/README.md` flags this explicitly.

## Alternatives

1. **Defer reorg per original plan §1.3 (leave at root, move post-M5c)** — Rejected. Operator explicitly overrode mid-M5b. Doing the move now means M5b's commit captures the final state; cleaner project history. Cost of doing it now is small (~10 file ops); cost of doing it later is a second commit with motivation that's harder to explain in isolation.
2. **Delete the Python files entirely in M5b** — Rejected. `joint_mapper.py` is the live snapshot oracle for `tests/test_mapper_snapshot.cpp` — deleting it would force regenerating the fixture from a different source (e.g., handwritten expected values in C++ source), defeating the parity claim of ADR-030. Other files retained for ADR-grounding (ADRs 001-022 cite them) and reviewer parity diff.
3. **Move only the non-oracle files** (keep `joint_mapper.py` at root, move the other 5) — Rejected. Splits the prototype across two locations; confusing. Better to keep all 6 together and update `sys.path` in one place (`scripts/dump_mapper_baseline.py`).
4. **Move to `prototype/` or `deprecated/` or `python/`** — Rejected on naming. `legacy_python/` is the most direct (matches "Legacy Python prototype" language already in CLAUDE.md "Migration status" and SPEC.md §8) and pre-emptively scopes for a hypothetical future `legacy_cpp/` should the C++ runtime itself ever be superseded.
