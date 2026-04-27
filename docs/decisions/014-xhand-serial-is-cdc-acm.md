# ADR-014: XHand Serial Port Is CDC-ACM, Not ttyUSB

## Context

The plan and all documentation assumed the XHand RS485 adapter would appear as `/dev/ttyUSB0` (FTDI/CH340 chipset). During first hardware test (2026-04-27), the device was not found at `ttyUSB*`. Investigation revealed two CDC-ACM devices: `/dev/ttyACM0` and `/dev/ttyACM1`.

## Decision

Change the default serial port from `/dev/ttyUSB0` to `/dev/ttyACM0` in config.yaml, xhand_driver.py fallback, plan docs, and roadmap.

## Consequences

- **正面**: Hardware connection works out of the box with correct default
- **正面**: Future operators won't waste time debugging a missing ttyUSB0
- **负面**: If a different XHand unit uses a FTDI adapter, the default will be wrong — but config.yaml and `--port` CLI flag already support overriding

## Alternatives

- **Keep ttyUSB0 as default, document ACM0 separately**: Rejected. The default should match the actual hardware we have. Documenting a wrong default creates confusion.
- **Auto-detect via enumerate_devices("RS485")**: The SDK provides this, but it returns all serial ports. Not reliable enough to replace an explicit config value. Could be added later as a convenience.
