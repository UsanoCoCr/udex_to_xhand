# M2: UDCAP Parameter Experimental Verification Plan

**Date**: 2026-04-27
**Milestone**: M2 — UDCAP 参数实验验证
**Status**: Complete — official docs found, superseding experimental approach (see ADR-009)
**Depends on**: M1 (complete)

## Context

SPEC.md §3.1 defines an l0-l23 → finger/joint mapping that is **unverified hypothesis**. The bone ordering from the UDCAP FBX model (`udcap关节文档/HandDriver Initial Hand Mode Joint Position.txt`) suggests the parameter order might NOT follow the thumb-first order assumed in SPEC.md — the FBX bones are ordered index → middle → pinky → ring → thumb, which could hint at a different parameter grouping.

This milestone creates a diagnostic script that lets the operator flex one finger at a time while watching which `l`-indices respond, producing a verified mapping table. The output updates `config.yaml` source indices and is documented in `docs/verified-mapping.md`.

No XHand hardware needed. Only requires UDCAP gloves + Windows PC sending UDP.

---

## 1. File List

| File | Action | Responsibility |
|------|--------|----------------|
| `scripts/udcap_param_identify.py` | **NEW** | Interactive guided tool: captures baseline, prompts operator per-finger, computes deltas, identifies responsive l-indices, exports results |
| `docs/verified-mapping.md` | **NEW** | Human-readable record of verified mapping with experiment date, per-finger results, and notes |
| `config.yaml` | **MODIFY** | Update `mapping.left.*.sources` and `mapping.right.*.sources` with verified indices after experiment |
| `udcap_receiver.py` | NO CHANGE | Reused as-is via import |
| `SPEC.md` | NO CHANGE | Will be updated in a later milestone if mapping differs significantly from hypothesis |

---

## 2. Data Flow

```
UDCAP HandDriver (Windows)
    |  UDP JSON, port 9000
    v
UdcapReceiver.receive()                 <-- reuse M1, real UDP mode
    |  returns {"left": [24 floats], "right": [24 floats], ...}
    v
udcap_param_identify.py
    |
    +-- Phase 1: BASELINE CAPTURE
    |   Operator holds hand relaxed, open palm
    |   Collect N frames (e.g. 50), compute per-index average
    |   baseline_left[0..23], baseline_right[0..23]
    |
    +-- Phase 2: PER-FINGER IDENTIFICATION (guided, 5 fingers x 2 gestures)
    |   For each finger (thumb, index, middle, ring, pinky):
    |     Prompt: "请只弯曲【左手拇指】，弯到底后按 Enter"
    |     Collect N frames, compute per-index average
    |     delta[i] = flexed_avg[i] - baseline[i]
    |     Report indices where |delta| > threshold (default 5°)
    |     Operator confirms or flags anomalies
    |
    +-- Phase 3: RESULTS SUMMARY
    |   Print verified mapping table
    |   Write docs/verified-mapping.md
    |   Optionally write updated config.yaml sources
    |
    v
Verified mapping: l-index → finger → joint → axis
```

### Per-finger capture detail

```
Prompt → operator flexes one finger → hold → press Enter
    |
    +-- Collect 50 frames (~0.5s at 90Hz)
    +-- Average each l0-l23 across frames (noise reduction)
    +-- delta[i] = avg_flexed[i] - baseline[i]
    +-- Sort by |delta| descending
    +-- Display: indices with |delta| > 5°, ranked by magnitude
    +-- Operator visually confirms (e.g. "l0 Δ=-42, l3 Δ=-19, l4 Δ=-12" for thumb)
```

---

## 3. Script Design: `scripts/udcap_param_identify.py`

### CLI interface

```bash
python scripts/udcap_param_identify.py --port 9000 [--samples 50] [--threshold 5.0] [--hand left|right|both]
```

### Guided flow (interactive, stdin prompts)

```
Step 0: Waiting for UDP data...
         ✓ Receiving at 89 FPS from 192.168.1.100

Step 1: BASELINE — 请将左手完全张开放松，然后按 Enter
         Capturing 50 frames... done.
         Baseline captured. (l0=-2.1, l1=-3.0, ..., l23=0.0)

Step 2: THUMB — 请只弯曲【左手拇指】到底，其余手指不动，然后按 Enter
         Capturing 50 frames... done.
         变化参数 (|Δ| > 5°):
           l0  Δ = -42.3°  ← 最大变化
           l3  Δ = -18.7°
           l4  Δ = -12.1°
         其余参数 |Δ| < 5° (最大: l1 Δ=3.2°)
         → 推断: l0=Thumb主弯曲, l3=Thumb MP, l4=Thumb IP

Step 3: INDEX — 请只弯曲【左手食指】...
         ...

Step 4: MIDDLE — ...
Step 5: RING — ...
Step 6: PINKY — ...

Step 7: WRIST (可选) — 请弯曲手腕...
         → 确认 l21/l22/l23 = wrist

Step 8: SUMMARY
         ┌───────┬────────────────┬────────────┬──────────┐
         │ Index │ Finger         │ Joint      │ Peak Δ   │
         ├───────┼────────────────┼────────────┼──────────┤
         │ l0    │ Thumb          │ CM Pitch   │ -42.3°   │
         │ l1    │ Thumb          │ CM Yaw     │ -35.0°   │
         │ ...   │ ...            │ ...        │ ...      │
         │ l23   │ Wrist          │ Roll       │ -8.0°    │
         └───────┴────────────────┴────────────┴──────────┘

         Saved to docs/verified-mapping.md
```

### Key design decisions

1. **Reuse UdcapReceiver** — import from `udcap_receiver.py`, don't re-implement UDP. Run in real mode (not mock).

