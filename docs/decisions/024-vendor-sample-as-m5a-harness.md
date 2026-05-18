# ADR-024: Use vendor `test_serial` sample as the M5a bring-up harness

Date: 2026-05-18
Status: Accepted
Milestone: M5a

## Context

M5a needs to prove that on G1 PC2 (aarch64) we can:
1. Link against vendor `libxhand_control.so` + headers (`xhand_control_sdk/`),
2. Open `/dev/ttyACM0` and talk to a real LEFT XHand,
3. Run the full lifecycle: `open_serial → list_hands_id → send_command → read_state → close_device`.

Two ways to do this:

A. Write a small (~30-line) homemade C++ probe using `xhand_control.hpp` directly.
B. Build and run the vendor's own `xhand_control_sdk/tests/src/serial_test.cpp` sample.

Both would compile against the same `.so`. The question is which one becomes our M5a artefact.

## Decision

Use vendor `test_serial` as-is. M5a writes no project C++ code; the only edit is a per-checkout `sed` patch on the port string (see ADR-025). All eight §3.5 acceptance items are evaluated against vendor sample output.

Project C++ source (`src/main.cpp`, `src/xhand_driver.{hpp,cpp}`, …) starts in M5b, on top of an already-trusted toolchain.

## Consequences

**正面**
- Vendor sample is the known-good baseline. If M5b's link line later fails, we can diff against this run instead of three-way-bisecting (toolchain vs SDK vs our code).
- The sample exercises more SDK surface than a minimal probe would (parameters dump, version dump, mode select prompt, `read_state` after each `send_command`). M5a captures a richer baseline for cheap.
- Zero project code touched in M5a → DoD §7.4 ("`git status` under `xhand_control_sdk/` shows no changes") is trivially satisfied via `sed -i.bak` + restore.

**负面 / 风险**
- Vendor sample also calls `set_hand_name` and `reset_sensor`, which have persistent side effects on the hand. We accept these for M5a (see Alternatives) and revisit in M5b where we own the call sequence.
- Sample uses vendor's own PID defaults (kp=225, tor_max=350), not the project's (kp=100, tor_max=300 per CLAUDE.md). This is an intentional separation (ADR-026) — M5a tests the toolchain, not project params.
- Sample is interactive (stdin prompts). Not amenable to CI; M5a is a one-shot operator-driven run, not a regression test.

## Alternatives

1. **Write a homemade probe in `src/m5a_probe.cpp`.** Rejected: produces a less authoritative baseline; would need to be deleted or moved before M5b; doubles the "did link work?" surface area we have to debug if M5b fails to link.
2. **Skip M5a entirely, jump straight to M5b.** Rejected: violates the "isolate variables" principle. If M5b fails, we cannot distinguish "vendor SDK doesn't work on PC2" from "our CMake / our wrapper is broken". M5a is the cheapest possible bisect bisector.
3. **Use vendor `test_ethercat` sample instead.** Rejected: EtherCAT path is out of scope (SPEC.md §7 specifies RS485/USB, ADR-014 specifies CDC-ACM); EtherCAT requires different hardware wiring.
