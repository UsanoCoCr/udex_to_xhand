"""XHand driver wrapper. Mock: no-op. Real: SDK RS485 commands."""

import math
import sys
import time


# Preset actions in degrees (from xhand_control_example.py)
PRESETS = {
    "fist": [11.85, 74.58, 40, -3.08, 106.02, 110, 109.75, 107.56, 107.66, 110, 109.1, 109.15],
    "palm": [0, 80.66, 33.2, 0.00, 5.11, 0, 6.53, 0, 6.76, 4.41, 10.13, 0],
    "v":    [38.32, 90, 52.08, 6.21, 2.6, 0, 2.1, 0, 110, 110, 110, 109.23],
    "ok":   [45.88, 41.54, 67.35, 2.22, 80.45, 70.82, 31.37, 10.39, 13.69, 16.88, 1.39, 10.55],
}


class XHandDriver:
    """XHand SDK wrapper. mock=True -> no-op; mock=False -> real RS485."""

    def __init__(self, config: dict, mock: bool = False):
        self._mock = mock
        self._config = config
        self._device = None
        self._sdk = None
        self._hand_map = {}   # {"left": int_id, "right": int_id}
        self._command = None  # template HandCommand_t

        if mock:
            return

        # --- Real mode: connect via SDK ---
        from xhand_controller import xhand_control
        self._sdk = xhand_control

        self._device = self._sdk.XHandControl()

        port = config.get("serial_port", "/dev/ttyUSB0")
        baud = int(config.get("baud_rate", 3000000))

        rsp = self._device.open_serial(port, baud)
        if rsp.error_code != 0:
            print(f"ERROR: open_serial failed: {rsp.error_message}")
            sys.exit(1)

        sdk_ver = self._device.get_sdk_version()
        print(f"SDK version: {sdk_ver}")
        print(f"Connected to {port} at {baud} baud")

        # Discover hands
        hand_ids = self._device.list_hands_id()
        if not hand_ids:
            print("ERROR: no hands found on bus")
            self._device.close_device()
            sys.exit(1)

        print(f"Hand IDs found: {list(hand_ids)}")
        for hid in hand_ids:
            err, htype = self._device.get_hand_type(hid)
            err_v, fw_ver = self._device.read_version(hid, 0)
            type_name = "Left" if htype == "L" else "Right"
            print(f"  hand_id={hid}: type={type_name}, firmware={fw_ver}")
            key = "left" if htype == "L" else "right"
            self._hand_map[key] = hid

        # Pre-build template command with config PID values
        kp = int(config.get("default_kp", 100))
        ki = int(config.get("default_ki", 0))
        kd = int(config.get("default_kd", 0))
        tor_max = int(config.get("default_tor_max", 300))
        mode = int(config.get("control_mode", 3))

        self._command = self._sdk.HandCommand_t()
        for i in range(12):
            self._command.finger_command[i].id = i
            self._command.finger_command[i].kp = kp
            self._command.finger_command[i].ki = ki
            self._command.finger_command[i].kd = kd
            self._command.finger_command[i].position = 0.0
            self._command.finger_command[i].tor_max = tor_max
            self._command.finger_command[i].mode = mode

    def send(self, hand: str, joints: list[float]) -> None:
        """Send 12 joint positions (radians) to one hand."""
        if self._mock:
            return

        if hand not in self._hand_map:
            raise RuntimeError(
                f"No {hand} hand discovered (available: {list(self._hand_map.keys())})"
            )

        hid = self._hand_map[hand]
        for i in range(12):
            self._command.finger_command[i].position = joints[i]

        rsp = self._device.send_command(hid, self._command)
        if rsp.error_code != 0:
            print(f"WARNING: send_command({hand}): {rsp.error_message}")

    def close(self) -> None:
        """Shutdown: mode=0 (passive) for all hands, then close serial."""
        if self._mock or self._device is None:
            return

        # Send mode=0 to all discovered hands
        for i in range(12):
            self._command.finger_command[i].mode = 0
        for hand, hid in self._hand_map.items():
            self._device.send_command(hid, self._command)
        print("Shutdown: mode=0 (passive)")

        self._device.close_device()
        self._device = None
        print("Device closed.")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="XHand driver")
    parser.add_argument("--port",
                        help="Serial port (required for real mode)")
    parser.add_argument("--baud", type=int, default=3000000,
                        help="Baud rate (default: 3000000)")
    parser.add_argument("--mock", action="store_true",
                        help="Use stub mode (no hardware)")
    parser.add_argument("--discover", action="store_true",
                        help="Connect, print device info, exit (no motion)")
    parser.add_argument("--action", choices=list(PRESETS.keys()),
                        help="Execute one preset: fist|palm|v|ok")
    parser.add_argument("--actions",
                        help="Execute multiple presets, comma-separated")
    parser.add_argument("--hold", type=float, default=1.0,
                        help="Hold time per action in seconds (default: 1.0)")
    args = parser.parse_args()

    # Validate
    if args.action and args.actions:
        parser.error("--action and --actions are mutually exclusive")

    if not args.mock and not args.port:
        parser.error("--port is required for real mode (or use --mock)")

    # Build action list
    action_list = []
    if args.actions:
        for name in args.actions.split(","):
            name = name.strip()
            if name not in PRESETS:
                parser.error(f"Unknown preset: {name}")
            action_list.append(name)
    elif args.action:
        action_list = [args.action]

    # --- Mock mode: print radians (M0-compatible output) ---
    if args.mock:
        if not action_list:
            action_list = ["fist"]
        for name in action_list:
            degrees = PRESETS[name]
            radians = [d * math.pi / 180.0 for d in degrees]
            print(f"Action '{name}' (12 joints, radians):")
            print(f"  {[round(r, 4) for r in radians]}")
        print("Done.")
        sys.exit(0)

    # --- Real mode ---
    config = {"serial_port": args.port, "baud_rate": args.baud}
    driver = XHandDriver(config, mock=False)

    if args.discover:
        driver.close()
        sys.exit(0)

    if not action_list:
        print("No action specified. Use --action or --actions, or --discover.")
        driver.close()
        sys.exit(0)

    try:
        for name in action_list:
            degrees = PRESETS[name]
            radians = [d * math.pi / 180.0 for d in degrees]
            for hand in driver._hand_map:
                driver.send(hand, radians)
            print(f"Action {name}: sent 12 joints, OK")
            time.sleep(args.hold)
    except KeyboardInterrupt:
        pass

    driver.close()
