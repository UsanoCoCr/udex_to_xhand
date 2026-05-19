# ADR-036: Startup Gate Aborts with exit 2 after 10s without First Calibrated UDP Frame

## Context

M5b's main loop entered immediately after `driver.open()`. If no UDCAP packets ever arrived (Windows host offline, firewall blocking UDP 9000, wrong static IP, UDCAP HandDriver not started), the binary spun at 100Hz forever, with `main.cpp:318` printing one "waiting for first packet..." then nothing further. From the operator's seat this was indistinguishable from "binary working, glove just not moving" — the worst class of UX failure for a teleoperation tool.

SPEC.md §5 startup sequence step (4) says "Wait for first valid UDP packet before moving" but does not specify what happens when "first valid" never arrives. M6 plan §0.1(b) closes that gap.

Three behaviors were considered:

- (A) **Wait forever** — keep the M5b status quo.
- (B) **Fail fast (e.g., 1s)** — abort almost immediately if no packet.
- (C) **Reasonable timeout, single config knob** — give legitimate cold-boot a chance, but fail closed.

Exit-code is a sub-decision: 0 (clean exit), 1 (generic error), or 2 (CLI/config error code already used by `main.cpp:217`).

## Decision

**Take (C): 10 seconds by default, configurable via `udcap.startup_timeout_s` in `config.yaml`. Gate failure (timeout OR signal-during-gate) returns `2`.** Implementation is a free function `wait_first_valid_frame(rx, timeout, shutdown_flag)` in `src/main.cpp` anonymous namespace, called between `driver.open()` and the main control loop (M6 plan §3.1).

The gate runs in **FULL and `--receiver-only` modes**; `--mock` skips (uses synthesized frames per M5b). When FULL-mode gate fails, the `std::optional<XHandDriver>` destructor still runs `shutdown()` (mode=0 + close_device, `src/xhand_driver.cpp:14-19`), so the hardware exit state is identical to a normal Ctrl+C.

## Consequences

- **正面**: Misconfiguration is now visible at startup, not silent during operation. The single `[ERROR] startup gate: ...` line is the unambiguous signal "your network / your UDCAP host is wrong".
- **正面**: Exit 2 matches the existing CLI-failure exit code (`main.cpp:217`) — shell scripts that wrap `udex_to_xhand` can treat "startup gate failure" the same as "bad arguments".
- **正面**: Single config knob in the same `udcap:` section as `timeout_ms`, so the operator finds it where they look for the watchdog threshold.
- **正面**: Gate inherits the same `shutdown_flag` as the main loop — `Ctrl+C` during the wait still triggers driver dtor cleanly (M6 plan §2.2 "关键不变式" #3).
- **负面**: Signal-during-gate exits 2 (same as timeout), which is slightly imprecise. A wrapper script that wants to distinguish "user cancelled" from "no UDCAP" must inspect log content. Accepted trade-off: gate failure is gate failure; the SIGINT case is uncommon and not actionable.
- **负面**: Receiver-only mode now also requires a calibrated frame within the gate. M5b receiver-only would happily print nothing for as long as the operator wanted. This is the consistency cost called out in plan §3.1 bullet — explicitly accepted.

## Alternatives Considered

- **(A) Wait forever**: Rejected — the M5b regression we're fixing.
- **(B) 1 second timeout**: Rejected — UDCAP HandDriver cold-start + static-IP negotiation can legitimately take several seconds on G1's network. 1s would break legitimate startups.
- **30 / 60 second timeout**: Rejected as too lenient for an operator-attended tool. 10s is the empirical sweet spot — typical UDCAP startup is well under, misconfiguration produces no packets ever (so 10s is enough).
- **CLI flag `--startup-timeout`**: Rejected — adds a CLI surface for a per-deployment value that belongs alongside `udcap.timeout_ms` (M6 plan §1.3, "不增 CLI flag").
- **Exit 0 on signal-during-gate**: Considered for shell-script ergonomics, rejected because gate has never produced useful work; exit 2 says "I didn't start cleanly" regardless of cause.
- **Different timeout for receiver-only**: Rejected — adds branching for negligible benefit; consistency wins.

## References

- SPEC.md §5 (startup sequence)
- CLAUDE.md "Safety (non-negotiable)" — "Startup: open device → verify IDs → set mode=3 → wait for first valid packet"
- `docs/plans/00-roadmap.md` §M6 (启动检查 line)
- `docs/plans/20260519-m6-safety-hardening-plan.md` §2.2, §3.1
- ADR-018 (mode=0 not powerless; driver dtor path)
- ADR-029 (CalibrationStatus pre-check on UDCAP side; gate inherits this filter via `UdcapReceiver::try_recv`)
