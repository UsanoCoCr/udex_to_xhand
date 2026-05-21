#!/usr/bin/env python3
"""M8c Step C.3 — offline prototype for thumb retargeting algorithm selection.

Reads a JSONL recording produced by `scripts/record_udcap_thumb_sequences.py`,
applies one of three candidate algorithms to the thumb-related UDCAP sources
(l0/l1/l2/l3/l20 for left; r0/r1/r2/r3/r20 for right), and produces a 4-panel
matplotlib PNG (or CSV fallback) of the resulting XHand thumb commands over
time.

Three candidates (see docs/plans/20260521-m8-tuning-acceptance-plan.md §0 / §3 M8c):

  A. Per-joint affine + zero offset (M8b schema extended to thumb). Smallest
     code change — every thumb joint is just `affine_rescale(weighted_sum +
     offset, input_range, output_range)`. input_range is captured from the
     recording itself per source-combination (mirroring what
     --actions calibrate-udcap would produce).

  B. Coupled affine — thumb_rota1 and thumb_rota2 each take a *signed*
     weighted sum of l3 (MCP Yaw) and l20 (MCP Roll). Equivalent to applying
     a 2x2 linear transform from UDCAP opposition space to XHand opposition
     space. Already supported by the current weighted-sum schema (weights may
     be negative); only config.yaml changes needed.

  C. Tip-direction reprojection — interprets (l2, l3, l20) as MCP Euler
     angles, builds the rotation matrix, extracts the thumb tip unit vector,
     then maps that vector to (rota1, rota2, bend) via an analytic heuristic.
     A proper version needs the XHand thumb URDF and an IK solver (scipy);
     this prototype ships the *direction-vector* portion only and uses a
     heuristic projection so we can still compare its qualitative response
     against A/B. Document any selection of C in ADR-049 + plan §M8c-extended.

Opposition score per frame (panel 4) is defined as the L2 norm of the
output (rota1, rota2) pair, normalised to its own range so the three
algorithms are visually comparable.

Usage:
    python3 scripts/thumb_retarget_prototype.py \
        --recording docs/logs/m8c-thumb-recording-2026-05-21.jsonl \
        --algo A \
        --plot scripts/thumb-algo-A.png \
        --hand left

Falls back to a CSV next to --plot (replacing the .png suffix) when
matplotlib is not installed — keeps the script viable on any dev Mac without
needing `brew install` of binary wheels.
"""

import argparse
import json
import math
import os
import sys
from typing import Dict, List, Optional, Tuple

# XHand output ranges (degrees, pre-clamp). Sourced from config.yaml clamp[]
# entries for the M7 baseline so the prototype's output magnitude is
# comparable to what M7 would produce on the same recording.
XHAND_THUMB_RANGE_DEG: Dict[str, Tuple[float, float]] = {
    "thumb_bend":  (-10.0, 110.0),
    "thumb_rota1": (-10.0, 110.0),
    "thumb_rota2": (  0.0,  50.0),
}

# UDCAP source indices for thumb signals (same for left and right; the
# recording stores them separately as l0..l23 / r0..r23).
THUMB_SOURCES = {
    "DIP":   0,
    "PIP":   1,
    "MCP_P": 2,
    "MCP_Y": 3,
    "MCP_R": 20,
}


def _affine(value: float, in_lo: float, in_hi: float,
            out_lo: float, out_hi: float) -> float:
    """Same arithmetic order as joint_mapper.cpp::apply_one + the Python
    oracle so the prototype's output matches what the realtime mapper
    would produce given identical input_range / output_range."""
    span = in_hi - in_lo
    if span <= 1e-9:
        return (out_lo + out_hi) * 0.5
    ratio = (value - in_lo) / span
    return ratio * (out_hi - out_lo) + out_lo


def _clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


# ----------------------------------------------------------------------------
# Algorithm A — per-joint affine. Mirrors the M8b schema extension applied to
# thumb_bend / thumb_rota1 / thumb_rota2.
# ----------------------------------------------------------------------------

