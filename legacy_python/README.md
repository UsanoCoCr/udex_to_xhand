# legacy_python/

M0–M4 Python prototype, moved here in M5b. **Not runtime code.** The authoritative implementation is `src/` (C++17 binary `udex_to_xhand`, built from top-level `CMakeLists.txt`).

## What's in here

| File                       | Post-M5b role                                                                                                                            |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `joint_mapper.py`          | **Still live** — snapshot oracle for `tests/test_mapper_snapshot.cpp`. Imported by `scripts/dump_mapper_baseline.py` to generate `tests/fixtures/mapper_baseline.json`. |
| `main.py`                  | Reference only. Zero runtime callers.                                                                                                    |
| `udcap_receiver.py`        | Reference only.                                                                                                                          |
| `xhand_driver.py`          | Reference only. Targeted the abandoned x86_64 Python wheel (`xhand_controller`); will fail at `import` on aarch64.                       |
| `safety.py`                | Reference only.                                                                                                                          |
| `test_udcap_connection.py` | Reference only. M0-era one-shot UDP sniffer that originally captured `example.json`.                                                     |

## Why we keep them

1. **Snapshot oracle**: `joint_mapper.py` is the ground truth for the C++ mapper's snapshot equivalence test. Deleting it would break `tests/test_mapper_snapshot.cpp`.
2. **Reviewer cross-reference**: ChatGPT / Gemini reviewers and humans cross-reference Python ↔ C++ to validate parity. Easier when both live in the tree.
3. **ADR groundedness**: ADRs 001–022 cite these files by name. Keeping the files alive keeps the ADRs from rotting into "what code?".

## How to run (if you really want to)

After the M5b move, the Python scripts assume `cwd = repo root`. Run them as:

```bash
# From the repo root, NOT from inside legacy_python/
python3 legacy_python/main.py --mock --duration 3
python3 legacy_python/udcap_receiver.py --port 9000 --duration 10
python3 legacy_python/joint_mapper.py
```

Internal `os.path.dirname(__file__)` math was adjusted to look one level up for
`example.json` and `config.yaml`, so these still work standalone.

`xhand_driver.py` will fail at `from xhand_controller import xhand_control` on
aarch64 — no Python wheel exists. That's expected; M5 pivoted to the C++ SDK
(see ADR-023, plan §M5 revision 2).

## When to delete

After M5c re-validates M3 + M4 acceptance on hardware against the C++ binary,
this folder becomes a deletion candidate. Tracked in plan §1.3 of
`docs/plans/20260518-m5b-cpp-rewrite-plan.md`.
