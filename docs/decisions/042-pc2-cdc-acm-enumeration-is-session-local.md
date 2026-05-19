# ADR-042: PC2 CDC-ACM Enumeration Is Session-Local; `config.yaml` Port Bindings Need Per-Session Re-Probe

## Context

M7 plan rev2 §10.0 documented the PC2 USB CDC-ACM mapping observed during morning hardware probe (2026-05-19): four ACM nodes numbered 1–4, with `/dev/ttyACM2` → Left XHand, `/dev/ttyACM1` → Right XHand. ADR-039 + commit `4111a82` baked those values into `config.yaml` (`xhand.left_serial_port` / `xhand.right_serial_port`) and the `XHandConfig` C++ struct defaults. Plan rev2 §9 NEW R8 ("PC2 USB topology dependency") pre-registered the fragility risk.

R8 materialized within the same workday. Test-execution logs (2026-05-19 22:30):

- `docs/logs/m7-enum-2026-05-19.log` shows the vendor `test_serial` enumeration now lists `/dev/ttyACM0`, `ttyACM1`, `ttyACM2`, `ttyACM3` (numbered 0–3, not 1–4 as in the morning), and discovers a Right hand (`hand id: 0 type: R serial number: 012R3202C0707003`) on `/dev/ttyACM0`.
- `docs/logs/m7-watchdog-dual-2026-05-19.log` shows the M7 binary's two `XHandDriver` instances opened `/dev/ttyACM2` (→ `hand_id=1 type=Left`) and `/dev/ttyACM0` (→ `hand_id=0 type=Right`). The Right hand is no longer on `/dev/ttyACM1` — it shifted by one slot.
- For the tests to run, the PC2 operator had to edit `config.yaml`'s `right_serial_port` from `/dev/ttyACM1` to `/dev/ttyACM0` locally on PC2 before launching the binary. Without that edit, `XHandDriver::open()` on the right side would hit `OPEN_DEVICE_FAILED (1501039)` on the non-existent `/dev/ttyACM1`, or worse — silently open a stale auxiliary endpoint.

Cause is plain: Linux CDC-ACM minor-number assignment is enumeration-order dependent, not stable across reboots / USB replugs / hub-power events. The vendor `xhand_control_sdk` exposes `enumerate_devices()` but only returns the raw ttyACMx paths — it does not bind them to stable per-XHand identifiers.

