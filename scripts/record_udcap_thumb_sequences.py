#!/usr/bin/env python3
"""M8c Step C.1 — record raw UDCAP frames to JSONL for offline thumb prototyping.

Listens on UDP <port>, parses each packet's `Parameter` block into 24 left +
24 right floats (degrees, UDCAP convention) plus the L_/R_CalibrationStatus
ints, writes one JSON object per line to the output file.

Pure stdlib (socket + json) so it works equally on the dev Mac and PC2 —
no extra deps. Frames where the packet fails to parse, or whose calibration
status is < 3 by default, are dropped (controlled via --require-calib).

Typical usage (PC2, with Windows UDCAP HandDriver pushing to PC2:9000):

    python3 scripts/record_udcap_thumb_sequences.py \
        --duration 60 \
        --out docs/logs/m8c-thumb-recording-2026-05-21.jsonl

Operator script during the 60 s window (recommended for M8c prototype):
    0-15 s   palm flat open (neutral)
    15-30 s  thumb tip → index tip pinch
    30-45 s  thumb opposition sweep (tip → mid / ring / pinky)
    45-60 s  full fist

JSONL row schema (compact, decoder-friendly):
    {
      "ts": float   # seconds since recording start (monotonic)
      "wall": str   # ISO8601 UTC for human reading
      "left":  [24 floats]   # l0..l23 in degrees
      "right": [24 floats]   # r0..r23 in degrees
      "calib_l": int         # 0..3 (UDCAP)
      "calib_r": int
      "sender": str          # "ip:port"
    }
"""

import argparse
import datetime
import json
import socket
import sys
import time


def _parse_packet(payload: bytes) -> dict | None:
    """Return a dict with left/right/calib fields, or None on parse failure."""
    try:
        raw = json.loads(payload.decode("utf-8"))
    except Exception:
        return None
    # UDCAP wraps the actual payload under a single integer-keyed frame id
    # (see example.json + udcap_receiver.cpp). The shape is {<id>: {...}}.
    if not isinstance(raw, dict) or not raw:
        return None
    frame_key = next(iter(raw))
    frame = raw[frame_key]
    if not isinstance(frame, dict):
        return None
    params = frame.get("Parameter")
    if not isinstance(params, list):
        return None
    by_name = {p["Name"]: p["Value"] for p in params if "Name" in p and "Value" in p}
    try:
        left = [float(by_name.get(f"l{i}", 0.0)) for i in range(24)]
        right = [float(by_name.get(f"r{i}", 0.0)) for i in range(24)]
        calib_l = int(by_name.get("L_CalibrationStatus", 0))
        calib_r = int(by_name.get("R_CalibrationStatus", 0))
    except (TypeError, ValueError):
        return None
    return {
        "left": left,
        "right": right,
        "calib_l": calib_l,
        "calib_r": calib_r,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", default="0.0.0.0",
                        help="Bind host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=9000,
                        help="Bind port (default: 9000)")
    parser.add_argument("--duration", type=float, default=60.0,
                        help="Recording window in seconds (default: 60)")
    parser.add_argument("--out", required=True,
                        help="Output JSONL path (one frame per line)")
    parser.add_argument("--require-calib", action="store_true",
                        help="Drop frames with calib_l != 3 or calib_r != 3 "
                             "(default: keep everything so the prototype can "
                             "see warm-up frames too)")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind((args.host, args.port))
    except OSError as e:
        print(f"ERROR: bind {args.host}:{args.port}: {e}", file=sys.stderr)
        return 2
    sock.settimeout(0.5)

    written = 0
    parse_errors = 0
    dropped_calib = 0
    t0 = time.monotonic()
    deadline = t0 + args.duration

    print(f"Listening on {args.host}:{args.port} for {args.duration:.1f}s; "
          f"writing to {args.out}", file=sys.stderr)
    with open(args.out, "w", buffering=1) as f:   # line-buffered
        try:
            while True:
                now = time.monotonic()
                if now >= deadline:
                    break
                try:
                    payload, sender = sock.recvfrom(65536)
                except socket.timeout:
                    continue
                parsed = _parse_packet(payload)
                if parsed is None:
                    parse_errors += 1
                    continue
                if args.require_calib and (
                    parsed["calib_l"] != 3 or parsed["calib_r"] != 3
                ):
                    dropped_calib += 1
                    continue
                row = {
                    "ts": now - t0,
                    "wall": datetime.datetime.now(datetime.timezone.utc)
                                       .isoformat(timespec="milliseconds"),
                    "sender": f"{sender[0]}:{sender[1]}",
                    **parsed,
                }
                f.write(json.dumps(row, separators=(",", ":")) + "\n")
                written += 1
        except KeyboardInterrupt:
            print("interrupted by user; flushing remainder", file=sys.stderr)

    print(f"Wrote {written} frames in {time.monotonic()-t0:.1f}s  "
          f"(parse_errors={parse_errors}  dropped_calib={dropped_calib})",
          file=sys.stderr)
    if written == 0:
        print("ERROR: 0 frames captured — check UDCAP HandDriver target IP/port",
              file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
