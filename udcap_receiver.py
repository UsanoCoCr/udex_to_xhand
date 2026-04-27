"""UDCAP glove data receiver.

Mock mode: reads from example.json (M0).
Real mode: non-blocking UDP socket, drain-to-latest (M1).
"""

import argparse
import json
import os
import socket
import time


class UdcapReceiver:
    def __init__(self, config: dict, mock: bool = False):
        self._mock = mock
        self._config = config
        self._mock_data = None
        self._sock = None
        self._last_addr = None

        if mock:
            example_path = os.path.join(os.path.dirname(__file__), "example.json")
            with open(example_path) as f:
                raw = json.load(f)
            self._mock_data = self._parse(raw)
        else:
            host = config.get("host", "0.0.0.0")
            port = config.get("port", 9000)
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.bind((host, port))
            self._sock.setblocking(False)

    def receive(self) -> dict | None:
        """Return parsed hand data or None on error / no data.

        Returns dict with keys:
            "left":  [float] * 24  (l0..l23, degrees)
            "right": [float] * 24  (r0..r23, degrees)
            "calib_left":  int
            "calib_right": int
        """
        if self._mock:
            return self._mock_data

        # Drain socket buffer, keep only the latest packet
        latest_raw = None
        while True:
            try:
                data, addr = self._sock.recvfrom(65535)
                self._last_addr = addr[0]
                latest_raw = data
            except BlockingIOError:
                break

        if latest_raw is None:
            return None

        try:
            raw = json.loads(latest_raw.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return None

        return self._parse(raw)

    @staticmethod
    def _parse(raw: dict) -> dict | None:
        """Parse raw UDCAP JSON into structured hand data."""
        try:
            frame_key = next(iter(raw))
            params = raw[frame_key].get("Parameter", [])
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
        except (StopIteration, KeyError, TypeError, ValueError, AttributeError):
            return None

    @property
    def last_addr(self) -> str | None:
        """Last-seen source IP address (None if no packets received yet)."""
        return self._last_addr

    def close(self) -> None:
        """Release the UDP socket."""
        if self._sock is not None:
            self._sock.close()
            self._sock = None


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDCAP UDP receiver (standalone)")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address")
    parser.add_argument("--port", type=int, default=9000, help="UDP port")
    parser.add_argument("--duration", type=float, default=None, help="Run for N seconds")
    parser.add_argument("--mock", action="store_true", help="Use example.json instead of UDP")
    args = parser.parse_args()

    config = {"host": args.host, "port": args.port}
    receiver = UdcapReceiver(config, mock=args.mock)

    if not args.mock:
        print(f"Listening on UDP {args.host}:{args.port}...")

    frame_count = 0
    t_start = time.monotonic()
    t_last_print = t_start

    try:
        while True:
            data = receiver.receive()
            if data is None:
                if not args.mock:
                    time.sleep(0.001)
                continue

            frame_count += 1
            now = time.monotonic()
            elapsed = now - t_start
            fps = frame_count / elapsed if elapsed > 0 else 0.0

            addr_str = f"[{receiver.last_addr}]" if receiver.last_addr else "[mock]"

            # Print at ~10 Hz to avoid terminal flood
            if now - t_last_print >= 0.1:
                left_str = " ".join(f"l{i}={data['left'][i]:.1f}" for i in range(24))
                right_str = " ".join(f"r{i}={data['right'][i]:.1f}" for i in range(24))
                print(f"{addr_str} L: {left_str}")
                print(f"{addr_str} R: {right_str}")
                print(f"CalibStatus: L={data['calib_left']} R={data['calib_right']} | FPS: {fps:.1f}")
                print()
                t_last_print = now

            if args.duration and elapsed >= args.duration:
                break

            if args.mock:
                time.sleep(0.01)

    except KeyboardInterrupt:
        pass
    finally:
        receiver.close()
        elapsed = time.monotonic() - t_start
        fps = frame_count / elapsed if elapsed > 0 else 0.0
        print(f"\nReceived {frame_count} frames in {elapsed:.1f}s (avg {fps:.1f} FPS)")
