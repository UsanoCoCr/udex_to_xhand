# ADR-025: Keep vendor SDK source pristine — patch via `sed -i.bak`, never commit

Date: 2026-05-18
Status: Accepted
Milestone: M5a (policy applies to all future vendor SDK interaction)

## Context

Vendor sample `xhand_control_sdk/tests/src/serial_test.cpp:36` hardcodes the serial port as `/dev/ttyUSB0`. Our hardware enumerates as `/dev/ttyACM0` (ADR-014: XHand RS485 adapter is CDC-ACM, not an FTDI/CH340 USB-serial bridge).

We need to change that one line to actually run M5a. The question is whether the change becomes a tracked commit or stays a per-checkout local mutation.

Side question: vendor sample also calls `set_hand_name(..., "xhand")` (line 68) and `reset_sensor(...)` (line 79). Both have persistent side effects. Plan §1.2 lists commenting them out as *optional*. The operator left them enabled for the 2026-05-18 run — log shows `save to flash ok set name successfully, current name xhand` and `reset sensor successfully`. Future operators may want them disabled.

## Decision

Treat `xhand_control_sdk/` as a read-only vendor drop. Any change needed to actually run a vendor sample is applied with `sed -i.bak` and reverted from the `.bak` file before `git add`. The plan's validation step §4.5 fails loudly if the working tree under that path is still dirty.

This rule covers:
- the port-name patch (always applied in M5a),
- the optional `set_hand_name` / `reset_sensor` comment-outs (whether enabled or not),
- any future vendor sample tweak.

## Consequences

**正面**
- When vendor ships a new SDK drop (new `.so` / new headers / new samples), we can untar it on top of `xhand_control_sdk/` with zero merge conflict — there are no local commits inside that tree to rebase.
- The environment-specific bit (port name) lives in the operator's `sed` invocation, not the repo. A different lab using `/dev/ttyUSB0` runs the same plan with a different sed and the repo stays identical.
- Reviewers diffing M5a's commit see only execution evidence (log + plan §6 + ADRs + roadmap), not vendor source churn.

**负面**
- Every M5a re-run on a new PC2 requires re-applying the sed. The plan §3.4 documents the exact command; the cost is one shell line.
- The `set_hand_name` and `reset_sensor` calls are still executed by default on M5a runs. The operator captured "current name xhand" being written to flash — harmless (same name as before) but a persistent write nonetheless. Future M5a re-runs can opt into the comment-out via the same sed-not-committed mechanism.

## Alternatives

1. **Commit the port-name patch as a `.patch` file under `xhand_control_sdk/tests/local/`.** Rejected: still pollutes the vendor tree, and now we have a patch file to maintain when vendor changes line numbers.
2. **Fork the vendor sample into project `src/`.** Rejected: defeats ADR-024's goal of using the vendor's own baseline. We would also have to maintain the fork against future vendor changes.
3. **Patch the port name via `configure_file()` at cmake time.** Rejected: requires modifying the vendor's `tests/CMakeLists.txt`, which is the same kind of vendor-tree mutation we're avoiding. Over-engineered for a one-time bring-up.
4. **Add a udev rule that creates a `/dev/ttyUSB0` symlink to `/dev/ttyACM0`.** Rejected: works around the issue but adds an opaque system-level dependency. Code that says ACM is clearer than a symlink that lies about CDC-ACM being a USB-serial bridge.
