# ADR-016: Pre-Build HandCommand_t Template, Reuse Per Tick

## Context

The XHand SDK requires a `HandCommand_t` containing 12 `FingerCommand_t` structs, each with 7 fields (id, kp, ki, kd, position, tor_max, mode). In the 100Hz control loop, only the 12 `position` fields change per tick. Rebuilding the full struct every tick is wasteful.

## Decision

Create one `HandCommand_t` in `__init__` with PID params (kp, ki, kd, tor_max, mode) pre-filled from config. In `send()`, only update the 12 `.position` fields, then pass the same object to `send_command()`.

## Consequences

- **正面**: Minimal per-tick overhead — 12 float assignments instead of 84 field assignments (12 × 7)
- **正面**: PID params are set once from config, consistent across all sends
- **负面**: Single shared command object means `close()` mutates mode in-place. After `close()`, the template has mode=0. Not a problem since `self._device` is set to None after close.
- **负面**: If different hands need different PID params (e.g., left kp=100, right kp=120), one template won't suffice. Deferred to M6/M7 if needed.

## Alternatives

- **Build fresh HandCommand_t per send()**: Simpler, no shared mutable state. Rejected for 100Hz performance — Python object construction overhead matters at this rate.
- **One template per hand**: More flexible for per-hand PID tuning. Unnecessary for M3 (single hand). Can be added in M7 (tuning) if needed.
