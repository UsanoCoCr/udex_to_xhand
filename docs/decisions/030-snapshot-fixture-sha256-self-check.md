# ADR-030: Snapshot fixture as committed JSON with SHA-256 self-check

Date: 2026-05-18
Status: Accepted
Milestone: M5b

## Context

Plan §6.6 / §7 DoD #4 require M5b to prove the C++ joint mapper reproduces the Python M4 baseline. Without such evidence, M5b would ship on "the C++ compiles" alone — too weak for a milestone whose entire point is algorithmic parity.

The Python prototype (`legacy_python/joint_mapper.py`) is the only validated implementation of the 24→12 joint-mapping math (ADRs 020-022). Reproducing it in C++ without an equivalence test invites silent semantic drift between revisions.

Three design axes to settle:

1. **Where does the Python baseline live?** Hard-coded numbers in C++ source vs file-based fixture vs Python-on-PC2-each-run.
2. **How do we know the fixture is still valid for the current `example.json` + `config.yaml`?** Inputs can change without anyone remembering to regenerate the fixture.
3. **Where does the test run?** PC2 (no Python at test time) vs Mac (can use Python but doesn't match deployment target).

## Decision

Adopt a **committed JSON fixture** with embedded SHA-256 of source inputs:

1. `scripts/dump_mapper_baseline.py` runs **once on dev Mac** (or any host with Python + pyyaml). It imports `legacy_python.joint_mapper.JointMapper`, runs `map("left", …)` + `map("right", …)` on the parsed `example.json`, and writes `tests/fixtures/mapper_baseline.json`:

   ```json
   {
     "source": {
       "example_json_sha256": "<hex>",
       "config_yaml_sha256":  "<hex>",
       "python_version":      "3.x.y",
       "generated_at":        "2026-05-18T...",
       "joint_order":         ["thumb_bend", "thumb_rota1", …]
     },
     "left":  [12 floats in radians],
     "right": [12 floats in radians]
   }
   ```

2. **The fixture is committed** to the repo (not regenerated per test).

3. `tests/test_mapper_snapshot.cpp` (built with `cmake -DBUILD_TESTS=ON`):
   - Loads the fixture.
   - Reads `example.json` + `config.yaml` from disk and computes their SHA-256 via the **OpenSSL EVP API** (`<openssl/evp.h>`). The legacy `SHA256()` from `<openssl/sha.h>` is deprecated in OpenSSL 3.0 (Ubuntu 22.04 ships 3.0.2); EVP works on both 1.1.1 and 3.0+ without warnings.
   - Asserts the recorded digests match the on-disk inputs. Mismatch → fail with the recorded hex, the on-disk hex, and a "regenerate via scripts/dump_mapper_baseline.py" hint.
   - Runs the C++ `JointMapper` on the same inputs.
   - Asserts `‖cpp - python‖∞ < 1e-6 rad` per joint, per hand.

## Consequences

**正面**
- PC2 needs no Python runtime to execute the test — pure C++ binary, links only OpenSSL + nlohmann_json + yaml-cpp. Matches the deployment target.
- Drift detection is loud: if anyone edits `example.json` or `config.yaml` without regenerating the fixture, the test fails with a clear actionable message.
- **M5b §6.6 result (2026-05-18)**: `L max |Δ| = 0.0e+00 rad`, `R max |Δ| = 1.4e-17 rad`, tolerance `1.0e-06`. L is bit-identical Python ↔ C++; R differs only by IEEE-754 rounding artifacts on the `0.6 * x + 0.4 * y` accumulator order — far below the joint encoder resolution and the 1e-6 tolerance. Strong evidence that the rewrite preserved the math.
- Fixture is ~2 KB; trivial to commit, version, and inspect.
- Acts as a *living* spec for what M0-M4 verified — any future C++ regression on the mapping math fires immediately on the next test run.

**负面 / 风险**
- Regenerating the fixture requires Python + pyyaml on the host running the regen (dev Mac in our case). Acceptable — regen happens only when `example.json` or `config.yaml` deliberately changes. Documented in plan §6.1.
- Couples M5b's correctness story to Python's correctness (transitive chain: M4 ADRs 020-022 → Python mapper → fixture → C++). If a bug was latent in the Python mapper, the C++ inherits it. Mitigated by the M3/M4 hardware validation already done in Python, and by M5c re-validating against actual hardware.
- The fixture covers only the *one* UDCAP frame in `example.json`. Edge cases (saturating clamps, all-zero input, max flexion across multiple joints) are not exercised. Acceptable for M5b's narrow parity claim; broader coverage is M5c or future property-based testing.

## Alternatives

1. **Run Python on PC2 each test invocation** — Rejected. Requires Python + pyyaml on PC2 (not currently a runtime dep). Slower (Python startup + import + yaml parse on every invocation). Harder to inspect "what changed?" — the canonical answer becomes runtime output, not committed data that can be diffed.
2. **Hardcode expected values as `constexpr` arrays in `test_mapper_snapshot.cpp`** — Rejected. Any edit to `example.json` or `config.yaml` would force a C++ source edit; high friction, and the input-hash check wouldn't be possible at all (the hashes need somewhere to live that is NOT the C++ source).
3. **Skip the parity test entirely** — Rejected. Plan §7 DoD item 4 is M5b's only direct algorithmic evidence. Without it, the milestone shipping criterion collapses to "the binary built", which doesn't prove the mapping math is correct.
4. **Property-based fuzzing** (random UDCAP inputs, run both Python + C++ in one harness, compare) — Considered. Doesn't fit M5b's 1-day scope; requires both Python and C++ reachable in one process. Worth revisiting in a future milestone but not at the expense of M5b's deterministic baseline.
5. **CRC32 / MD5 instead of SHA-256 for the source hashes** — Rejected. SHA-256 is already linked transitively via OpenSSL (libssl-dev is a vendor SDK dep — see CLAUDE.md "Commands"). Cryptographic strength isn't strictly needed but the marginal cost is zero, and SHA-256 hex digests are immediately recognizable to reviewers without having to explain the choice.
6. **Use the deprecated `SHA256()` from `<openssl/sha.h>` directly** — Rejected. Generates `-Wdeprecated-declarations` under OpenSSL 3.0+, which would break the "no warnings" criterion of plan §6.8 / §7 DoD #6. EVP API is the supported path and works identically on 1.1.1 and 3.0+.
