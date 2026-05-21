"""Joint mapping: UDCAP 24 DOF → XHand 12 DOF per hand.

Config-driven: weighted sum of UDCAP sources, sign flip, offset,
degree-domain clamping, deg→rad conversion. All params from config.yaml.
"""

import json
import math
import os

_DEG2RAD = math.pi / 180.0

# XHand joint order J0-J11 — maps config key names to output indices.
JOINT_ORDER = (
    "thumb_bend", "thumb_rota1", "thumb_rota2",
    "index_bend", "index_joint1", "index_joint2",
    "mid_joint1", "mid_joint2",
    "ring_joint1", "ring_joint2",
    "pinky_joint1", "pinky_joint2",
)


class JointMapper:
    def __init__(self, config: dict):
        # config is the mapping section:
        #   {"use_new_retarget": bool, "left": {...}, "right": {...}}
        # M8a Step A.0: must read the master switch first so it does not
        # leak into the per-hand iteration below. Required field — explicit
        # `false` is the M7-baseline setting; missing key raises so a config
        # regression can never silently fall back to M7.
        if "use_new_retarget" not in config:
            raise KeyError(
                "mapping.use_new_retarget required "
                "(set to false to keep M7 baseline; true enables M8 retarget pipeline)"
            )
        self.use_new_retarget: bool = bool(config["use_new_retarget"])

        # Pre-build specs per hand for fast hot-path access.
        # Each spec: (sources, weights, sign, offset, clamp_lo, clamp_hi)
        self._specs: dict[str, list[tuple]] = {}

        for hand, hand_cfg in config.items():
            if hand == "use_new_retarget":
                continue  # already consumed above
            specs = []
            for i, joint_name in enumerate(JOINT_ORDER):
                if joint_name not in hand_cfg:
                    raise KeyError(
                        f"Missing joint '{joint_name}' (J{i}) in mapping.{hand}. "
                        f"Expected keys: {list(JOINT_ORDER)}"
                    )
                entry = hand_cfg[joint_name]
                sources = entry["sources"]
                weights = entry["weights"]
                sign = entry["sign"]
                offset = entry.get("offset", 0.0)
                clamp_range = entry["clamp"]

                if len(sources) != len(weights):
                    raise ValueError(
                        f"mapping.{hand}.{joint_name}: "
                        f"len(sources)={len(sources)} != len(weights)={len(weights)}"
                    )
                for s in sources:
                    if not 0 <= s <= 23:
                        raise ValueError(
                            f"mapping.{hand}.{joint_name}: "
                            f"source index {s} out of range [0, 23]"
                        )

                # M8b Step B.4: optional affine rescale fields. Both-or-none
                # schema check happens regardless of use_new_retarget so a
                # config-only typo still fails fast. Flag-gating then strips
                # both entries when use_new_retarget=False so the M7 path is
                # bit-identical, matching joint_mapper.cpp load_hand.
                input_range = entry.get("input_range")
                output_range = entry.get("output_range")
                if (input_range is None) != (output_range is None):
                    raise ValueError(
                        f"mapping.{hand}.{joint_name}: "
                        f"input_range and output_range must be both present or both absent"
                    )
                if input_range is not None:
                    if len(input_range) != 2:
                        raise ValueError(
                            f"mapping.{hand}.{joint_name}.input_range "
                            f"must have exactly 2 elements"
                        )
                    if len(output_range) != 2:
                        raise ValueError(
                            f"mapping.{hand}.{joint_name}.output_range "
                            f"must have exactly 2 elements"
                        )
                if not self.use_new_retarget:
                    input_range = None
                    output_range = None
                in_min  = float(input_range[0])  if input_range  is not None else None
                in_max  = float(input_range[1])  if input_range  is not None else None
                out_min = float(output_range[0]) if output_range is not None else None
                out_max = float(output_range[1]) if output_range is not None else None

                specs.append((
                    tuple(sources),
                    tuple(weights),
                    sign,
                    float(offset),
                    float(clamp_range[0]),
                    float(clamp_range[1]),
                    in_min,
                    in_max,
                    out_min,
                    out_max,
                ))
            self._specs[hand] = specs

    def map(self, hand: str, udcap_24: list[float]) -> list[float]:
        """Map 24 UDCAP params to 12 XHand joint positions in radians."""
        if hand not in self._specs:
            raise KeyError(
                f"No mapping for hand '{hand}' "
                f"(available: {list(self._specs.keys())})"
            )
        specs = self._specs[hand]
        result = []
        for (sources, weights, sign, offset, lo, hi,
             in_min, in_max, out_min, out_max) in specs:
            wsum = 0.0
            for s, w in zip(sources, weights):
                wsum += w * udcap_24[s]
            deg = sign * wsum + offset
            # M8b Step B.4: optional affine rescale, identical arithmetic
            # order to joint_mapper.cpp::apply_one. When use_new_retarget
            # was False the loader already nulled the bounds → branch skipped
            # and output is bit-identical to the M7 path.
            if in_min is not None:
                span = in_max - in_min
                if span > 1e-9:
                    ratio = (deg - in_min) / span
                    deg = ratio * (out_max - out_min) + out_min
                else:
                    deg = (out_min + out_max) * 0.5
            deg = max(lo, min(hi, deg))
            result.append(deg * _DEG2RAD)
        return result


if __name__ == "__main__":
    import yaml

    # Moved to legacy_python/ in M5b; config.yaml + example.json stay at repo root.
    _ROOT = os.path.join(os.path.dirname(__file__), "..")
    config_path = os.path.join(_ROOT, "config.yaml")
    with open(config_path) as f:
        config = yaml.safe_load(f)

    mapper = JointMapper(config.get("mapping", {}))

    example_path = os.path.join(_ROOT, "example.json")
    with open(example_path) as f:
        raw = json.load(f)
    frame_key = next(iter(raw))
    params = raw[frame_key]["Parameter"]
    lookup = {p["Name"]: p["Value"] for p in params}
    left_24 = [float(lookup.get(f"l{i}", 0.0)) for i in range(24)]

    print(f"Input (24, deg): {left_24}")
    result = mapper.map("left", left_24)
    print(f"Output (12, rad): {[round(v, 4) for v in result]}")
    print()
    for i, name in enumerate(JOINT_ORDER):
        print(f"  J{i:2d} {name:16s} = {result[i]:+.4f} rad  ({result[i] / _DEG2RAD:+.2f} deg)")
