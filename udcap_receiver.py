"""UDCAP glove data receiver. Stub: reads from example.json. Real: UDP socket (M1)."""

import json
import os


class UdcapReceiver:
    def __init__(self, config: dict, mock: bool = False):
        self._mock = mock
        self._config = config
        self._mock_data = None

        if mock:
            example_path = os.path.join(os.path.dirname(__file__), "example.json")
            with open(example_path) as f:
                raw = json.load(f)
            self._mock_data = self._parse(raw)

    def receive(self) -> dict | None:
        """Return parsed hand data or None on error.

        Returns dict with keys:
            "left":  [float] * 24  (l0..l23, degrees)
            "right": [float] * 24  (r0..r23, degrees)
            "calib_left":  int
            "calib_right": int
        """
        if self._mock:
            return self._mock_data
        # Real UDP receive — implemented in M1
        return None

    @staticmethod
    def _parse(raw: dict) -> dict | None:
        """Parse raw UDCAP JSON into structured hand data."""
        # UDCAP wraps everything under a frame key (e.g. "1")
        frame_key = next(iter(raw))
        params = raw[frame_key].get("Parameter", [])

        # Build lookup: name → value
        lookup = {p["Name"]: p["Value"] for p in params}

        left = [float(lookup.get(f"l{i}", 0.0)) for i in range(24)]
        right = [float(lookup.get(f"r{i}", 0.0)) for i in range(24)]
        calib_left = int(lookup.get("L_CalibrationStatus", 0))
        calib_right = int(lookup.get("R_CalibrationStatus", 0))

        return {
            "left": left,
            "right": right,
            "calib_left": calib_left,
            "calib_right": calib_right,
        }


if __name__ == "__main__":
    receiver = UdcapReceiver({}, mock=True)
    data = receiver.receive()
    if data:
        print(f"Left  ({len(data['left'])} params): {data['left']}")
        print(f"Right ({len(data['right'])} params): {data['right']}")
        print(f"Calib: L={data['calib_left']} R={data['calib_right']}")