def _algo_a(thumb: Dict[str, float],
            in_range: Dict[str, Tuple[float, float]]) -> Dict[str, float]:
    # thumb_bend ← 0.3*DIP + 0.3*PIP + 0.4*MCP_P  with sign=-1, offset=0
    # (same weighted sum config.yaml currently uses).
    wsum = (0.3 * thumb["DIP"] + 0.3 * thumb["PIP"] + 0.4 * thumb["MCP_P"])
    deg_bend = -1.0 * wsum + 0.0
    deg_bend = _affine(deg_bend, *in_range["thumb_bend"], *XHAND_THUMB_RANGE_DEG["thumb_bend"])

    # thumb_rota1 ← l3 (MCP Yaw) with sign=-1, offset=0. Affine rescale.
    deg_r1 = -1.0 * thumb["MCP_Y"] + 0.0
    deg_r1 = _affine(deg_r1, *in_range["thumb_rota1"], *XHAND_THUMB_RANGE_DEG["thumb_rota1"])

    # thumb_rota2 ← l20 (MCP Roll) with sign=+1, offset=0. Affine rescale.
    deg_r2 = +1.0 * thumb["MCP_R"] + 0.0
    deg_r2 = _affine(deg_r2, *in_range["thumb_rota2"], *XHAND_THUMB_RANGE_DEG["thumb_rota2"])

    return {
        "thumb_bend":  _clamp(deg_bend, *XHAND_THUMB_RANGE_DEG["thumb_bend"]),
        "thumb_rota1": _clamp(deg_r1,   *XHAND_THUMB_RANGE_DEG["thumb_rota1"]),
        "thumb_rota2": _clamp(deg_r2,   *XHAND_THUMB_RANGE_DEG["thumb_rota2"]),
    }


# ----------------------------------------------------------------------------
# Algorithm B — coupled affine. rota1 / rota2 share both l3 and l20 (signed
# weights). Default weights below describe a 30-degree rotation between
# UDCAP and XHand opposition frames; M8c ADR-049 will commit to concrete
# numbers based on the visual fit.
# ----------------------------------------------------------------------------

_ALGO_B_DEFAULT_COUPLING = {
    "rota1_w3":   -0.866,   # cos(30°) on the rota1 component
    "rota1_w20":  -0.500,   # sin(30°) cross-coupling from MCP Roll
    "rota2_w3":   +0.500,
    "rota2_w20":  +0.866,
}


def _algo_b(thumb: Dict[str, float],
            in_range: Dict[str, Tuple[float, float]]) -> Dict[str, float]:
    wsum_bend = (0.3 * thumb["DIP"] + 0.3 * thumb["PIP"] + 0.4 * thumb["MCP_P"])
    deg_bend = -1.0 * wsum_bend + 0.0
    deg_bend = _affine(deg_bend, *in_range["thumb_bend"], *XHAND_THUMB_RANGE_DEG["thumb_bend"])

    deg_r1 = (_ALGO_B_DEFAULT_COUPLING["rota1_w3"]  * thumb["MCP_Y"] +
              _ALGO_B_DEFAULT_COUPLING["rota1_w20"] * thumb["MCP_R"])
    deg_r1 = _affine(deg_r1, *in_range["thumb_rota1"], *XHAND_THUMB_RANGE_DEG["thumb_rota1"])

    deg_r2 = (_ALGO_B_DEFAULT_COUPLING["rota2_w3"]  * thumb["MCP_Y"] +
              _ALGO_B_DEFAULT_COUPLING["rota2_w20"] * thumb["MCP_R"])
    deg_r2 = _affine(deg_r2, *in_range["thumb_rota2"], *XHAND_THUMB_RANGE_DEG["thumb_rota2"])

    return {
        "thumb_bend":  _clamp(deg_bend, *XHAND_THUMB_RANGE_DEG["thumb_bend"]),
        "thumb_rota1": _clamp(deg_r1,   *XHAND_THUMB_RANGE_DEG["thumb_rota1"]),
        "thumb_rota2": _clamp(deg_r2,   *XHAND_THUMB_RANGE_DEG["thumb_rota2"]),
    }


