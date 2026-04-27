# ADR-013: Thumb MCP Roll (l20) Maps to thumb_rota2 — Non-Contiguous Source

## Context

UDCAP groups most finger parameters contiguously (l0-l3 for thumb, l4-l7 for index, etc.), but the MCP Roll angles are placed at the end: l20 (thumb), l21 (index), l22 (pinky). This means `thumb_rota2` (XHand J2, thumb opposition) must reference `sources: [20]` — far from the other thumb params at l0-l3.

The old config.yaml (based on SPEC.md's incorrect assumption) had `thumb_rota2: sources: [2]`, which was l2. Under the correct mapping, l2 is actually Thumb MCP Pitch (a flexion parameter), not Roll.

## Decision

Map `thumb_rota2` to `sources: [20]` (Thumb MCP Roll). Accept the non-contiguous index. The config-driven architecture already supports arbitrary source indices, so this requires no code change — only a config.yaml update.

## Consequences

- **正面**: Thumb opposition is correctly driven by the Roll axis, which physically corresponds to opposition/reposition of the thumb
- **正面**: Previously, thumb_rota2 was driven by MCP Pitch (l2 under old mapping), which would have caused flexion input to drive rotation — a serious mapping error avoided
- **负面**: config.yaml thumb section references both [0,1,2] and [20], which may confuse future readers expecting contiguous ranges
- **正面**: Config comments now document the full UDCAP layout including the non-contiguous roll params

## Alternatives

- **Remap in receiver to make indices contiguous**: Rejected. Adds a translation layer with its own bugs. Better to use the vendor's native indexing throughout.
- **Use l3 (Thumb MCP Yaw) for thumb_rota2 instead of l20**: l3 is abduction (lateral movement), l20 is roll (opposition). These are different axes. thumb_rota1=l3 (yaw), thumb_rota2=l20 (roll) is the correct semantic mapping.
