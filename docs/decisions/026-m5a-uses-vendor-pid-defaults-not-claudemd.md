# ADR-026: M5a runs with vendor PID defaults (kp=225, tor_max=350), not project defaults (kp=100, tor_max=300)

Date: 2026-05-18
Status: Accepted
Milestone: M5a

## Context

CLAUDE.md (Code conventions) mandates project-wide XHand PID defaults: `kp=100, ki=0, kd=0, tor_max=300, mode=3`.

Vendor sample `serial_test.cpp:193-198` bakes a different baseline into its mode=3 path: `kp=225, ki=0, kd=0, tor_max=350`. The sample takes finger_id and position from stdin but reuses these fixed PID values.

The plan deliberately uses the vendor sample as-is (ADR-024) and does not commit edits to vendor source (ADR-025). So we have to pick one of two PID baselines for M5a, and the choice has safety implications: kp=225 produces a stiffer, faster response than kp=100, which at large position commands could cause slam.

## Decision

In M5a, do **not** override vendor PID defaults. Run with `kp=225, ki=0, kd=0, tor_max=350` exactly as vendor ships. Project defaults (`kp=100 / tor_max=300`) become authoritative only in M5b, where they are loaded from `config.yaml` by the project's own driver code.

Safety envelope is preserved by capping the M5a move at ±0.1 rad (~5.7°) — small enough that kp=225 stiffness cannot cause physical damage even on first contact.

## Consequences

**正面**
- Smaller delta vs the vendor's known-good baseline → if M5a fails, we don't have "did our PID change break it?" as a hypothesis. ADR-024's bisect-baseline goal is preserved end-to-end.
- The 2026-05-18 run confirmed the regime is safe: joint 4 commanded to 0.1 rad, read-back 0.0843 rad, no buzz/slam/overshoot reported by the operator. Empirical data point that vendor defaults handle a small command cleanly on this hand.
- Boundary stays clean: M5a tests "vendor SDK + .so + hardware talk to each other"; M5b tests "our config.yaml params load and clamp correctly". Mixing them would muddy both.

**负面 / 风险**
- M5a operators must remember the ±0.1 rad cap. If a future operator types 1.0 rad at the prompt, kp=225 will produce a meaningfully harder slam than kp=100 would. Mitigated by plan §3.5 explicitly scripting the input values; not relied on as the only safety mechanism (kp=225 with tor_max=350 is still within the joint's mechanical range).
- M5b cannot reuse M5a's PID validation as evidence — when M5b's `config.yaml` PID hits the same joint, it is a separate experimental data point.

## Alternatives

1. **Patch `serial_test.cpp` to use kp=100 / tor_max=300.** Rejected: adds sed surface beyond the port name (ADR-025 keeps the patch minimal), introduces a delta from vendor baseline (defeats ADR-024), and the patch would not transfer to M5b anyway because M5b reads PID from `config.yaml`.
2. **Skip mode=3, use mode=0 (passive) for M5a.** Rejected: passive mode does not energize the motor, so the §3.5 acceptance item "position read-back > 0.05 rad after commanding 0.1 rad" cannot be evaluated. Mode=3 is the mode the project will actually use.
3. **Use mode=5 (force control) to sidestep position PID.** Rejected: out of scope (project is position-control-only, SPEC.md §9), and we would still need *some* PID values — they would just be vendor's force-mode defaults instead, which is the same issue under a different name.
