#!/usr/bin/env python3
"""UDCAP parameter identification tool.

Interactive guided tool to verify which l-indices (l0-l23) correspond
to which finger joints by having the operator flex one finger at a time.

Usage:
    python scripts/udcap_param_identify.py --port 9000 --hand left
    python scripts/udcap_param_identify.py --port 9000 --mock
"""

import argparse
import os
import sys
import time
from datetime import datetime

# Add parent directory to path for importing udcap_receiver
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from udcap_receiver import UdcapReceiver

FINGERS = [
    ("THUMB", "拇指"),
    ("INDEX", "食指"),
    ("MIDDLE", "中指"),
    ("RING", "无名指"),
    ("PINKY", "小指"),
]

HAND_PREFIX = {"left": "l", "right": "r"}


def collect_frames(receiver, n_samples, hand_key):
    """Collect n_samples non-None frames, return averaged 24-value list."""
    frames = []
    while len(frames) < n_samples:
        data = receiver.receive()
        if data is not None:
            frames.append(data[hand_key])
        else:
            time.sleep(0.001)

    n = len(frames)
    avg = [sum(f[i] for f in frames) / n for i in range(24)]
    return avg


def compute_deltas(baseline, flexed):
    """Compute delta for each index: flexed - baseline."""
    return [flexed[i] - baseline[i] for i in range(24)]


def display_deltas(deltas, threshold, prefix, assigned):
    """Display deltas sorted by |delta|, flag cross-talk.

    Returns list of (index, delta) for indices above threshold.
    """
    indexed = [(i, deltas[i]) for i in range(24)]
    indexed.sort(key=lambda x: abs(x[1]), reverse=True)

    significant = [(i, d) for i, d in indexed if abs(d) >= threshold]
    insignificant = [(i, d) for i, d in indexed if abs(d) < threshold]

    if significant:
        print(f"  变化参数 (|Δ| > {threshold}°):")
        for rank, (i, d) in enumerate(significant):
            marker = ""
            if i in assigned:
                marker = f"  ⚠ 已分配给 {assigned[i]}"
            biggest = " ← 最大变化" if rank == 0 else ""
            print(f"    {prefix}{i:<3} Δ = {d:>+7.1f}°{biggest}{marker}")
    else:
        print(f"  未检测到显著变化 (所有 |Δ| < {threshold}°)")

    if insignificant:
        max_insig = max(insignificant, key=lambda x: abs(x[1]))
        print(
            f"  其余参数 |Δ| < {threshold}° "
            f"(最大: {prefix}{max_insig[0]} Δ={max_insig[1]:+.1f}°)"
        )

    return significant


def wait_for_data(receiver, timeout=10):
    """Wait until receiver gets data. Returns estimated FPS."""
    print("\nStep 0: 等待 UDP 数据...")
    t_start = time.monotonic()
    count = 0
    t_fps_start = None

    while time.monotonic() - t_start < timeout:
        data = receiver.receive()
        if data is not None:
            count += 1
            if t_fps_start is None:
                t_fps_start = time.monotonic()
            if count >= 30:
                elapsed = time.monotonic() - t_fps_start
                fps = (count - 1) / elapsed if elapsed > 0.001 else 0
                addr = receiver.last_addr or "mock"
                print(f"  ✓ 正在接收数据，来自 {addr}，FPS ≈ {fps:.0f}")
                return fps
        else:
            time.sleep(0.001)

    if count == 0:
        print(f"  ✗ {timeout}秒内未收到数据，请检查 UDCAP 是否在发送")
        sys.exit(1)

    elapsed = time.monotonic() - t_fps_start if t_fps_start else 0
    fps = (count - 1) / elapsed if elapsed > 0.001 else 0
    addr = receiver.last_addr or "mock"
    print(f"  ✓ 正在接收数据，来自 {addr}，FPS ≈ {fps:.0f}")
    return fps


def run_hand(receiver, hand, n_samples, threshold):
    """Run the full identification flow for one hand.

    Returns (results, assigned, baseline) where:
      results: dict finger_name -> [(index, delta), ...]
      assigned: dict index -> finger_name
      baseline: list of 24 floats
    """
    hand_key = hand  # "left" or "right"
    prefix = HAND_PREFIX[hand]
    hand_cn = "左手" if hand == "left" else "右手"

    print(f"\n{'='*60}")
    print(f"  开始识别【{hand_cn}】参数映射")
    print(f"{'='*60}")

    # --- Baseline ---
    print(f"\nStep 1: BASELINE — 请将{hand_cn}完全张开放松，然后按 Enter")
    input("  [按 Enter 开始采集...]")
    print(f"  采集 {n_samples} 帧...", end="", flush=True)
    baseline = collect_frames(receiver, n_samples, hand_key)
    print(" 完成。")
    baseline_str = ", ".join(f"{prefix}{i}={baseline[i]:.1f}" for i in [0, 1, 2])
    print(f"  基线: {baseline_str}, ..., {prefix}23={baseline[23]:.1f}")

    # --- Per-finger ---
    results = {}  # finger_name -> [(index, delta), ...]
    assigned = {}  # index -> finger_name (for cross-talk detection)

    for step, (finger_en, finger_cn) in enumerate(FINGERS, start=2):
        print(
            f"\nStep {step}: {finger_en} — "
            f"请只弯曲【{hand_cn}{finger_cn}】到底，其余手指不动，然后按 Enter"
        )
        input("  [按 Enter 开始采集...]")
        print(f"  采集 {n_samples} 帧...", end="", flush=True)
        flexed = collect_frames(receiver, n_samples, hand_key)
        print(" 完成。")

        deltas = compute_deltas(baseline, flexed)
        significant = display_deltas(deltas, threshold, prefix, assigned)

        results[finger_en] = significant
        for idx, _ in significant:
            if idx not in assigned:
                assigned[idx] = finger_en

    # --- Wrist (optional) ---
    print(f"\nStep 7: WRIST (可选) — 是否测试手腕？(y/N)")
    wrist_choice = input("  ").strip().lower()
    if wrist_choice in ("y", "yes"):
        print(f"  请弯曲{hand_cn}手腕（各方向都试），然后按 Enter")
        input("  [按 Enter 开始采集...]")
        print(f"  采集 {n_samples} 帧...", end="", flush=True)
        flexed = collect_frames(receiver, n_samples, hand_key)
        print(" 完成。")

        deltas = compute_deltas(baseline, flexed)
        significant = display_deltas(deltas, threshold, prefix, assigned)
        results["WRIST"] = significant
        for idx, _ in significant:
            if idx not in assigned:
                assigned[idx] = "WRIST"

    # --- Unassigned indices ---
    unassigned = [i for i in range(24) if i not in assigned]
    if unassigned:
        print(f"\n  未分配的参数: {', '.join(f'{prefix}{i}' for i in unassigned)}")
        if wrist_choice not in ("y", "yes"):
            print(f"  (可能是手腕参数 — 建议重新运行并测试手腕)")

    return results, assigned, baseline


