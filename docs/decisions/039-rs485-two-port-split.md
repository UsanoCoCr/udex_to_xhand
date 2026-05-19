# ADR-039: RS485 Two-Port Split (One CDC-ACM per XHand)

## Context

M7 plan rev1 §3 / §4.2 / §7 was written on the assumption that PC2 would expose dual XHands as a single USB-to-RS485 multi-drop bus — i.e. one `/dev/ttyACM0`, one `XHandControl::open_serial` call, `list_hands_id()` returning `[left_hand_id, right_hand_id]`. SPEC §2 / §10 risk 3 ("RS485 dual-hand bus contention") encoded the same hypothesis. Single-instance multi-drop would have matched the bandwidth math in SPEC §2 (`~5Kbits/cycle vs 3Mbps available`) and required no changes to `XHandDriver`.

On 2026-05-19, the first §4.2 probe on G1 PC2 revealed a different reality:

- `/dev/ttyACM0` does NOT exist. USB enumeration assigned `/dev/ttyACM1` through `/dev/ttyACM4`.
- `dmesg` shows **two physical USB devices** (`usb 1-2.2`, `usb 1-2.3`), each a CDC-ACM composite with one primary control endpoint (interface 1.0) and one auxiliary endpoint (interface 1.2):
  - `usb 1-2.2 → ttyACM1` (primary) + `ttyACM4` (aux) — physical **Right XHand**
  - `usb 1-2.3 → ttyACM2` (primary) + `ttyACM3` (aux) — physical **Left XHand**
- Operator-confirmed mapping (single-port probes with `--port /dev/ttyACMx --hand left --duration 3`): `/dev/ttyACM2` discovers a Left hand, `/dev/ttyACM1` discovers a Right hand. The auxiliary endpoints (`ttyACM3` / `ttyACM4`) are diagnostic / firmware ports — they accept `open_serial` but `list_hands_id()` is empty or errors out, depending on probe.

So each XHand has its own USB-to-RS485 path with its own CDC-ACM node. The two hands do not share an RS485 bus. M7 rev1's single-port architecture would have required hardware that PC2 doesn't have (a multi-drop USB-to-RS485 splitter wiring both XHand 485 endpoints together — which is not how the units ship).

The rev1 plan's §4.2 FAIL criterion was an explicit STOP-and-replan rule precisely for this case. ADR-040 (rev1, pre-registered: `XHandDriver::open(require_both)` fail-closed) is a downstream artifact of the rev1 multi-drop model and only makes sense in a world where a single bus enumerates both `hand_id`s.

## Decision

**Use the two-port architecture as the M7 runtime model.** Specifically:

1. **`XHandDriver` is single-port, single-hand-or-bust.** Its existing `open()` already enumerates `list_hands_id()` on whatever bus its `XHandControl` instance was opened on, and labels each id via `get_hand_type()` into `hand_id_left_` / `hand_id_right_`. On a bus with one hand, exactly one of those `std::optional<uint8_t>` ends up populated. No code change to the driver.

2. **`main.cpp` instantiates two `XHandDriver` objects** when `--hand both` is requested: `driver_left` opens `config.xhand.left_serial_port`, `driver_right` opens `config.xhand.right_serial_port`. Single-hand modes (`--hand left` or `--hand right`) instantiate just the matching driver.

3. **Post-open sanity check, in `main.cpp` not in the driver.** After `driver_left->open()`, `main.cpp` asserts `driver_left->has_left()`. If false (port enumerated a Right hand because cables are crossed, or no hand at all because the bus is empty), `LOG_ERROR` with the offending port + observed side + return 2. The driver dtor still runs mode=0 + close on whatever was discovered. Symmetric for the right side.

4. **`config.yaml` schema migrates** from a single `xhand.serial_port` to `xhand.left_serial_port` + `xhand.right_serial_port`. Legacy keys `left_hand_id` / `right_hand_id` (never read by C++; hand id is auto-discovered from `list_hands_id()` on each bus) are dropped.

5. **`--port` CLI override is single-side only.** With `--hand left`, `--port` overrides `xhand.left_serial_port`; with `--hand right`, `xhand.right_serial_port`. With `--hand both`, `--port` is ambiguous and rejected with a clear error message + exit 2. This applies to both FULL mode and `--actions` mode.

6. **ADR-040 (rev1 pre-register) is RETIRED before being written.** The rev1 plan §7 pre-registered ADR-040 as "`XHandDriver::open(require_both)` fail-closed on partial discovery" — that mechanism no longer exists in the codebase (commit `375e8e7` reverted it). The fail-closed semantic still applies, but it lives in `main.cpp` as a straightforward "log + return 2" pattern that matches ADR-036's startup-gate philosophy and doesn't need its own ADR.

## Consequences