# ----------------------------------------------------------------------------
# Algorithm C — tip-direction reprojection (URDF-free best-effort).
#
# Treats (l2 = MCP Pitch, l3 = MCP Yaw, l20 = MCP Roll) as ZYX Euler angles
# in radians, builds R = Rz(yaw) * Ry(pitch) * Rx(roll), takes the world-X
# axis through R (the resting thumb tip direction in UDCAP frame), then
# heuristically projects the direction vector onto plausible XHand axes:
#
#   rota1  ← arctan2(d_y, d_x)      (opposition-around-vertical)
#   rota2  ← arctan2(d_z, sqrt(d_x^2 + d_y^2))   (opposition-elevation)
#   bend   ← magnitude of (l0+l1+l2) chain flexion, same as A/B
#
# Without the XHand thumb URDF this is *only* a qualitative comparison
# target — full IK is deferred to ADR-049 if Algo C ends up being the choice.
# ----------------------------------------------------------------------------

def _rot_matrix_zyx(yaw_deg: float, pitch_deg: float, roll_deg: float):
    cy = math.cos(math.radians(yaw_deg));   sy = math.sin(math.radians(yaw_deg))
    cp = math.cos(math.radians(pitch_deg)); sp = math.sin(math.radians(pitch_deg))
    cr = math.cos(math.radians(roll_deg));  sr = math.sin(math.radians(roll_deg))
    # R = Rz(yaw) * Ry(pitch) * Rx(roll)
    return [
        [cy*cp,         cy*sp*sr - sy*cr,    cy*sp*cr + sy*sr],
        [sy*cp,         sy*sp*sr + cy*cr,    sy*sp*cr - cy*sr],
        [-sp,           cp*sr,               cp*cr           ],
    ]


def _algo_c(thumb: Dict[str, float],
            in_range: Dict[str, Tuple[float, float]]) -> Dict[str, float]:
    R = _rot_matrix_zyx(thumb["MCP_Y"], thumb["MCP_P"], thumb["MCP_R"])
    # World X axis through R = thumb tip direction in UDCAP frame.
    dx, dy, dz = R[0][0], R[1][0], R[2][0]
    r1_deg = math.degrees(math.atan2(dy, dx))
    r2_deg = math.degrees(math.atan2(dz, math.sqrt(dx*dx + dy*dy)))
    wsum_bend = (0.3 * thumb["DIP"] + 0.3 * thumb["PIP"] + 0.4 * thumb["MCP_P"])
    deg_bend = -1.0 * wsum_bend + 0.0

    deg_bend = _affine(deg_bend, *in_range["thumb_bend"], *XHAND_THUMB_RANGE_DEG["thumb_bend"])
    deg_r1   = _affine(r1_deg,   *in_range["thumb_rota1"], *XHAND_THUMB_RANGE_DEG["thumb_rota1"])
    deg_r2   = _affine(r2_deg,   *in_range["thumb_rota2"], *XHAND_THUMB_RANGE_DEG["thumb_rota2"])
    return {
        "thumb_bend":  _clamp(deg_bend, *XHAND_THUMB_RANGE_DEG["thumb_bend"]),
        "thumb_rota1": _clamp(deg_r1,   *XHAND_THUMB_RANGE_DEG["thumb_rota1"]),
        "thumb_rota2": _clamp(deg_r2,   *XHAND_THUMB_RANGE_DEG["thumb_rota2"]),
    }


ALGORITHMS = {"A": _algo_a, "B": _algo_b, "C": _algo_c}


# ----------------------------------------------------------------------------
# Recording loader + per-source range calibration.
# ----------------------------------------------------------------------------

