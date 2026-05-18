# ADR-032: Preset action table is C++ header-only with a Python byte-equality script

Date: 2026-05-18
Status: Accepted
Milestone: M5c

## Context

Plan §6.5 makes `./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok`
the C++ equivalent of M3 Python's `xhand_driver.py --actions ...`. Python
shipped 4 hard-coded preset poses in `legacy_python/xhand_driver.py:9-14`:
fist / palm / V / OK — each a 12-element array of degrees.

Where do those 48 numbers live in the C++ runtime?

1. **In-source C++ literals (committed to the repo)**: tightest deployment;
   risk = two source-of-truth files for the same data → silent drift on edits.
2. **External YAML/JSON/CSV side-car**: trivial to edit; risk = adds a file
   dep to the canonical bring-up command shape, which plan §6.5 requires to
   work with just `--port` + `--actions`.
3. **Generated header (`scripts/generate_preset_actions_h.py`)**: matches the
   M5b snapshot-fixture pattern (ADR-030). Adds a build-time Python dep.

## Decision

Adopt **option 1** with two reinforcements:

1. **`src/preset_actions.hpp` is header-only** —
   - `constexpr std::array<preset_actions::Preset, 4> kPresets` with 4 ×
     `{ const char* name, std::array<double, 12> deg }`.
   - `inline std::array<double, 12> deg_to_rad(...)` and
     `inline const Preset* find_preset(std::string_view)` defined in the
     same header. No `.cpp`. No CMake target added.
   - Includes `<array>`, `<cmath>` (for `M_PI`), `<string_view>`.

2. **Drift defense lives in plan §6.1's `[LOCAL]` checklist**, not in CMake/CI.
   The plan ships a Python regex script that
   - extracts the 4 `{"name", { 12 floats }}` blocks from the C++ header,
   - extracts the 4 `"name": [12 floats]` blocks from the Python file,
   - asserts pairwise `abs(py - cpp) < 1e-9` for all 48 numbers.

   The script is an operator-run pre-flight before the PC2 sync. It is **not**
   wired into the build — running it remains the operator's responsibility.
   M5c is internal-tool scale; a build-time gate would be overhead for one-off
   preset edits (presets are essentially frozen — they have not changed since
   M3, 2026-04-27).

## Consequences

**Positives**
- `--actions` mode runs without any file deps beyond the XHand serial endpoint;
  CLAUDE.md's `./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok`
  copy-pastes and works regardless of CWD / config.yaml location. M5c §6.5
  verified this from `build/`, which has no `config.yaml` sibling.
- `constexpr std::array` → compile-time literal, zero dynamic allocation,
  trivially diffable in PRs (per-line numeric edits show as 1-line changes).
- Byte-equality script catches the failure mode that motivated this decision:
  someone edits one side and forgets the other. Plan §6.1 ran 2026-05-18
  before the PC2 sync and reported `OK: PRESETS byte-identical between Python
  and C++`.
- Header-only means no second compilation unit, no link-order surprises, no
  CMake change to review.

**Negatives / risks**
- Two source-of-truth files for the same data is an anti-pattern in general.
  Mitigated by §6.1 being the documented pre-flight and by `legacy_python/`'s
  read-only status post-M5b (zero runtime callers; ADR-031 — only
  `joint_mapper.py` retains a live role as snapshot oracle, not
  `xhand_driver.py`). Practical drift surface is small.
- Python's `re.findall(r"-?[0-9]+\.[0-9]+", line)` requires a decimal point.
  Python source has plain integers (`40`, `110`, `109.1`). The C++ header was
  written with `.0` appended on every integer-valued entry (`40.0`, `110.0`,
  `109.10` …) so the regex captures all 12 per line. Future readers must not
  "clean up" the trailing `.0` suffixes — they are load-bearing for the §6.1
  check. Inline header comment notes this; this ADR is the durable record.
- The header is not regenerated; manual edits are the maintenance model. If
  presets ever change, the operator must update both files and re-run §6.1.

## Alternatives

1. **Hard-coded `constexpr` in a `.cpp`** — works, but forces a recompile,
   breaks the header-only claim, and gives no benefit over the chosen form.
2. **YAML/JSON side-car file (e.g., `presets.yaml`)** — adds file I/O + path
   resolution. Plan §6.5 wants `--actions` to work with just `--port`.
   YAML-parse failure surface added for zero gain (presets are immutable from
   the operator's perspective).
3. **Generate `preset_actions.hpp` from Python at build time** — clean
   single-source, but adds a build-time Python + pyyaml dep on PC2 (currently
   has zero Python deps for the runtime build). M5b deliberately ran the
   snapshot-fixture generator on dev Mac for exactly this reason (ADR-030).
4. **Move presets into `config.yaml`** — mixes "bring-up actions" (one-off
   smoke gestures) with "teleop config" (joint mapping). Operationally
   confusing: editing config.yaml for a joint sign-flip would risk perturbing
   fist/palm constants if someone YAML-formatted the file wrong.
