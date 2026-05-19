# ADR-037: Snapshot Fixture Must Be Regenerated on Any `config.yaml` Schema or Data Change

## Context

`tests/fixtures/mapper_baseline.json` records, alongside the 24 expected joint values, the SHA-256 of both `example.json` and `config.yaml`. ADR-030 chose this self-check explicitly to "catch silent drift": if either source file changes after the fixture was generated, the C++ snapshot test (`tests/test_mapper_snapshot.cpp`) exits 1 with a clear `[ERROR] config.yaml SHA-256 mismatch` and a pointer at `scripts/dump_mapper_baseline.py`.

M6 added the single line `startup_timeout_s: 10` to `config.yaml`'s `udcap:` section (plan §1.2(c)). That edit changed the file's SHA-256 from `7ca216eb…` to `63e4ae82…` while leaving every mapper-relevant field untouched. On the PC2 P0 build (2026-05-19), `./test_mapper_snapshot` failed at the SHA-256 check exactly as ADR-030 designed. The 24 joint values would still have passed bit-for-bit if the test had been allowed to run.

M5b's plan never spelled out a regen-and-commit step for fixture-affecting source edits because M5b shipped the fixture itself; the M6 plan inherited that gap and didn't budget the step either. Future milestones with even a minor `config.yaml` edit (M7 right-hand sign tuning, M8 thumb retargeting parameters, low-pass filter coefficients) will trip the same wire.

## Decision

**Any commit that modifies `config.yaml` or `example.json` MUST also regenerate `tests/fixtures/mapper_baseline.json` and commit the updated fixture in the same change.** The regen command is fixed:

```
python3 scripts/dump_mapper_baseline.py \
    --example example.json \
    --config  config.yaml \
    --out     tests/fixtures/mapper_baseline.json
```

The script imports `legacy_python/joint_mapper.py` (still the Python M4 oracle per ADR-031) and writes the JSON with refreshed SHA-256 entries. If the underlying mapping math is unchanged, the diff is restricted to the `config_yaml_sha256` / `example_json_sha256` / `generated_at` fields and the 24 joint arrays are byte-identical.

Future plans for M7/M8 (and any later milestone that touches these two source files) must include "regen + verify snapshot test" as an explicit P0 step alongside `make -j$(nproc)`.

## Consequences

- **正面**: The SHA-256 self-check from ADR-030 stays the authoritative drift detector; we don't weaken it.
- **正面**: One mechanical command per source-file edit. Trivial overhead.
- **正面**: Every fixture regen is committed alongside the source change, so `git log -- tests/fixtures/mapper_baseline.json` is a complete history of what the snapshot is anchored on.
- **正面**: M6 itself proved the wire works — the failure on PC2 was caught at build time, not after a regression in production.
- **负面**: Forgetting the regen step blocks the next build at P0. Mitigated by ADR-030's clear error message + this ADR's explicit policy.
- **负面**: `legacy_python/joint_mapper.py` keeps its live oracle role indefinitely. Already documented in ADR-031; this ADR makes the dependency more visible.

## Alternatives Considered

- **Drop the SHA check (back to value-only comparison)**: Rejected. ADR-030's whole point is to catch the "config drift but mapping math accidentally still close enough" failure mode that a tolerance-only check would mask.
- **Hash only the `mapping:` subtree of `config.yaml`**: Rejected. Adds a YAML-parsing path into the test binary; existing `nlohmann_json` + flat-file SHA path is simpler and the cost is one regen per genuine edit.
- **Auto-regenerate as a pre-commit hook**: Rejected for this milestone. Hooks aren't shared in-repo (CLAUDE.md "NEVER update the git config"); enforcement via plan-level checklist + ADR-030 build-time fail is enough.
- **Regenerate the fixture only on M-acceptance commits, not on every WIP commit**: Rejected. Then any intermediate WIP that pulls into a build dir without a parallel fixture refresh fails P0. Couple the regen to the source edit, not to milestone boundaries.

## References

- ADR-030: snapshot fixture as committed JSON with SHA-256 self-check (the detection mechanism)
- ADR-031: `legacy_python/` reorg during M5b (`joint_mapper.py` retained as oracle)
- `scripts/dump_mapper_baseline.py` (the regen tool, with usage in module docstring)
- `tests/test_mapper_snapshot.cpp:154-178` (the SHA check that fires the error)
- M6 plan §8.4 (deviation entry for the SHA-mismatch hit on PC2 P0)
