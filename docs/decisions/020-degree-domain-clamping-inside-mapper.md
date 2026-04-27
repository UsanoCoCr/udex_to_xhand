# ADR-020: Clamp in Degree Domain Inside Mapper, Not Radian Domain Outside

## Context

The mapping pipeline converts UDCAP degrees → XHand radians. Clamping must happen somewhere. Two natural choices exist: (a) clamp in degrees inside `mapper.map()` before the deg→rad conversion, or (b) convert config clamp ranges to radians at init time and clamp in radians outside the mapper (in `main.py` or `safety.py`).

Config specifies clamp ranges in degrees (e.g., `clamp: [0, 110]`) because that's the unit operators think in when tuning.

## Decision

Clamp in degrees inside `mapper.map()`, immediately after the weighted sum and sign flip, before the deg→rad conversion. The formula order is: weighted_sum → sign → offset → clamp(deg) → deg2rad.

## Consequences

- **正面**: Clamp values in config are directly comparable to the values being clamped — no unit conversion needed when debugging or tuning. An operator sees `clamp: [0, 110]` and knows exactly what range to expect.
- **正面**: The deg→rad conversion happens exactly once, at the end, on already-clamped values. No risk of clamping a radian value with a degree limit or vice versa.
- **负面**: External code (main.py, safety.py) cannot observe the degree-domain value — it only sees the final radians. If degree-domain debugging is needed, it must happen inside the mapper.

## Alternatives

- **Clamp in radians outside mapper**: Convert config limits to radians at init. Simpler mapper (just compute + convert), but introduces a unit mismatch between config file (degrees) and runtime clamp (radians). Debugging requires mental deg↔rad conversion.
- **Clamp in both domains**: Redundant and confusing — two places enforcing the same logical constraint in different units.
