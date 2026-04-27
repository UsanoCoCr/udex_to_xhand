# ADR-010: No Wrist Parameters Exist in UDCAP JSON SDK

## Context

SPEC.md §3.1 assumed l21-l23 were wrist joints (Pitch, Yaw, Roll) and marked them as "EXCLUDED" from the mapping. The scope document (§6) explicitly listed "Wrist control — UDCAP wrist data (l21-l23) is ignored" as out of scope.

Official UDCAP documentation reveals:
- l20 = Thumb MCP Roll (not wrist)
- l21 = Index MCP Roll (not wrist)
- l22 = Pinky MCP Roll (not wrist)
- l23 = Gesture recognition reserved flag (always -1, not a joint)
- l24-l27 = IMU quaternion data (not joint angles)

There are **zero wrist parameters** in the JSON SDK output.

## Decision

Remove all references to "wrist exclusion" from the mapping logic. Treat l20 as a usable joint parameter (Thumb MCP Roll → maps to `thumb_rota2`). Treat l21/l22 as discarded parameters (no XHand DOF for index/pinky roll). Treat l23 as non-joint data to be ignored.

## Consequences

- **正面**: l20 is now correctly used for thumb opposition (thumb_rota2), which was previously mapped to l2 (wrong index)
- **正面**: "Wrist excluded" ceases to be a design constraint — it was never an option
- **正面**: SPEC.md risk #2 ("UDCAP parameter ordering unknown") is fully resolved for this axis
- **负面**: If wrist data is ever needed, it cannot come from the JSON SDK — would need a different UDCAP API or sensor

## Alternatives

- **Keep l20-l22 as "excluded wrist"**: Factually incorrect. Would cause thumb_rota2 to use the wrong source index
- **Try to extract wrist from IMU quaternion (l24-l27)**: IMU gives hand orientation in world frame, not wrist angle relative to forearm. Different data, different use case. Out of scope regardless
