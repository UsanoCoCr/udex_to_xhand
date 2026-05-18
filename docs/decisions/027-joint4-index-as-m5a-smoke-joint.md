# ADR-027: Joint 4 (index proximal flexion) is the canonical M5a smoke joint, commanded ±0.1 rad

Date: 2026-05-18
Status: Accepted
Milestone: M5a

## Context

The vendor `test_serial` sample asks the operator to type a finger id and a position. To make M5a reproducible, the plan has to pin *which* joint, at *what* position. XHand exposes 12 joints per hand (CLAUDE.md §"XHand joints"):

```
0 thumb_bend     3 index_bend    6 mid_1     8  ring_1   10 pinky_1
1 thumb_rota1    4 index_1       7 mid_2     9  ring_2   11 pinky_2
2 thumb_rota2    5 index_2
```

Any of these could in principle be the smoke joint. They differ in kinematic complexity, visibility, and slam risk.

## Decision

M5a commands **joint 4 (`index_1`, proximal flexion of the index finger)** with `position=0.1 rad` (~5.7°), then returns to `0.0`. Plan §3.5 scripts these exact inputs.

## Consequences

**正面**
- Single DOF, no kinematic coupling. A failure (no movement, wrong direction, oscillation) maps unambiguously to "joint 4 is broken" rather than "something in the thumb's 3-axis chain is broken".
- Index proximal flexion is the most visually obvious motion on the hand — operator can confirm physical move without instrumentation. The 2026-05-18 run logged read-back 0.0843 rad against 0.1 commanded, with operator confirming visible flex.
- 0.1 rad ≈ 5.7° is well within the joint's mechanical range and small enough to be safe even at vendor's kp=225 (ADR-026). No risk of slamming into a joint stop.
- Index flexion mirrors the most common UDCAP single-finger test signal in M2 (l3/l4/l5 index params per ADR-011), giving downstream M5b/M5c a natural continuity.

**负面 / 风险**
- Joint 4 working does not prove joint 0–3 or 5–11 work. They are individually validated only in M5b's project driver. Acceptable because M5a's claim is "the toolchain + .so + transport work", not "every joint works".
- A vendor firmware bug specific to thumb rotation axes (joints 1, 2) would slip past M5a. Acceptable: M5b/M5c will exercise those.

## Alternatives

1. **Joint 0 (thumb_bend).** Rejected: thumb is 3-DOF coupled (joints 0, 1, 2 share kinematics). A failure on joint 0 alone is harder to attribute; a failure that involves coupling between joints 0/1/2 looks like a joint 0 failure but isn't.
2. **Joint 6 (mid_1, middle proximal).** Acceptable but less visually unambiguous than index — middle finger sits between index and ring, harder to read at a glance.
3. **Full-hand gesture (fist or palm).** Rejected: moves all 12 joints simultaneously, masks per-joint failures. Belongs in M5c (project driver re-validation), not M5a (vendor baseline).
4. **Larger move, e.g. position=0.5 rad.** Rejected: under vendor's kp=225 (ADR-026), a 0.5 rad step would produce a much harder transient. 0.1 rad is the smallest move that still produces an observable physical motion (read-back > 0.05 rad acceptance threshold).
