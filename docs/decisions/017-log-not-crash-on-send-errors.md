# ADR-017: Log But Don't Crash on send_command Errors

## Context

`send_command()` returns an `ErrorStruct` with `error_code` and `error_message`. In a 100Hz control loop, transient RS485 bus errors (collision, timeout, noise) are expected occasionally. The system must choose between crashing (safe but disruptive) and continuing (resilient but potentially masking real failures).

## Decision

If `send_command` returns `error_code != 0`, print a WARNING with the error message but do not raise or exit. The 100Hz loop continues with the next tick.

Persistent failure detection is deferred to M5 (watchdog/safety layer).

## Consequences

- **正面**: System survives transient bus glitches — important for teleoperation where a single dropped command is invisible but a crash requires full restart
- **正面**: Error messages are printed, so operator can observe failure patterns
- **负面**: A persistently broken bus will silently drop every command while printing warnings — no automatic recovery or escalation until M5
- **负面**: Warning prints at 100Hz could flood the terminal if failure is persistent

## Alternatives

- **Crash on first error**: Safe (system stops on any failure) but too fragile for real-time RS485 operation. A single bus collision would kill the teleop session.
- **Count consecutive errors, crash after N**: Good middle ground. Deferred to M5 watchdog — the driver layer should be simple, the safety layer handles escalation.
- **Silent drop (no print)**: Rejected. Hiding errors makes debugging impossible. At minimum, the operator should see that something is wrong.
