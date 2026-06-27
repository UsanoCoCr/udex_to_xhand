#!/usr/bin/env python3
"""Offline dev helper: stream LeRobot 38-dim `action` frames over UDP/JSON to the
`lerobot_to_xhand` C++ bridge (default 127.0.0.1:9100).

This is a *producer* process — exactly the role UDCAP plays for the original path.
It is NOT part of the realtime control loop (that stays pure C++ on PC2); it only
does file I/O + socket send, which is the allowed "Python for offline scripts" role.

Two modes:

  1. Synthetic toggle (no data file) — alternates both hands open/close every
     --period seconds. Use to smoke-test the UDP link and `--dry-run`:

         python scripts/lerobot_udp_test_sender.py --fps 30 --period 2

  2. Parquet replay — streams a real recorded episode's `action` column row by row
     at --fps, so the C++ bridge plays it back on the XHand in realtime. Requires
     pyarrow (offline only):

         python scripts/lerobot_udp_test_sender.py \
             --parquet data_stepit/data/chunk-003/episode_003370.parquet --fps 30

The wire format matches lerobot_receiver.cpp shape 1:  {"action": [...38 floats...]}
Only indices 36 (left_hand_closed) and 37 (right_hand_closed) are read by the C++
side; the rest are sent as-is (zeros in synthetic mode).
"""

import argparse
import json
import socket
import sys
import time

LEFT_CLOSED_IDX = 36
RIGHT_CLOSED_IDX = 37
ACTION_LEN = 38


def make_action(left_closed: int, right_closed: int):
    """Build a minimal valid 38-dim action with only the two hand bits set."""
    action = [0.0] * ACTION_LEN
    action[LEFT_CLOSED_IDX] = float(left_closed)
    action[RIGHT_CLOSED_IDX] = float(right_closed)
    return action


def iter_synthetic(fps: float, period: float):
    """Yield (action) frames toggling both hands open/close every `period` sec."""
    frames_per_state = max(1, int(round(fps * period)))
    closed = 0
    while True:
        for _ in range(frames_per_state):
            yield make_action(closed, closed)
        closed ^= 1


def iter_parquet(path: str):
    """Yield each row's 38-dim `action` list from a LeRobot episode parquet."""
    try:
        import pyarrow.parquet as pq
    except ImportError:
        sys.exit("pyarrow required for --parquet mode: pip install pyarrow")
    table = pq.read_table(path, columns=["action"])
    for row in table.column("action").to_pylist():
        yield list(row)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1", help="target host (default 127.0.0.1)")
    ap.add_argument("--port", type=int, default=9100, help="target UDP port (default 9100)")
    ap.add_argument("--fps", type=float, default=30.0, help="send rate (default 30)")
    ap.add_argument("--parquet", default=None,
                    help="replay this episode parquet's action column (else synthetic toggle)")
    ap.add_argument("--period", type=float, default=2.0,
                    help="synthetic mode: seconds per open/close state (default 2)")
    ap.add_argument("--loop", action="store_true",
                    help="parquet mode: loop the episode forever")
    ap.add_argument("--duration", type=float, default=0.0,
                    help="stop after N seconds (0 = run until Ctrl+C / end of data)")
    args = ap.parse_args()

    if args.fps <= 0:
        sys.exit("--fps must be > 0")
    dt = 1.0 / args.fps

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = (args.host, args.port)

    def frame_source():
        if args.parquet:
            while True:
                yield from iter_parquet(args.parquet)
                if not args.loop:
                    return
        else:
            yield from iter_synthetic(args.fps, args.period)

    mode = f"parquet={args.parquet}" if args.parquet else f"synthetic period={args.period}s"
    print(f"[sender] -> {args.host}:{args.port}  fps={args.fps}  {mode}", flush=True)

    start = time.monotonic()
    next_t = start
    sent = 0
    last_decision = None
    try:
        for action in frame_source():
            if len(action) < ACTION_LEN:
                sys.exit(f"action has {len(action)} dims, need >= {ACTION_LEN}")
            sock.sendto(json.dumps({"action": action}).encode("utf-8"), addr)
            sent += 1

            decision = (action[LEFT_CLOSED_IDX] > 0.5, action[RIGHT_CLOSED_IDX] > 0.5)
            if decision != last_decision:
                print(f"[sender] frame {sent}: L={'CLOSE' if decision[0] else 'open '}"
                      f"  R={'CLOSE' if decision[1] else 'open '}", flush=True)
                last_decision = decision

            if args.duration > 0 and (time.monotonic() - start) >= args.duration:
                break

            next_t += dt
            sleep = next_t - time.monotonic()
            if sleep > 0:
                time.sleep(sleep)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
    print(f"[sender] done, sent {sent} frames", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
