"""XHand driver wrapper. Stub: prints joint values. Real: SDK RS485 commands (M3)."""

import math


# Preset actions in degrees (from xhand_control_example.py)
PRESETS = {
    "fist": [11.85, 74.58, 40, -3.08, 106.02, 110, 109.75, 107.56, 107.66, 110, 109.1, 109.15],
    "palm": [0, 80.66, 33.2, 0.00, 5.11, 0, 6.53, 0, 6.76, 4.41, 10.13, 0],
    "v":    [38.32, 90, 52.08, 6.21, 2.6, 0, 2.1, 0, 110, 110, 110, 109.23],
    "ok":   [45.88, 41.54, 67.35, 2.22, 80.45, 70.82, 31.37, 10.39, 13.69, 16.88, 1.39, 10.55],
}


class XHandDriver:
    def __init__(self, config: dict, mock: bool = False):
        self._mock = mock
        self._config = config

    def send(self, hand_id: str, joints: list[float]) -> None:
        """Send 12 joint positions (radians) to one hand.

        Stub: no-op (main.py handles printing). Real: SDK send_command (M3).
        """
        pass

    def close(self) -> None:
        """Release hardware resources. Stub: no-op."""
        pass


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="XHand driver stub")
    parser.add_argument("--mock", action="store_true", default=True)
    parser.add_argument("--action", choices=list(PRESETS.keys()), default="fist")
    args = parser.parse_args()

    driver = XHandDriver({}, mock=args.mock)
    degrees = PRESETS[args.action]
    radians = [d * math.pi / 180.0 for d in degrees]
    print(f"Action '{args.action}' (12 joints, radians):")
    print(f"  {[round(r, 4) for r in radians]}")
    driver.send("left", radians)
    driver.close()
    print("Done.")
