# ADR-021: Two-Layer Clamping — Degree in Mapper + Radian Hard Limits in Main

## Context

ADR-020 places the primary per-joint clamp inside the mapper (degree domain). The question is whether `main.py` should also clamp before calling `driver.send()`. CLAUDE.md requires: "Clamp all joint positions to per-joint [min, max] before send_command."

If the mapper has a bug (e.g., wrong offset, missing clamp, source index error), unclamped radian values could reach the XHand hardware.

## Decision

Keep a secondary clamp in `main.py` using `HARD_LIMITS_RAD` from `safety.py`. This uses the XHand's overall physical range (-90° to 110°, converted to radians) as a uniform catch-all for all 12 joints. Under normal operation this layer never activates — it exists solely as a safety net.

## Consequences

- **正面**: Defense-in-depth — a mapper bug cannot send values outside XHand physical range to hardware
- **正面**: Satisfies CLAUDE.md safety requirement ("clamp before send_command")
- **正面**: Hard limits are simple and uniform — no config dependency, no per-joint complexity in the safety layer
- **负面**: Two clamp calls per frame (24 comparisons total) — negligible at 100Hz but adds a code path that should never activate, making it hard to verify via testing

## Alternatives

- **Single clamp in mapper only**: Simpler, but violates defense-in-depth principle. A mapper bug directly reaches hardware.
- **Single clamp in main.py with per-joint radian limits**: Moves all clamping outside mapper, but then mapper output is unconstrained — see ADR-020 for why degree-domain clamping is preferred.
- **Per-joint hard limits in safety.py**: More precise than uniform range, but requires safety.py to parse the mapping config, creating coupling. The mapper already handles per-joint precision.