2. **Average over N frames** — single frames are noisy; averaging 50 frames at 90 FPS takes ~0.6s, good enough for a held pose.

3. **Threshold-based detection** — default 5° filters noise. Operator can override with `--threshold`. The per-finger report shows ALL indices sorted by |delta|, not just those above threshold, so nothing is hidden.

4. **Left hand first, right hand separate** — do left hand fully, then optionally repeat for right. Right hand mapping might differ due to sign conventions. `--hand` flag controls which.

5. **No automatic config.yaml rewrite** — the script prints the suggested mapping and writes `docs/verified-mapping.md`. Operator manually updates `config.yaml` (or a future flag `--update-config` does it). Reason: mapping interpretation requires human judgment (e.g., whether l0 is "CM Pitch" or something else).

6. **Cross-talk detection** — if a finger test shows significant delta on indices already assigned to another finger, flag it as "cross-talk" in the report. This catches coupled movements (e.g., ring + pinky tend to co-activate).

---

## 4. Test Strategy

### Test 1: Script runs with mock receiver (no hardware)

```bash
python scripts/udcap_param_identify.py --port 9000 --mock
```

Expected: script starts, captures baseline from example.json, each finger step captures same data (no real movement), all deltas ≈ 0°, prints "no significant change detected" for each finger. Script completes without crash.

Purpose: validates code path — imports, frame averaging, delta computation, output formatting.

### Test 2: Manual UDP injection (no UDCAP hardware, requires LAN or localhost)

**Terminal 1:**
```bash
python scripts/udcap_param_identify.py --port 9000 --samples 5 --threshold 3
```

**Terminal 2:** Python script that sends crafted packets:
- Baseline: all l-values = 0
- "Thumb" step: l0=-40, l3=-20, l4=-15, all others = 0
- "Index" step: l5=-70, l6=-10, l7=-80, l8=-60, all others = 0

Expected:
- Baseline step: all zeros captured
- Thumb step: script reports l0 Δ=-40, l3 Δ=-20, l4 Δ=-15
- Index step: script reports l5 Δ=-70, l7 Δ=-80, l8 Δ=-60, l6 Δ=-10

Purpose: validates delta computation, threshold filtering, and ranking with known inputs.

### Test 3: Real UDCAP gloves — full experiment (requires hardware)

```bash
python scripts/udcap_param_identify.py --port 9000 --hand left
```

Expected:
- Script connects and shows FPS
- Baseline captured successfully
- For each finger, operator flexes and multiple l-indices respond with Δ > 5°
- Summary table shows 24 indices grouped by finger
- `docs/verified-mapping.md` created with results

Verification checklist:
- [ ] Each finger test activates 2-5 indices (no more — otherwise sensor noise or cross-talk)
- [ ] All 24 indices (l0-l23) are accounted for in the summary (21 finger + 3 wrist)
- [ ] No index appears as primary in two different fingers
- [ ] l21/l22/l23 only change during wrist test
- [ ] Results are consistent with SPEC.md §3.1 hypothesis OR deviations are documented

### Test 4: Right hand (requires hardware)

```bash
python scripts/udcap_param_identify.py --port 9000 --hand right
```

Expected: same flow as Test 3 but for r0-r23. Key observation: check whether sign convention differs from left hand (SPEC.md §4.3 warns about this).

---

## 5. Verification Commands

### Automated (no hardware):
```bash
python scripts/udcap_param_identify.py --port 9000 --mock
```
Pass: completes without crash, prints summary table.

### Manual (requires UDCAP hardware):
```bash
# Primary M2 done-definition:
python scripts/udcap_param_identify.py --port 9000 --hand left
python scripts/udcap_param_identify.py --port 9000 --hand right
```
Pass criteria:
1. `docs/verified-mapping.md` exists with all 24 left and 24 right indices mapped
2. Each index assigned to exactly one finger+joint
3. Mapping is internally consistent (indices for same finger are contiguous or near-contiguous)
4. `config.yaml` mapping sources updated to match verified indices

---

## 6. Expected Outcomes vs SPEC.md Hypothesis

SPEC.md §3.1 assumes this ordering (thumb → index → middle → ring → pinky):

```
l0-l4:   Thumb (CM Pitch, CM Yaw, CM Roll, MP Pitch, IP Pitch)
l5-l8:   Index (MP Pitch, MP Yaw, PIP Pitch, DIP Pitch)
l9-l12:  Middle (MP Pitch, MP Yaw, PIP Pitch, DIP Pitch)
l13-l16: Ring (MP Pitch, MP Yaw, PIP Pitch, DIP Pitch)
l17-l20: Pinky (MP Pitch, MP Yaw, PIP Pitch, DIP Pitch)
l21-l23: Wrist (Pitch, Yaw, Roll)
```

The FBX bone ordering in `udcap关节文档/HandDriver Initial Hand Mode Joint Position.txt` is:
```
Bone 3-7:   index
Bone 8-12:  middle
Bone 13-17: pinky
Bone 18-22: ring
Bone 23-26: thumb
```

These differ. The experiment will determine which (if either) matches reality. If the bone ordering maps to parameter ordering, the l-index grouping would be:

```
l0-l4:   Index?
l5-l8:   Middle?
l9-l12:  Pinky?
l13-l16: Ring?
l17-l20: Thumb?
l21-l23: Wrist?
```

The experiment is the only way to know. Both hypotheses are recorded here for comparison.

---

## 7. Deferred (not M2 scope)

- Actual joint mapping implementation (M4)
- XHand driver (M3)
- Right hand sign convention deep-dive (M4 after mapping is verified)
- Per-axis identification within a finger (e.g., separating MP Pitch from MP Yaw) — M2 identifies which indices belong to which finger; exact axis labeling may require M4 iteration
- Automatic config.yaml rewriting — manual for now, can be added later if needed
