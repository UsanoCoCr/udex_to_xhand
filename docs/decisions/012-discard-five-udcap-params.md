# ADR-012: Discard 5 UDCAP Joint Parameters (No XHand DOF)

## Context

UDCAP provides 23 joint parameters per hand (l0-l22). XHand has 12 DOF per hand. The mapping must reduce 23→12, meaning 11 params are either combined (weighted sums) or discarded.

After mapping, 18 UDCAP params are used (some combined into single XHand joints). Five UDCAP params have no corresponding XHand DOF:

| Discarded | Joint | Reason |
|-----------|-------|--------|
| l11 | Middle MCP Yaw | XHand mid_joint1/2 are flexion only, no lateral spread |
| l15 | Ring MCP Yaw | Same — ring has no spread DOF on XHand |
| l19 | Pinky MCP Yaw | Same — pinky has no spread DOF on XHand |
| l21 | Index MCP Roll | XHand index has bend (J3=Yaw) but no roll |
| l22 | Pinky MCP Roll | XHand pinky has no roll DOF |

Note: l7 (Index MCP Yaw) is NOT discarded — it maps to `index_bend` (J3), XHand's only lateral spread DOF.

## Decision

Discard l11, l15, l19, l21, l22 silently. They are read from UDP but not referenced in any `config.yaml` mapping source list. No warning or logging when these params have non-zero values.

## Consequences

- **正面**: Simple — unused params are just not referenced. No code change needed in receiver or mapper
- **正面**: If XHand hardware is later upgraded with more DOF, these can be added to config.yaml without code changes
- **负面**: Operator's middle/ring/pinky finger spread gestures are lost. This may affect naturalism of grasping (e.g., spreading fingers around large objects)
- **负面**: Index and pinky MCP roll are lost. Thumb roll (l20) is preserved via thumb_rota2

## Alternatives

- **Blend Yaw into flexion**: Add a fraction of MCP Yaw to the flexion channel (e.g., `mid_joint1 sources: [10, 11]`). Rejected: Yaw and flexion are physically different axes. Mixing them would produce unpredictable motion.
- **Log a warning when discarded params are large**: Would produce constant noise during normal operation (fingers naturally splay). Not useful.
- **Virtual coupling**: Map multiple UDCAP fingers' Yaw to the single XHand index_bend (J3). Rejected: confusing operator experience — only index spread should control index spread.
