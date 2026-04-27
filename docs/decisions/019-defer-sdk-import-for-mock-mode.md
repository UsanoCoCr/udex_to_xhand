# ADR-019: Defer SDK Import to Real-Mode Init for Cross-Platform Development

## Context

The XHand Python SDK (`xhand_controller`) is a compiled C extension (`.whl`) that only supports Linux + Python 3.10. Development and testing of the pipeline (UDP receiver, joint mapper, safety, main loop) happens on macOS. If `xhand_driver.py` imports the SDK at module level, `import xhand_driver` fails on macOS, breaking mock mode and `main.py --mock`.

## Decision

Move `from xhand_controller import xhand_control` inside `__init__`, after the `if mock: return` guard. Mock mode never touches the SDK. The import only executes when real hardware mode is requested.

## Consequences

- **正面**: `python main.py --mock` works on macOS without the SDK installed — enables full pipeline development/testing on any platform
- **正面**: `python xhand_driver.py --mock --action fist` works anywhere for quick radian value checks
- **负面**: Import errors (SDK not installed, wrong Python version) only surface at runtime when real mode is first used, not at startup
- **负面**: IDE type hints and autocomplete for SDK types don't work in the file (minor DX issue)

## Alternatives

- **Top-level import with try/except**: `try: from xhand_controller ... except ImportError: xhand_control = None`. Allows module-level import but requires None-checks throughout real mode code. More error-prone.
- **Separate files for mock and real driver**: `xhand_driver_mock.py` + `xhand_driver_real.py`. Violates the plan's "zero new files" constraint and duplicates the interface. Unnecessary complexity for a single `if mock` guard.
