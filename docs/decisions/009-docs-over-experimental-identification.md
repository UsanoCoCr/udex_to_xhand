# ADR-009: Official Documentation Supersedes Experimental Identification Script

## Context

M2 was originally designed around an experimental approach: `scripts/udcap_param_identify.py` has the operator flex one finger at a time, measures which l-indices respond, and builds a mapping table. The operator ran the script on the right hand and produced `docs/verified-mapping.md` with experimental results.

However, the experimental results showed significant problems:
- Cross-talk: r1 (should be Thumb PIP) triggered under "PINKY", r2 (Thumb MCP) triggered under "RING"
- Many indices labeled "WRIST" or "???" that are actually finger joints
- Only 1 of 24 assignments was clearly correct (r0=THUMB)

Separately, official UDCAP documentation was found at the [JSON SDK joint angle manual](https://udexreal.gitbook.io/udexreal-docs/docs-cn/c++-python-sdk/json-c++python-sdk-guan-jie-jiao-du-shi-yong-shou-ce) which provides an authoritative, complete parameter-to-joint mapping.

## Decision

Use the **official documentation** as the authoritative source for the l0-l22 → finger/joint mapping. The experimental script results are superseded. The script itself is retained for future hardware validation (confirming the docs match reality on our specific gloves), but is not the source of truth for `config.yaml`.

## Consequences

- **正面**: Mapping is based on vendor documentation, not noisy single-operator experimental data
- **正面**: config.yaml sources now correct — no risk of mapping thumb to pinky
- **正面**: Experiment script remains useful for validation, not wasted work
- **负面**: We haven't experimentally confirmed the docs match our specific glove firmware version — this is deferred to M4 integration testing

## Alternatives

- **Trust experimental data only**: The cross-talk and misidentification issues made this unreliable without multiple-run averaging and better isolation of finger movements
- **Rerun experiment with stricter protocol**: Possible but unnecessary given we now have official documentation. Can be done in M4 as validation
- **Hybrid approach (docs + experiment weighting)**: Overcomplicated. Docs are authoritative; if they're wrong, we'll discover it in M4 integration testing
