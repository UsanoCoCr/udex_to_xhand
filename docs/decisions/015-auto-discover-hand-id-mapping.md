# ADR-015: Auto-Discover hand_id↔left/right Mapping at Runtime

## Context

config.yaml has `left_hand_id: 0` and `right_hand_id: 1` as static hints. The SDK provides `list_hands_id()` to discover connected hands and `get_hand_type(id)` to query whether each is "L" or "R". We could either trust the config values or discover dynamically.

## Decision

`__init__` calls `list_hands_id()` then `get_hand_type()` for each discovered ID, building a `{"left": id, "right": id}` dict at runtime. Config hints are ignored; only the SDK's hardware query is authoritative.

If `send("left", ...)` is called but no left hand was discovered, raise `RuntimeError`.

## Consequences

- **正面**: Catches miswired setups — if hand IDs are swapped or only one hand is connected, the mapping is still correct
- **正面**: No manual config update needed when swapping hands or changing hardware
- **负面**: Config `left_hand_id` / `right_hand_id` fields are currently unused (but kept for future M6 dual-hand as tie-breaker hints)
- **负面**: Startup is slightly slower (extra SDK calls), negligible in practice

## Alternatives

- **Trust config.yaml values directly**: Simpler, but fails silently when hardware doesn't match config. A swapped left/right hand would send mirror-image commands — dangerous and hard to debug.
- **Discover + validate against config**: Discover dynamically, warn if config disagrees. More complex, deferred to M6 when dual-hand makes ID conflicts more likely.
