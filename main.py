"""UDCAP → XHand teleoperation main loop.

Usage:
    python main.py --mock --duration 3
    python main.py --config config.yaml --hand left
"""

import argparse
import time

import yaml

from joint_mapper import JointMapper
from safety import STUB_LIMITS, clamp
from udcap_receiver import UdcapReceiver
from xhand_driver import XHandDriver


def fmt_joints(values: list[float]) -> str:
    return "[" + ", ".join(f"{v:+.3f}" for v in values) + "]"


def main():
    parser = argparse.ArgumentParser(description="UDCAP → XHand teleoperation")
    parser.add_argument("--config", default="config.yaml", help="Path to config file")
    parser.add_argument("--mock", action="store_true", help="Use stubs instead of real hardware")
    parser.add_argument("--duration", type=float, default=None, help="Run for N seconds then exit")
    parser.add_argument("--hand", choices=["left", "right", "both"], default="both")
    args = parser.parse_args()

    with open(args.config) as f:
        config = yaml.safe_load(f)

    receiver = UdcapReceiver(config.get("udcap", {}), mock=args.mock)
    mapper = JointMapper(config.get("mapping", {}))
    driver = XHandDriver(config.get("xhand", {}), mock=args.mock)

    rate_hz = config.get("xhand", {}).get("update_rate_hz", 100)
    interval = 1.0 / rate_hz

    tick = 0
    t_start = time.monotonic()

    try:
        while True:
            t_loop = time.monotonic()
            tick += 1

            data = receiver.receive()
            if data is not None:
                parts = []

                if args.hand in ("left", "both"):
                    left_12 = mapper.map("left", data["left"])
                    left_12 = clamp(left_12, STUB_LIMITS)
                    driver.send("left", left_12)
                    parts.append(f"L: {fmt_joints(left_12)}")

                if args.hand in ("right", "both"):
                    right_12 = mapper.map("right", data["right"])
                    right_12 = clamp(right_12, STUB_LIMITS)
                    driver.send("right", right_12)
                    parts.append(f"R: {fmt_joints(right_12)}")

                print(f"[tick {tick:03d}] {' '.join(parts)}")

            if args.duration and (t_loop - t_start) >= args.duration:
                break

            elapsed = time.monotonic() - t_loop
            sleep_time = interval - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    except KeyboardInterrupt:
        pass

    elapsed = time.monotonic() - t_start
    print(f"Exited after {elapsed:.1f}s, {tick} ticks")
    receiver.close()
    driver.close()


if __name__ == "__main__":
    main()
