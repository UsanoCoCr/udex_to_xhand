# ADR-022: JOINT_ORDER Constant as Config-Key-to-Index Bridge

## Context

XHand has 12 joints indexed J0-J11. Config uses human-readable names (`thumb_bend`, `index_joint1`, etc.). The mapper must produce a 12-element list where position [0] corresponds to J0, position [1] to J1, etc. Something must define which config key maps to which output index.

Options: (a) rely on YAML dict ordering matching J0-J11, (b) hardcode index numbers in config, (c) define an explicit constant tuple.

## Decision

Define `JOINT_ORDER` as a module-level tuple of 12 config key names in J0-J11 order. The mapper iterates this tuple to build specs, guaranteeing output order matches XHand joint IDs regardless of config key ordering.

```python
JOINT_ORDER = (
    "thumb_bend", "thumb_rota1", "thumb_rota2",
    "index_bend", "index_joint1", "index_joint2",
    "mid_joint1", "mid_joint2",
    "ring_joint1", "ring_joint2",
    "pinky_joint1", "pinky_joint2",
)
```

## Consequences

- **正面**: Single source of truth for the name↔index mapping — config can list joints in any order and the output is always correct
- **正面**: Init-time validation: if a config key is missing, the error message names the exact joint and expected index
- **正面**: Readable — the tuple directly documents XHand's joint layout
- **负面**: Adding a joint (hardware change) requires updating both config and this constant — but XHand's 12-DOF is fixed hardware

## Alternatives

- **Rely on YAML dict ordering**: Python 3.7+ and PyYAML preserve insertion order, so iterating `config["left"].items()` would work. But reordering keys in config.yaml would silently produce wrong joint assignments — a dangerous latent bug.
- **Use index numbers in config**: e.g., `{0: {sources: ...}, 1: {...}}`. Unambiguous but unreadable — operator must memorize which index is which finger.
