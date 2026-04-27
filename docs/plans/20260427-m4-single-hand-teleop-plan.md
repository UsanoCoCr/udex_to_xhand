# M4: Single-hand Real-time Teleoperation (Left Hand) — Implementation Plan

## Context

M0-M3 complete. UDP receiver (M1) and XHand driver (M3) are real. `joint_mapper.py` and `safety.py` are still M0 stubs. M4 replaces the stub mapper with config-driven mapping so that UDCAP glove data actually controls the XHand. Left hand only per user instruction.

---

## 1. File List

| File | Action | Purpose |
|------|--------|---------|
| `joint_mapper.py` | **Rewrite** | Config-driven weighted-sum mapping: UDCAP 24 DOF → XHand 12 DOF |
| `safety.py` | **Modify** | Replace `STUB_LIMITS` with `HARD_LIMITS_RAD` (XHand physical range) |
| `main.py` | **Modify** | Import `HARD_LIMITS_RAD` instead of `STUB_LIMITS` (2 lines) |

**Zero new files. `config.yaml` unchanged** — M2 already wrote the correct mapping.

---

## 2. Data Flow

```
udcap_receiver.receive()
  → {"left": [24 floats, degrees]}
       │
       ▼
mapper.map("left", udcap_24)
  FOR EACH of 12 XHand joints (J0-J11):
    1. weighted_sum = Σ(weight[k] * udcap_24[source[k]])
    2. deg = sign * weighted_sum + offset
    3. deg = clamp(deg, min_deg, max_deg)    ← degree domain, from config
    4. rad = deg * π / 180
  → [12 floats, radians]
       │
       ▼
clamp(left_12, HARD_LIMITS_RAD)              ← radian domain, last-resort safety
       │
       ▼
driver.send("left", left_12)                 ← RS485 to XHand
```

**Clamping design**: Primary clamp in degree domain inside `mapper.map()` (per-joint from config). Secondary clamp in radian domain in `main.py` (defense-in-depth, XHand physical range -90°~110°, should never activate under normal operation).

---

## 3. Per-file Changes

### 3.1 `joint_mapper.py` — Rewrite

**Add** module constant `JOINT_ORDER`: tuple of 12 config key names in XHand J0-J11 order:
```
("thumb_bend", "thumb_rota1", "thumb_rota2",
 "index_bend", "index_joint1", "index_joint2",
 "mid_joint1", "mid_joint2",
 "ring_joint1", "ring_joint2",
 "pinky_joint1", "pinky_joint2")
```

**Rewrite** `__init__(self, config)`:
- `config` is `config.get("mapping", {})` — already passed this way from main.py
- For each hand in config (e.g., `config["left"]`):
  - For each joint name in `JOINT_ORDER`, read the config entry
  - Extract: `sources` (list[int]), `weights` (list[float]), `sign` (int), `offset` (float), `clamp` ([min_deg, max_deg])
  - Store as pre-built list of 12 tuples for fast hot-path access
- Validate: source indices in [0,23], len(sources)==len(weights)

**Rewrite** `map(self, hand, udcap_24) -> list[float]`:
- Look up pre-built specs for `hand`
- For each of 12 joints: weighted sum → sign → offset → deg clamp → deg2rad
- Raise `KeyError` if hand not in config

**Update** `__main__` block:
- Load config.yaml, create mapper, load example.json, extract left-hand 24 values
- Run `mapper.map("left", ...)`, print input and output for manual verification

### 3.2 `safety.py` — Modify

**Remove**: `STUB_LIMITS = [(-2.0, 2.0)] * 12`

**Add**:
```python
import math
_D2R = math.pi / 180.0
# Last-resort hard limits in radians (XHand physical range: -90° to 110°)
HARD_LIMITS_RAD = [(-90 * _D2R, 110 * _D2R)] * 12
```

### 3.3 `main.py` — Modify (2 lines only)

- Line 14: `from safety import STUB_LIMITS, clamp` → `from safety import HARD_LIMITS_RAD, clamp`
- Lines 55, 61: `STUB_LIMITS` → `HARD_LIMITS_RAD`

---

## 4. Test Strategy

### Test A: Mapper self-test (no hardware, any machine)

```bash
python joint_mapper.py
```

Expected output with example.json data through config.yaml left mapping:

| Joint | Config Key | Calculation | Deg | Rad |
|-------|-----------|-------------|-----|-----|
| J0 | thumb_bend | 0.3×(-47.6)+0.3×(-60)+0.4×(9.4)=-28.52 → ×-1=28.52 | 28.52 | 0.498 |
| J1 | thumb_rota1 | 1.0×(23.6)=23.6 → ×-1=-23.6 → clamp(-10,110)=-10.0 | -10.0 | -0.175 |
| J2 | thumb_rota2 | 1.0×(-0.6)=-0.6 → ×1=-0.6 → clamp(0,50)=0.0 | 0.0 | 0.000 |
| J3 | index_bend | 1.0×(0.0)=0 → ×-1=0 | 0.0 | 0.000 |
| J4 | index_joint1 | 1.0×(-13.1) → ×-1=13.1 | 13.1 | 0.229 |
| J5 | index_joint2 | 0.6×(-14.1)+0.4×(-11.3)=-12.98 → ×-1=12.98 | 12.98 | 0.227 |
| J6 | mid_joint1 | 1.0×(-1.3) → ×-1=1.3 | 1.3 | 0.023 |
| J7 | mid_joint2 | 0.6×(-5.7)+0.4×(-4.5)=-5.22 → ×-1=5.22 | 5.22 | 0.091 |
| J8 | ring_joint1 | 1.0×(-9.1) → ×-1=9.1 | 9.1 | 0.159 |
| J9 | ring_joint2 | 0.6×(-15.2)+0.4×(-12.2)=-14.0 → ×-1=14.0 | 14.0 | 0.244 |
| J10 | pinky_joint1 | 1.0×(-24.7) → ×-1=24.7 | 24.7 | 0.431 |
| J11 | pinky_joint2 | 0.6×(-33.9)+0.4×(-27.1)=-31.18 → ×-1=31.18 | 31.18 | 0.544 |

### Test B: Mock mode end-to-end (no hardware)

```bash
python main.py --mock --duration 3 --hand left
```

Expected: ~300 ticks, 12 radian values per tick matching Test A table. No crash.

### Test C: Real hardware left-hand teleoperation

```bash
python main.py --config config.yaml --hand left
```

Verification checklist:
- [ ] Thumb flexion → J0 increases, J1/J2 minimal
- [ ] Index flexion → J4/J5 increase, others minimal
- [ ] Middle/Ring/Pinky same pattern, correct joints
- [ ] Fist → all flexion joints high
- [ ] Open palm → all flexion joints near zero
- [ ] No reversed motion
- [ ] No unexpected cross-talk

### Test D: Clamp verification (hardware)

Edit `config.yaml`: set `index_joint1: { ..., clamp: [0, 30] }`. Flex index fully. XHand index should stop at ~30°.

---

## 5. Deferred (NOT in M4 scope)

- Right hand mirroring (M6)
- Watchdog timer (M5)
- Graceful shutdown improvements (M5)
- CalibrationStatus==3 gate in main loop (M5)
- Per-joint PID tuning (M7)
