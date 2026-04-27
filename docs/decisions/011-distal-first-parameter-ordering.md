# ADR-011: UDCAP Uses DIP-First (Distal→Proximal) Parameter Ordering

## Context

SPEC.md §3.1 assumed each finger's parameters were ordered proximal→distal:
- Index: l5=MCP Pitch, l6=MCP Yaw, l7=PIP Pitch, l8=DIP Pitch

Official documentation reveals the opposite — **distal→proximal** ordering:
- Index: l4=DIP Pitch, l5=PIP Pitch, l6=MCP Pitch, l7=MCP Yaw

This affects every single source index in `config.yaml`. For example, `index_joint1` (MCP flexion) was mapped to `sources: [5]` (old: "MCP Pitch"), but l5 is actually PIP Pitch. The correct source is l6 (MCP Pitch).

## Decision

Update all `config.yaml` source indices to match the official DIP→PIP→MCP→Yaw ordering. Document the ordering prominently in config.yaml comments and in `docs/verified-mapping.md`.

Corrected index→joint mapping:
```
l0=Thumb DIP, l1=Thumb PIP, l2=Thumb MCP, l3=Thumb Yaw
l4=Index DIP, l5=Index PIP, l6=Index MCP, l7=Index Yaw
l8=Mid DIP,   l9=Mid PIP,   l10=Mid MCP,  l11=Mid Yaw
l12=Ring DIP, l13=Ring PIP,  l14=Ring MCP, l15=Ring Yaw
l16=Pinky DIP,l17=Pinky PIP, l18=Pinky MCP,l19=Pinky Yaw
l20=Thumb Roll, l21=Index Roll, l22=Pinky Roll
```

## Consequences

- **正面**: Every source index in config.yaml is now correct — MCP flexion maps to MCP, not PIP
- **正面**: Pattern is consistent: each 4-param group = [DIP, PIP, MCP, Yaw], easy to remember
- **正面**: Thumb params (l0-l3) match same pattern, just with different joint names (IP, MCP, CMC, CMC Yaw)
- **负面**: Anyone reading old SPEC.md will have wrong mental model. SPEC.md §3.1 should be updated (deferred — SPEC is reference, config.yaml is operational)

## Alternatives

- **Remap at parse time**: Add an index remapping layer in `udcap_receiver.py` so downstream code sees the SPEC.md ordering. Rejected: adds unnecessary complexity and an extra translation layer. Better to fix the config to use actual indices.
- **Update SPEC.md §3.1 now**: Possible but SPEC is a planning document. The operational source of truth is config.yaml + verified-mapping.md. SPEC update deferred.