def _load_recording(path: str, hand: str) -> Tuple[List[float], List[Dict[str, float]]]:
    """Returns (times, thumb_frames) where thumb_frames[i] has keys
    DIP/PIP/MCP_P/MCP_Y/MCP_R sourced from the chosen hand's UDCAP indices."""
    if hand not in ("left", "right"):
        raise SystemExit(f"--hand must be left|right, got: {hand}")
    times: List[float] = []
    thumb_frames: List[Dict[str, float]] = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            src = row.get(hand)
            if not isinstance(src, list) or len(src) < 24:
                continue
            times.append(float(row.get("ts", 0.0)))
            thumb_frames.append({k: float(src[i]) for k, i in THUMB_SOURCES.items()})
    if not thumb_frames:
        raise SystemExit(f"recording {path} has no parseable {hand}-hand frames")
    return times, thumb_frames


def _derive_input_ranges(thumb_frames: List[Dict[str, float]]) -> Dict[str, Tuple[float, float]]:
    """For each XHand thumb joint, derive `input_range` as the min/max of
    the same weighted+sign+offset expression each algorithm applies before
    the affine step. Cheap stand-in for what `--actions calibrate-udcap`
    would produce at runtime."""
    def collect(values: List[float]) -> Tuple[float, float]:
        return (min(values), max(values))

    bend_inputs = [
        -1.0 * (0.3 * f["DIP"] + 0.3 * f["PIP"] + 0.4 * f["MCP_P"])
        for f in thumb_frames
    ]
    # rota1 / rota2 input ranges differ per algorithm but using the
    # straight Algo-A pre-affine signal as the common range keeps the
    # three traces directly comparable. Algorithm-specific input ranges
    # are an explicit M8c follow-up if the prototype shows Algo B or C
    # needs them.
    r1_inputs = [-1.0 * f["MCP_Y"] for f in thumb_frames]
    r2_inputs = [+1.0 * f["MCP_R"] for f in thumb_frames]
    return {
        "thumb_bend":  collect(bend_inputs),
        "thumb_rota1": collect(r1_inputs),
        "thumb_rota2": collect(r2_inputs),
    }


# ----------------------------------------------------------------------------
# Output: matplotlib 4-panel PNG, or CSV fallback.
# ----------------------------------------------------------------------------

def _opposition_score(r1: float, r2: float) -> float:
    """Normalised L2 norm of (r1, r2). Both joints share output range
    [-10, 110] / [0, 50] so we normalise each axis to [0, 1] first."""
    r1_lo, r1_hi = XHAND_THUMB_RANGE_DEG["thumb_rota1"]
    r2_lo, r2_hi = XHAND_THUMB_RANGE_DEG["thumb_rota2"]
    n1 = (r1 - r1_lo) / (r1_hi - r1_lo)
    n2 = (r2 - r2_lo) / (r2_hi - r2_lo)
    return math.sqrt(n1 * n1 + n2 * n2)


def _emit_csv(out_path: str, times: List[float],
              outs: List[Dict[str, float]],
              opp: List[float]) -> None:
    with open(out_path, "w") as f:
        f.write("ts,thumb_bend,thumb_rota1,thumb_rota2,opposition\n")
        for t, o, sc in zip(times, outs, opp):
            f.write(f"{t:.6f},{o['thumb_bend']:.3f},"
                    f"{o['thumb_rota1']:.3f},{o['thumb_rota2']:.3f},{sc:.4f}\n")