This is the same class of fragility we have on `/dev/ttyUSBx` enumeration that ADR-014 already pivoted us away from (CDC-ACM was preferred precisely because it was supposed to be more deterministic — empirically, it isn't, when you have multiple XHands).

## Decision

- **`config.yaml`'s `xhand.left_serial_port` / `xhand.right_serial_port` are PC2-session-local values, not stable cross-reboot identifiers.** The canonical `config.yaml` committed at M7 ✅ records the last observed working mapping (`/dev/ttyACM2` / `/dev/ttyACM0`), but every reboot, USB replug, or USB hub power event invalidates them.
- **Before any M-future session** (M8 PID tuning, M8 acceptance test, day-N teleop, post-M8 daily use), the operator MUST re-probe with the plan rev2 §10.2.3 enumeration command and update `config.yaml` if the mapping shifted. The fixture-regen + same-commit policy (ADR-037) applies as usual.
- **Detection is loud and immediate, by design.** Wrong port → `OPEN_DEVICE_FAILED` from the SDK → driver throws → main `LOG_ERROR` + `return 2`. Wrong side on a valid port → post-open `has_left()` / `has_right()` check (ADR-039 §3.4') fires with explicit LOG_ERROR naming the configured port + observed side. No silent degradation.
- **We do NOT implement udev-rule-based stable symlinks** (`/dev/xhand-left` / `/dev/xhand-right` keyed on USB serial number) in M7. The udev path is the right long-term fix but expands M7 surface; scoped as a post-M7 polish candidate (M8 sidecar or post-M8 hardening task) — opens a new ADR if pursued.
- **We do NOT implement auto-probing** (open each ACM, match `get_hand_type` against expected side). Each failed `open_serial` costs multiple seconds of SDK retry overhead (test_serial log shows `try open device times: 1 / 3`); probing all 4–N ACM nodes at startup would push startup-gate latency over ADR-036's 10s ceiling. Out of scope.

## Consequences

- **Positive — failure is observable, not silent.** Either `OPEN_DEVICE_FAILED` (port doesn't exist) or `has_left()/has_right()` mismatch — both produce explicit LOG_ERROR + exit 2 with the offending port path printed. Mean-time-to-diagnose is seconds.
- **Positive — fix is a one-line config edit + fixture regen + push** (or local-only edit if you're on PC2 between commits). No code changes. Takes <2 minutes.
- **Positive — the M7 architecture isolates the failure to one side.** If the left port is wrong but the right is correct, the right driver still opens, its destructor runs mode=0 + close cleanly on shutdown. No cross-side contamination.
- **Negative — every reboot of PC2 can require operator intervention before the next teleop session.** Slows down daily team use that depends on PC2 power cycling.
- **Negative — `config.yaml` committed on origin/main may not match PC2's local file** after enumeration shifts. Fresh-clone-on-PC2 workflows may fail at first hand-discovery until the operator re-probes and updates. Mitigation: this ADR + plan rev2 §10.2.3 + SPEC.md §11 spell out the re-probe procedure; first-time setup docs (CLAUDE.md "Commands" section) carry the link.
- **Negative — ADR-039's stated mapping (ACM2 / ACM1) is already historically stale.** ADR-039 won't be edited (ADRs are immutable history); future readers learn the latest-good mapping from `config.yaml` + this ADR's policy rather than from ADR-039's body. Cross-reference added.
- **Negative — the four-ACM-node-shape on PC2 is unintuitive.** Each XHand contributes two CDC-ACM endpoints (primary interface 1.0 + auxiliary interface 1.2); only the primaries respond to `list_hands_id()`. An operator who naively `--port`s the auxiliary will see `OPEN_DEVICE_FAILED` or empty `list_hands_id()`. SPEC §2 / §11 + plan rev2 §10.0 document the topology but operators are still likely to trip on this once each.

## Alternatives Considered

- **Udev rules → stable `/dev/xhand-{left,right}` symlinks keyed on USB serial number**: the right long-term fix. Vendor-shipped XHands have unique serials (e.g. `012R3202C0707003` per the m7-enum log). Need (a) a one-time udev rules file installed on PC2, (b) config.yaml entries pointing at the symlinks, (c) SPEC.md documentation of the rules. Rejected for M7 scope as not-on-critical-path-to-acceptance; revisit if R8 keeps biting operationally.
- **Embed USB serial number in `config.yaml` and resolve at runtime**: similar effect, but requires C++ code change (sysfs walk: `/sys/bus/usb-serial/devices/ttyACMx/../serial`). More invasive than udev rules; covers the same failure mode. Rejected for M7.
- **Auto-detect by probing every ACM port**: per "Decision" above — failed opens cost real wall-clock time + the auxiliary endpoints (1.2 interfaces) confound the probe. Rejected as overengineering.
- **Accept a fixed PC2-side bash wrapper that rewrites `config.yaml` on every boot from `udevadm` output**: rejected. The wrapper would live outside the repo, drift, and produce surprises. ADR-037 explicitly chose committed-fixture policy over generated-at-build to detect such drift.
- **Increase the auxiliary endpoint visibility — block the 1.2 interface in the SDK or via a kernel param**: rejected. Vendor controls the SDK; PC2's kernel is part of the G1 robot's stack. Not our subsystems to modify.

## References

- Plan rev2 §10.0 (morning's mapping) vs `docs/logs/m7-watchdog-dual-2026-05-19.log` + `docs/logs/m7-enum-2026-05-19.log` (evening's mapping) — direct evidence of drift
- Plan rev2 §9 NEW R8 — the risk that materialized
- ADR-014 — CDC-ACM device-path convention (the earlier pivot away from ttyUSBx for similar fragility)
- ADR-037 — snapshot-fixture-on-config-change policy (used in the M7 closeout fix commit to regen the fixture after right port edit)
- ADR-039 — two-port architecture; this ADR's policy lives on top of ADR-039's structural choice
- SPEC.md §11 (config schema) + §10 R3 (RS485 contention — closed; replaced by R8/this ADR)
- `docs/logs/m7-enum-2026-05-19.log` — the test_serial run that revealed Right on ACM0
- `docs/logs/m7-watchdog-dual-2026-05-19.log` — the live binary's `[INFO] Serial: /dev/ttyACM0 @ ... hand_id=0 type=Right` line, against `config.yaml`-on-origin then saying `right_serial_port: /dev/ttyACM1`
- M7 closeout commit (after this one) — pins `config.yaml` to the observed mapping (ACM2 / ACM0)
