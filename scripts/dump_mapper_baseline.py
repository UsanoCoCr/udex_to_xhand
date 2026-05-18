#!/usr/bin/env python3
"""Generate snapshot baseline for tests/test_mapper_snapshot.cpp.

Reads example.json + config.yaml, runs the Python joint_mapper.py (M4 baseline),
writes tests/fixtures/mapper_baseline.json with SHA-256 of both source files so
the C++ test can detect silent drift.

Run once on dev Mac (any host with the Python prototype + pyyaml available).
Output is committed.

Usage:
    python3 scripts/dump_mapper_baseline.py \
        --example example.json \
        --config  config.yaml \
        --out     tests/fixtures/mapper_baseline.json
"""

import argparse
import datetime
import hashlib
import json
import os
import sys
from typing import List, Tuple

# Import joint_mapper from legacy_python/ (post-M5b reorg).
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
LEGACY_DIR = os.path.join(REPO_ROOT, "legacy_python")
sys.path.insert(0, LEGACY_DIR)

import yaml  # noqa: E402
from joint_mapper import JOINT_ORDER, JointMapper  # noqa: E402


def sha256_hex(path: str) -> str:
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def load_udcap_frame(example_path: str) -> Tuple[List[float], List[float]]:
    with open(example_path) as f:
        raw = json.load(f)
    frame_key = next(iter(raw))
    params = raw[frame_key]["Parameter"]
    lookup = {p["Name"]: p["Value"] for p in params}
    left = [float(lookup.get(f"l{i}", 0.0)) for i in range(24)]
    right = [float(lookup.get(f"r{i}", 0.0)) for i in range(24)]
    return left, right


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--example", required=True, help="Path to example.json")
    parser.add_argument("--config", required=True, help="Path to config.yaml")
    parser.add_argument("--out", required=True, help="Output fixture path")
    args = parser.parse_args()

    with open(args.config) as f:
        config = yaml.safe_load(f)
    mapper = JointMapper(config.get("mapping", {}))

    left_24, right_24 = load_udcap_frame(args.example)
    left_12 = mapper.map("left", left_24)
    right_12 = mapper.map("right", right_24)

    fixture = {
        "source": {
            "example_json_sha256": sha256_hex(args.example),
            "config_yaml_sha256": sha256_hex(args.config),
            "python_version": "{}.{}.{}".format(*sys.version_info[:3]),
            "generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(
                timespec="seconds"
            ),
            "joint_order": list(JOINT_ORDER),
        },
        "left": left_12,
        "right": right_12,
    }

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(fixture, f, indent=2)
    print(f"Wrote {args.out}")
    print(f"  example_json_sha256 = {fixture['source']['example_json_sha256']}")
    print(f"  config_yaml_sha256  = {fixture['source']['config_yaml_sha256']}")
    print(f"  left  = {[round(v, 6) for v in left_12]}")
    print(f"  right = {[round(v, 6) for v in right_12]}")


if __name__ == "__main__":
    main()
