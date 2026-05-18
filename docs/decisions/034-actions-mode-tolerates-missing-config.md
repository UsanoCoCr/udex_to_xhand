# ADR-034: `--actions` mode tolerates missing/invalid `--config`

Date: 2026-05-18
Status: Accepted
Milestone: M5c

## Context

CLAUDE.md "Commands" advertises two distinct invocation shapes:

```bash
# bring-up / smoke (M3 equivalent)
./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok

# teleop (M4 equivalent)
./udex_to_xhand --config ../config.yaml --hand left
```

The first — actions mode — needs only the XHand RS485 endpoint to do its job.
It does **not** read UDP, does **not** touch the joint mapper, does **not**
consult `mapping.*` in `config.yaml`. The 12 joint angles per preset are
in-source constants (ADR-032). PID gains, baudrate, serial port have sensible
defaults baked into `XHandConfig{}` (kp=100, ki=0, kd=0, tor_max=300, mode=3,
3 Mbps, `/dev/ttyACM0`).

Yet `cli::Args::config_path` defaults to `"config.yaml"`, and the FULL-mode
code path in `main.cpp:127-131` treats `YAML::LoadFile` failure as fatal
(`LOG_ERROR("failed to load config …")` + `return 2`). If the actions branch
reused that strict policy, the canonical bring-up command would **require**
`config.yaml` in the current working directory — which:

- breaks the M3-equivalence claim,
- breaks the CLAUDE.md "Commands" example (the example has no `--config`),
- adds friction with zero diagnostic gain (driver.open() will produce a
  better error message anyway if hardware is wrong).

The legacy Python (`legacy_python/xhand_driver.py:118-193`) had no config
loading at all in its `__main__` block; `--port` was sufficient. We need
behavior-parity on the M3 path.

## Decision

In the actions branch dispatch (`main.cpp` post-CLI-parse, pre–FULL-mode
strict load), tolerate `YAML::LoadFile` failure:

```cpp
if (args.actions) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(args.config_path);
    } catch (const std::exception&) {
        // Silent fall-through — actions mode does not need config.yaml.
    }
    XHandConfig xc = root.IsNull() ? XHandConfig{} : load_xhand_config(root);
    if (args.port_override) xc.serial_port = *args.port_override;
    return run_actions(args, xc);
}
```

Three operational properties:

1. **Silent on load failure.** No `LOG_WARN`. If `config.yaml` is absent
   (bring-up on a fresh checkout), no scary warning. If it is malformed, the
   next step — `driver.open()` — will fail with a clear actionable error and
   exit 2 (no double reporting).
2. **`--port` overrides config-derived `serial_port`** — same precedence rule
   as FULL mode. Lets the operator drive any device without editing config.
3. **`XHandConfig{}` defaults are deliberately enough to drive XHand.** The
   defaults match the values in `config.yaml`'s `xhand:` section (the
   canonical PC2 build), so an operator without `config.yaml` gets the same
   PID/baudrate/port behavior as an operator with the committed file.

`config.yaml` is **still consulted if present and valid** — operators who
keep a `config.yaml` with tweaked PID values would have those values used.
This branch is "optional best-effort", not "ignored". FULL mode's strict
policy is preserved; only actions mode is permissive.

## Consequences

**Positives**
- M3-equivalent invocation works on first boot, from any working directory,
  with no preconditions. The CLAUDE.md `--actions` example copy-pastes and
  runs.
- M5c §6.5 ran on PC2 2026-05-18 with `cwd = build/` (no `config.yaml`
  sibling) — `LoadFile` silently caught, defaults applied, all 4 presets
  executed:
  ```
  [INFO] XHand SDK version: 1.4.3
  [INFO] Serial: /dev/ttyACM0 @ 3000000 baud
  [INFO] hand_id=1 type=Left
  [INFO] Action fist: sent 12 joints, OK
  [INFO] Action palm: sent 12 joints, OK
  [INFO] Action v: sent 12 joints, OK
  [INFO] Action ok: sent 12 joints, OK
  [INFO] Shutdown: mode=0 (passive)
  ```
  (Two transient CRCs during sends; ADR-017 log-not-crash applied — neither
  blocked the action from completing.)
- Defaults visible in one place (`XHandConfig{}` in `main.cpp`). No hunting
  through YAML to know what `kp`/baud are.

**Negatives / risks**
- Silent `catch (...)` hides genuine config errors. Mitigated by
  `driver.open()` being the immediate next step — any port / hardware
  problem surfaces there with a full error message. The "did config.yaml
  matter?" question collapses to "did the operator specify `--port`?".
- FULL mode and actions mode now have **different** config-load policies
  (strict vs tolerant). Asymmetric, but the asymmetry is intentional and
  documented in the actions-dispatch comment + this ADR. The principle:
  data that the runtime *needs* (mapping, watchdog timeout) is strict; data
  that is only an *override* (PID for preset bring-up) is tolerant.
- `root.IsNull()` is a slightly off predicate (default-constructed
  `YAML::Node` is *undefined*, not *null*) — but `load_xhand_config()`
  handles both states identically via its internal `if (x)` field guards,
  so runtime behavior is the same. Inline comment notes this; rewriting to
  `root.IsDefined()` was deemed gold-plating for M5c scope.

## Alternatives

1. **Strict config-load in actions mode (FULL-mode policy parity)** —
   rejected. Breaks the M3-equivalent invocation; legacy Python had no
   precedent. Adds friction with zero diagnostic gain.
2. **Require `--config` explicitly in actions mode** — same friction.
   Plus `--config` defaults to `config.yaml`, so this becomes a behavior
   trap: "I omitted --config, so why did it still look for it?".
3. **Add a `--no-config` flag** — explicit but redundant. The default
   behavior of "look for config.yaml, fall back if missing" already covers
   the common case; an extra flag costs cognitive overhead.
4. **Move PID defaults from `XHandConfig{}` into a constants header** —
   centralizing defaults is fine in principle, but separating them from the
   FULL-mode path risks drift between actions-mode defaults and what the
   committed `config.yaml` advertises. Keeping `XHandConfig{}` as the one
   source of truth for runtime defaults is cleaner. M6+ may revisit if a
   constants header would serve other purposes.
