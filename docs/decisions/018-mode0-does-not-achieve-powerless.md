# ADR-018: mode=0 Does Not Achieve Powerless State — Shutdown Is Best-Effort

## Context

The SDK example comments describe mode=0 as "powerless" (`0: powerless, 3: position (default), 5: powerful`). The plan specified `close()` should send mode=0 to make the hand go limp before calling `close_device()`.

During hardware testing (2026-04-27), mode=0 was sent successfully (no SDK error), but the hand retained damping/resistance and did not go limp. Testing with `time.sleep(1)` after the mode=0 command confirmed the behavior is not a timing issue — the hand simply does not become powerless with mode=0.

## Decision

Keep the mode=0 send in `close()` as best-effort. Do not attempt workarounds (kp=0, tor_max=0, etc.) without further investigation. The current shutdown path is: send mode=0 → sleep 100ms → close_device().

Investigation of the actual powerless mechanism is deferred. This is a known limitation.

## Consequences

- **正面**: Shutdown path is simple and doesn't rely on unverified workarounds
- **正面**: close_device() still releases the serial port cleanly
- **负面**: Hand retains holding force after shutdown. Operator must physically power off the hand or wait for the hand's own timeout
- **负面**: Ctrl+C during teleoperation won't make the hand go limp — it holds last position

## Alternatives

- **Try kp=0 + tor_max=0**: Might work, but untested. Sending zero PID gains to a servo could have unpredictable firmware behavior. Needs controlled testing before deploying.
- **Send palm preset before close**: Would move fingers to open position but still with active force. Doesn't solve the core problem.
- **Contact XHand vendor for correct powerless command**: Best long-term solution. The SDK documentation is sparse and the example code has known bugs (e.g., exam_close_device never calls close_device).