def print_summary(results, assigned, hand, prefix):
    """Print the summary table. Returns (all_entries, unassigned)."""
    hand_cn = "左手" if hand == "left" else "右手"

    print(f"\n{'='*60}")
    print(f"  【{hand_cn}】参数映射总结")
    print(f"{'='*60}")
    print(f"  {'Index':<8} {'Finger':<12} {'Δ (deg)':>10}")
    print(f"  {'─'*8} {'─'*12} {'─'*10}")

    # Collect all significant entries, sort by index
    all_entries = []
    for finger, sigs in results.items():
        for idx, delta in sigs:
            all_entries.append((idx, finger, delta))
    all_entries.sort(key=lambda x: x[0])

    for idx, finger, delta in all_entries:
        print(f"  {prefix}{idx:<7} {finger:<12} {delta:>+10.1f}°")

    # Unassigned
    unassigned = [i for i in range(24) if i not in assigned]
    for idx in unassigned:
        print(f"  {prefix}{idx:<7} {'???':<12} {'N/A':>10}")

    return all_entries, unassigned


def write_verified_mapping(all_results, output_path):
    """Write docs/verified-mapping.md with experiment results."""
    now = datetime.now().strftime("%Y-%m-%d %H:%M")

    lines = [
        "# Verified UDCAP Parameter Mapping",
        "",
        f"**Date**: {now}",
        "**Tool**: `scripts/udcap_param_identify.py`",
        "",
        "---",
        "",
    ]

    for hand, (entries, unassigned, assigned) in all_results.items():
        hand_cn = "左手 (Left)" if hand == "left" else "右手 (Right)"
        prefix = HAND_PREFIX[hand]

        lines.append(f"## {hand_cn}")
        lines.append("")
        lines.append("| Index | Finger | Peak Δ (deg) | Notes |")
        lines.append("|-------|--------|-------------|-------|")

        entry_map = {idx: (finger, delta) for idx, finger, delta in entries}
        for i in range(24):
            if i in entry_map:
                finger, delta = entry_map[i]
                lines.append(f"| {prefix}{i} | {finger} | {delta:+.1f}° | |")
            else:
                lines.append(
                    f"| {prefix}{i} | ??? | N/A | Not triggered by any finger |"
                )

        lines.append("")

    lines.extend([
        "---",
        "",
        "## Notes",
        "",
        "- Entries marked `???` were not significantly activated by any single-finger test",
        "- Cross-talk (index responding to multiple fingers) should be investigated",
        "- This mapping should be used to update `config.yaml` mapping sources",
        "",
    ])

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w") as f:
        f.write("\n".join(lines))

    print(f"\n  结果已保存到 {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="UDCAP parameter identification — verify l0-l23 → finger mapping"
    )
    parser.add_argument("--host", default="0.0.0.0", help="UDP bind address")
    parser.add_argument("--port", type=int, default=9000, help="UDP port")
    parser.add_argument(
        "--samples", type=int, default=50, help="Frames to average per capture"
    )
    parser.add_argument(
        "--threshold", type=float, default=5.0, help="Delta threshold in degrees"
    )
    parser.add_argument(
        "--hand",
        choices=["left", "right", "both"],
        default="both",
        help="Which hand to test",
    )
    parser.add_argument(
        "--mock", action="store_true", help="Use example.json (no hardware)"
    )
    args = parser.parse_args()

    config = {"host": args.host, "port": args.port}
    receiver = UdcapReceiver(config, mock=args.mock)

    try:
        if args.mock:
            print("Mock 模式: 使用 example.json 数据")

        wait_for_data(receiver)

        hands = ["left", "right"] if args.hand == "both" else [args.hand]
        all_results = {}

        for hand in hands:
            results, assigned, baseline = run_hand(
                receiver, hand, args.samples, args.threshold
            )
            entries, unassigned = print_summary(
                results, assigned, hand, HAND_PREFIX[hand]
            )
            all_results[hand] = (entries, unassigned, assigned)

        # Write verified mapping
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_dir = os.path.dirname(script_dir)
        output_path = os.path.join(project_dir, "docs", "verified-mapping.md")
        write_verified_mapping(all_results, output_path)

    except KeyboardInterrupt:
        print("\n\n中断。")
    finally:
        receiver.close()


if __name__ == "__main__":
    main()