- **Positive**: Mapping math + safety + UDP receiver + watchdog + signal handling + clamping all unchanged. Every M0–M6 verified asset (config.yaml mapping values, ADRs 020/021/022 clamping semantics, ADRs 035/036/038 safety contracts, snapshot test, etc.) carries forward bit-identical. Only `main.cpp` wiring + `XHandConfig` struct + `config.yaml` schema move.
- **Positive**: SPEC §10 risk 3 ("RS485 dual-hand bus contention") closes — there is no shared bus to contend on. Latency from M5c single-hand baseline (avg 9.6 / p95 9.6 / max 10.7 ms) is the best floor estimate; dual cost depends on USB scheduling and per-port SDK serialization, not RS485 contention.
- **Positive**: Cabling-swap detection is essentially free. `get_hand_type()` per port + `has_left()` / `has_right()` post-open already gives us a one-line LOG_ERROR for "left port enumerated a Right hand" — an operator-friendly failure mode that the rev1 single-bus model could not catch (a single bus with both hands has no concept of "which port is left").
- **Positive**: Each port's driver dtor independently shuts down its hand. A failure to send mode=0 on the left bus cannot prevent mode=0 on the right bus (cf. main.cpp shutdown branch with two independent try/catch).
- **Negative — fragility**: Linux USB enumeration order is not stable across reboots / replug / hub-power events. `config.yaml`'s declared `left_serial_port` / `right_serial_port` may need updating after physical changes. SPEC §10 NEW R8 tracks this; plan rev2 §4.2 has an explicit re-probe procedure.
- **Negative — twice the SDK init cost**: two `XHandControl::open_serial` calls instead of one. ~tens of milliseconds added to startup; negligible compared to the 10s startup-gate ceiling (ADR-036).
- **Negative — diagnostic noise**: `ttyACM3` / `ttyACM4` (the 1.2 auxiliary interfaces) are visible in `ls /dev/ttyACM*` and may confuse future debugging. SPEC §2 will document the four-port enumeration shape on M7 closeout.
- **Negative — rev1 plan partially dead-letter**: rev1 §3 / §4.2 / §7 / §9 R1 are superseded; rev2 §10 records the override but the rev1 prose remains in-file for review traceability. ChatGPT/Gemini reviewers MUST read rev2 §10 before judging rev1.

## Alternatives Considered

- **Stick with single-port multi-drop**: Rejected. Hardware does not present this topology. No reasonable amount of software can pretend a single CDC-ACM device sees both hands.
- **One `XHandDriver` holding two `XHandControl` instances internally**: Rejected (Q2 user decision, 2026-05-19). It thickens the driver class with multi-port lifecycle, two open_serial calls in one `open()`, two close_device calls in one `shutdown()`, and a per-port error-isolation problem (if the left port fails open, what does the right side see?). The two-driver model already gives us natural error isolation via two `std::optional<XHandDriver>` with independent try/catch blocks. Driver class stays thin.
- **Keep `require_both` parameter, just route it through one of the two drivers**: Rejected. Under two-driver model each driver opens one port and the "did I find a hand" question is trivially answered by `has_left()` / `has_right()` post-open. `require_both` parameter would be dead code (no caller would pass `true`).
- **Per-port hand-side hint in `XHandDriver::open(HandSide hint)`**: Rejected. The post-open sanity check in `main.cpp` already catches cabling swaps with strictly more information (knows the configured side AND the observed `get_hand_type` result). Pushing the check into the driver would not catch anything new; would couple the driver to a config-level concept.
- **Use vendor `enumerate_devices("RS485")` output to auto-pick ports**: Rejected for M7. The probe (`test_serial` output) shows enumerate_devices returning all four ttyACMx + ttyS0..3 — no way to distinguish primary from auxiliary endpoints without trying to open each. An auto-detector would need a probe-and-discard pass; deferring this until SPEC §10 R8 actually causes operational pain.

## References

- M7 plan rev2 (`docs/plans/20260519-m7-dual-hand-integration-plan.md`) §10 — section-by-section override of rev1
- M7 plan rev1 §3 / §4.2 / §7 / §9 R1 — superseded
- Commit `375e8e7` — revert of `require_both` parameter (ADR-040 rev1 retirement made concrete)
- ADR-014 — `/dev/ttyACM*` (CDC-ACM) device path convention
- ADR-017 — log-not-crash on transient send errors (carries over per-port unchanged)
- ADR-018 — mode=0 is not full power-off (per-port shutdown semantics unchanged)
- ADR-029 — XHand-side calibration is not exposed by SDK (carries over per-port unchanged)
- ADR-030 / ADR-037 — snapshot fixture SHA self-check (regen-required policy applied during this commit for the `config.yaml` schema migration)
- ADR-036 — startup-gate exit 2 + driver dtor cleanup (per-port version unchanged)
- SPEC §2 — architecture diagram (NEEDS UPDATE on M7 closeout: redraw with two RS485 paths)
- SPEC §10 R3 — closes; SPEC §10 NEW R8 — opens (USB enumeration fragility)
- SPEC §11 — config.yaml schema example (UPDATE on M7 closeout: `serial_port` → `left_serial_port` + `right_serial_port`)
- `dmesg` 2026-05-19 — `usb 1-2.2:1.0: ttyACM1 USB ACM device`, etc. (the topology evidence)