def _emit_plot(plot_path: str, times: List[float],
               outs: List[Dict[str, float]],
               opp: List[float], algo: str, hand: str,
               in_range: Dict[str, Tuple[float, float]]) -> Optional[str]:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as e:
        return f"matplotlib unavailable ({e}); use CSV fallback"

    fig, axes = plt.subplots(4, 1, figsize=(10, 10), sharex=True)
    bend  = [o["thumb_bend"]  for o in outs]
    rota1 = [o["thumb_rota1"] for o in outs]
    rota2 = [o["thumb_rota2"] for o in outs]

    for ax, ydata, ylabel, key in [
        (axes[0], bend,  "thumb_bend (deg)",  "thumb_bend"),
        (axes[1], rota1, "thumb_rota1 (deg)", "thumb_rota1"),
        (axes[2], rota2, "thumb_rota2 (deg)", "thumb_rota2"),
    ]:
        ax.plot(times, ydata, linewidth=1.0)
        ax.set_ylabel(ylabel)
        ax.grid(True, alpha=0.3)
        lo, hi = XHAND_THUMB_RANGE_DEG[key]
        ax.axhspan(lo, hi, alpha=0.05)
        ax.set_ylim(lo - 5, hi + 5)

    axes[3].plot(times, opp, color="purple", linewidth=1.0)
    axes[3].set_ylabel("opposition (norm)")
    axes[3].set_xlabel("time (s)")
    axes[3].grid(True, alpha=0.3)
    axes[3].set_ylim(0.0, math.sqrt(2.0) + 0.05)

    title_lines = [f"Algorithm {algo} — {hand} thumb retargeting prototype"]
    for k, (lo, hi) in in_range.items():
        title_lines.append(f"  derived input_range[{k}] = [{lo:.2f}, {hi:.2f}]")
    fig.suptitle("\n".join(title_lines), fontsize=10, ha="left", x=0.02)
    plt.tight_layout(rect=(0.0, 0.0, 1.0, 0.94))
    fig.savefig(plot_path, dpi=120)
    plt.close(fig)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--recording", required=True,
                        help="JSONL recording from record_udcap_thumb_sequences.py")
    parser.add_argument("--algo", choices=list(ALGORITHMS.keys()), required=True,
                        help="Which candidate algorithm to render")
    parser.add_argument("--plot", required=True,
                        help="Output PNG path; CSV fallback writes <plot>.csv next to it")
    parser.add_argument("--hand", choices=("left", "right"), default="left",
                        help="Which hand to analyse (default: left)")
    args = parser.parse_args()

    times, thumb_frames = _load_recording(args.recording, args.hand)
    in_range = _derive_input_ranges(thumb_frames)
    algo_fn = ALGORITHMS[args.algo]

    outs: List[Dict[str, float]] = []
    opp: List[float] = []
    for frame in thumb_frames:
        out = algo_fn(frame, in_range)
        outs.append(out)
        opp.append(_opposition_score(out["thumb_rota1"], out["thumb_rota2"]))

    # Always write a CSV next to the PNG so the data is recoverable when
    # matplotlib is not installed or fails (LOCAL dev Macs without brew).
    csv_path = os.path.splitext(args.plot)[0] + ".csv"
    _emit_csv(csv_path, times, outs, opp)
    print(f"Wrote {csv_path} ({len(outs)} rows)", file=sys.stderr)

    err = _emit_plot(args.plot, times, outs, opp, args.algo, args.hand, in_range)
    if err is None:
        print(f"Wrote {args.plot}", file=sys.stderr)
    else:
        print(f"PNG skipped: {err}", file=sys.stderr)

    # Quick stdout summary for the operator (and for the plan execution
    # record). M8c Step C.4 selection decision uses these numbers.
    def _stats(label: str, vals: List[float]) -> str:
        return (f"{label}: min={min(vals):+.2f}  max={max(vals):+.2f}  "
                f"range={max(vals)-min(vals):.2f}")

    print(f"--- Algorithm {args.algo} / {args.hand} hand ---")
    print(_stats("thumb_bend ", [o["thumb_bend"]  for o in outs]))
    print(_stats("thumb_rota1", [o["thumb_rota1"] for o in outs]))
    print(_stats("thumb_rota2", [o["thumb_rota2"] for o in outs]))
    print(_stats("opposition ", opp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
