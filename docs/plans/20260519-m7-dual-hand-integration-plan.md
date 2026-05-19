# M7 Implementation Plan — Dual-Hand Integration (C++)

**Plan owner**: claude (assistant)
**Plan date**: 2026-05-19
**Target milestone**: roadmap §M7
**Predecessor**: M6 ✅ (2026-05-19) — safety hardening verified on PC2
**Executor**: PC2 operator on Unitree G1 PC2 (aarch64 Linux). Plan author runs on macOS workstation; every PC2 command below is explicit and self-contained (path + tee). LOCAL / PC2 prefix marks where each command runs.

---

## 0. Scope

**In-scope**:
- Physically connect a second XHand on PC2 via RS485, verify both hands enumerate.
- Confirm that the existing C++ binary (already dual-hand-capable since M5b) drives both hands simultaneously without code regressions.
- Add **one** behavioral guard: fail-closed when `--hand both` is requested but only one of `{Left, Right}` is discovered by `list_hands_id()` (currently the driver opens successfully on partial discovery and only fails on the first `send_*` call — late, log-poor failure).
- Verify `mapping.right` per-joint signs experimentally (M4-style single-finger flex tests on the **right** glove → right XHand). SPEC §10 risk 5 / §12 open question 3 flag this as HIGH probability of needing flips.
- Re-validate M6 safety scenarios in dual context: shared watchdog freezes both hands; SIGINT / SIGTERM sets mode=0 on both; per-joint clamp still authoritative; startup-gate timeout still exits 2 with both-hand passive close.

**Out-of-scope** (deferred to M8):
- PID re-tuning for dual-hand interactions (`config.yaml` `default_kp/ki/kd`).
- Low-pass smoothing filter on UDP jitter (SPEC §12 open question 6).
- Thumb retargeting algorithm rewrite (roadmap revision #6).
- The cup-grasping acceptance test — that **is** M8's goal.

**Non-goals** (do NOT do):
- Do NOT change watchdog semantics (ADR-035 / -038 stand; 1Hz stale WARN cadence + "recovered after Nms = time since last WARN" semantic untouched).
- Do NOT touch left-hand mapping numbers — verified through M4 / M5c.
- Do NOT introduce a per-hand watchdog. UDCAP transmits L+R in one UDP packet (SPEC §3.1); a stale packet stalls both sides equally.
- Do NOT relax `try_recv()`'s "both calib==3" gate. Operator wearing both gloves before launch is the established workflow; M7 only stresses that workflow.
- Do NOT refactor `XHandDriver` to two `XHandControl` instances. The single-instance multi-drop design is the verification target, not a planned change. If §4.2 forces two adapters, **STOP and re-plan** rather than silently splitting.

---

## 1. 文件清单

### 新增

(none)

### 修改

| File | 一句话职责 (delta only — focus on what M7 changes) |
| --- | --- |
| `src/xhand_driver.cpp` | `open()` now asserts both L and R hand_ids were discovered **only if** caller passes a "require both" flag; new tiny method `bool has_both() const`. Single-hand and existing M5c default paths keep current semantics. |
| `src/xhand_driver.hpp` | Declare `has_both()` + one extra arg on `open(bool require_both)` with default `false` (backward compatible). |
| `src/main.cpp` | Pass `require_both = (args.hand == cli::HandSelect::Both)` into `driver->open(...)`. No other loop change. |
| `config.yaml` | Per-joint `mapping.right` entries (sign / clamp) tuned from §4.3 single-finger tests. Each accepted edit triggers ADR-037 fixture regen + same-commit fixture update. |
| `tests/fixtures/mapper_baseline.json` | Regenerated on every `config.yaml` edit (ADR-037). Diff restricted to `config_yaml_sha256` + `generated_at` + the affected right-hand joint values; left-hand values must remain byte-identical. |
| `docs/plans/00-roadmap.md` | On completion: revision history line + §M7 marked ✅. |
| `SPEC.md` | On completion: §3 (right-hand sign findings, if any) + §10 risk 3 dual-bus contention result + §12 close out open questions 3 & 4. |

### 不动 (explicit non-scope — protects against scope creep)

- `src/udcap_receiver.{hpp,cpp}` — current "both calib==3" AND-gate is the desired M7 behavior.
- `src/joint_mapper.{hpp,cpp}` — `map_left` / `map_right` already symmetric since M5b. Only YAML inputs change.
- `src/safety.{hpp,cpp}` — watchdog + clamp + signal handlers all hand-agnostic; nothing to touch.
- `src/cli.{hpp,cpp}` — `HandSelect::Both` + parsing already exist and default is `Both` (verified 2026-05-19, cli.hpp:16).
- `tests/test_mapper_snapshot.cpp` / `tests/test_safety.cpp` — source unchanged.
- `legacy_python/` — frozen since M5b reorg (ADR-031).

---

## 2. 数据流

### 2.1 Dual-mode steady-state tick (both hands present, both calibrated)

```
UDCAP UDP packet (~60-120 Hz, both gloves worn)
  → UdcapReceiver::try_recv()
      → returns frame iff calib_left == 3 AND calib_right == 3 (already enforced)
  → UdcapFrame { l[24], r[24], calib_left=3, calib_right=3, recv_ts }
  → main loop @ 100 Hz
      → mapper.map_left (frame.l)  → 12 rad  (clamped in degree domain + radian fail-safe)
      → mapper.map_right(frame.r) → 12 rad  (clamped in degree domain + radian fail-safe)
      → driver.send_left (left_rad)  → SDK send_command(hand_id_left_,  HandCommand_t)
      → driver.send_right(right_rad) → SDK send_command(hand_id_right_, HandCommand_t)
      → cache last_left_rad / last_right_rad (M6 stale-resend reservoir)
      → latency_stats.add(now - recv_ts)
```

### 2.2 Stale UDP branch (>200 ms since last fresh frame)

```
loop tick, frame_opt == nullopt && wdog.is_stale()
  → driver.send_left (*last_left_rad)
  → driver.send_right(*last_right_rad)
  → if (now - last_warn_ts >= 1 s) LOG_WARN("watchdog: no UDP for >200ms, holding last position")
  → was_stale = true
```

Single shared `Watchdog`; both hands resend on each stale tick. On recovery, single `LOG_INFO("watchdog: recovered after Nms")` (ADR-038 semantic — N = time since last WARN, not total outage). M6 P1 single-hand path is identical; M7 P1' only adds the right-hand resend that's already in the code.

### 2.3 Startup (Both mode)

```
driver.open(require_both = true)
  → XHandControl::open_serial("/dev/ttyACM0", 3000000)
  → ids = list_hands_id()             // expect ≥ 2 ids
  → for each id in ids: get_hand_type(id) → 'L' | 'R' | else
      → store into hand_id_left_ / hand_id_right_
  → if require_both && !(hand_id_left_ && hand_id_right_)
      → throw std::runtime_error("--hand both: missing <L|R|both> on bus")
      → main.cpp catches → LOG_ERROR + return 2
      → ~XHandDriver swallows shutdown error; ctl_ closes via SDK dtor
main.cpp
  → wait_first_valid_frame(receiver, udcap.startup_timeout_s seconds, shutdown_flag)
      → blocks until receiver yields frame (already both-calib gated)
      → on timeout: LOG_ERROR + return 2 (ADR-036)
loop
  → first tick consumes primed_frame via std::move (M6 §3.2)
```

### 2.4 Shutdown

```
SIGINT / SIGTERM (ADR-023, std::signal handlers from M6)
  → shutdown_flag.store(true)
  → loop exits
  → driver->shutdown()
      → for each held hand_id (Left then Right): send mode=0 HandCommand_t
      → LOG_INFO("Shutdown: mode=0 (passive)")
      → XHandControl::close_device()
      → LOG_INFO("Device closed")
  → latency_stats.summary printed
```

Already correct in `XHandDriver::shutdown()` (xhand_driver.cpp:94-109) — sends mode=0 to whichever of `hand_id_left_` / `hand_id_right_` are set, then one `close_device()`.

---

## 3. 模块规约 (delta-only)

### 3.1 `XHandDriver::open(bool require_both)` (xhand_driver.cpp:21)

Add one parameter; preserve current discovery loop verbatim. After the loop:

```cpp
void XHandDriver::open(bool require_both) {
    // ... existing serial open + list_hands_id + per-id get_hand_type loop ...

    if (require_both && !(hand_id_left_ && hand_id_right_)) {
        std::string missing;
        if (!hand_id_left_)  missing += "Left ";
        if (!hand_id_right_) missing += "Right";
        throw std::runtime_error(
            "open(require_both=true): missing hand(s) on bus: " + missing);
    }
}
```

**Behavioral invariant**: when caller passes `require_both = false` (the M5c / `--hand left` / `--hand right` / `--actions` paths), discovery succeeds as long as ≥1 hand is on the bus — backward compatible with every M5c / M6 acceptance test.

### 3.2 `main.cpp` (one line — line 326)

Replace `driver->open();` with:

```cpp
driver->open(/*require_both=*/args.hand == cli::HandSelect::Both);
```

No other change. The post-open `args.hand`-gated dispatch in the loop (main.cpp:412 / :421) already does the right thing — it only calls `send_left` if `args.hand != Right`, and only `send_right` if `args.hand != Left`. With `require_both=true`, both `hand_id_*_` are guaranteed populated, so neither `send_*` will throw the "not discovered" error from xhand_driver.cpp:82 / :89.

### 3.3 `config.yaml` — right-hand tuning protocol

Current `mapping.right` (SPEC §11) mirrors `mapping.left` 1:1. Single-finger tests in §4.3 verify each right joint's direction. Expected change classes:

1. **Sign flip** (`sign: -1 ↔ +1`): the high-probability change. Per SPEC §10 risk 5 "right hand may need sign negation". Anticipated on a subset of joints; budget for 0–6 flips.
2. **Clamp narrowing** (`clamp: [a, b]`): unlikely. Only if right hardware's safe range demonstrably differs from left.
3. **Weight change** (`weights: […]`): VERY unlikely. Anatomical fusion ratios shouldn't differ by hand. Treat as red flag if you find yourself reaching for this — re-examine whether the underlying sign or source-index is actually correct.

**Per-edit cadence** (LOCAL workstation steps; commit-and-push between PC2 iterations):

```bash
# LOCAL (macOS) — for each accepted right-hand edit:
$EDITOR config.yaml                  # change one joint, one field
python3 scripts/dump_mapper_baseline.py \
    --example example.json \
    --config  config.yaml \
    --out     tests/fixtures/mapper_baseline.json
git diff config.yaml tests/fixtures/mapper_baseline.json
# Verify: only mapping.right.<joint>.<field> changed; left values byte-identical;
#         baseline json diff restricted to config_yaml_sha256, generated_at,
#         and the right-hand entries for that joint.
git add config.yaml tests/fixtures/mapper_baseline.json
git commit -m "M7: right hand <joint> <sign|clamp> flip + fixture regen"
git push origin main
# Then on PC2: git pull + rebuild + re-run §4.3 to confirm direction now correct.
```

ADR-037 is non-negotiable here: every `config.yaml` edit MUST carry its fixture-regen in the same commit, or the next PC2 P0 build trips the SHA self-check.

---

## 4. 测试策略

All PC2 commands assume:
- PC2 user `unitree`, repo at `~/projects/udex_to_xhand`, branch `main`.
- `~/projects/udex_to_xhand/docs/logs/` exists (it does — populated by M5c / M6).
- SDK shared library + vendor headers already installed (M5a, 2026-05-18).

Each step lists explicit pass / fail criteria. Tests run in numerical order; a fail blocks subsequent steps (ChatGPT/Gemini: this ordering is intentional, mirrors M5c §11 + M6 §4).

### 4.0 LOCAL · regen snapshot fixture before any config.yaml-touching commit

(See §3.3 cadence — this is the policy, not a one-shot test.)

### 4.1 PC2 · P0 build + unit gates

```bash
# PC2
cd ~/projects/udex_to_xhand
git pull origin main
mkdir -p build && cd build
cmake -DBUILD_TESTS=ON .. 2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-cmake-$(date +%F).log
make -j$(nproc)            2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-make-$(date +%F).log

# Snapshot equivalence (Python oracle vs C++ mapper, byte-for-byte SHA check)
./test_mapper_snapshot \
    --fixture ../tests/fixtures/mapper_baseline.json \
    --example ../example.json \
    --config  ../config.yaml \
    2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-snapshot-$(date +%F).log

# Watchdog + clamp unit tests (hand-rolled, M6-introduced)
./test_safety \
    2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-test-safety-$(date +%F).log
```

**Expected**:
- `cmake` exits 0; `make` exits 0 with `-Wall -Wextra -Wpedantic` zero warnings.
- `udex_to_xhand` ELF aarch64 produced (`file ./udex_to_xhand` → `ELF 64-bit … aarch64`).
- `test_mapper_snapshot` exits 0; stdout includes `L max |Δ| = 0.0e+00 rad` and `R max |Δ| ≤ 1.4e-17 rad`.
- `test_safety` exits 0; stdout ends with `all checks passed`.

**Fail handling**:
- SHA-256 mismatch on snapshot → root cause is `config.yaml` or `example.json` drift since last LOCAL regen. STOP. Re-run §3.3 regen on LOCAL workstation, commit, push, retry PC2 build. (Same wire as M6 P0 — ADR-037 designed for this.)
- Snapshot value drift (max |Δ| > 1e-6) → mapper regression. STOP, do NOT continue M7; bisect via `git log -- src/joint_mapper.* config.yaml`.
- Test-safety failure → safety regression. STOP, do NOT proceed to hardware tests.

### 4.2 PC2 · hardware enumeration (dual XHand on single CDC-ACM)

Goal: confirm one `/dev/ttyACM*` enumerates BOTH `hand_id`s via multi-drop RS485.

```bash
# PC2 — physical setup: connect both XHands to the SAME USB-to-RS485 adapter on
# PC2 (multi-drop wiring). Power both. Confirm device node:
ls -l /dev/ttyACM*
# Expected: at least /dev/ttyACM0 enumerated. Note the path.

# Use the vendor sample (re-used since M5a — ADR-024) to probe IDs on the bus:
cd ~/projects/udex_to_xhand/xhand_control_sdk/tests/build
./test_serial 2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-enum-$(date +%F).log
```

**Expected stdout snippet** (mirrors M5a 2026-05-18 result but with two ids):
```
open_serial(/dev/ttyACM0, 3000000) … OK
list_hands_id: [0, 1]                  ← or whatever cfg.left/right_hand_id are
get_hand_type(0) → "Left"  SN=<…>
get_hand_type(1) → "Right" SN=<…>
```

**PASS**: both expected `hand_id`s enumerate AND `get_hand_type` reports `L` / `R` matching `config.yaml` (`xhand.left_hand_id: 0`, `xhand.right_hand_id: 1`).

**FAIL (any of the following)**:
- Only one id appears.
- Both ids appear but `get_hand_type` returns the wrong side or unrecognized.
- `open_serial` fails / bus errors.

→ **STOP**. Do NOT attempt to silently split to two adapters or rewrite the driver. The fallback (two `XHandControl` instances on two CDC-ACM ports) is a design change, not an M7 deviation. Open a new plan revision / ADR before proceeding. Document the failure mode in §8 Execution Record + Risks (R1).

### 4.3 PC2 · right-hand single-finger flex verification

Pre-req: §4.2 PASS.

Goal: validate each joint of `mapping.right` against the right UDCAP glove. M4 ran this for the left hand at the protocol level; M7 does the same for the right. Iterative: tune one joint, regen fixture, push, pull on PC2, retest.

```bash
# PC2 — operator wears RIGHT UDCAP glove only (left optional but receiver
# requires both calib==3 to forward frames — operator should wear left glove too
# so it sits in calibrated state, hand can be still).
cd ~/projects/udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --hand right --duration 120 \
    2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-right-finger-$(date +%F)-iter<N>.log
```

**Right-hand checklist** (replicates M4 §verification on the mirror side):

- [ ] Right thumb flexion → J0 (thumb_bend) follows direction; J1 / J2 stay stable.
- [ ] Right thumb opposition / yaw → J1 (thumb_rota1) follows.
- [ ] Right thumb roll → J2 (thumb_rota2) follows. (Source: r20 — non-contiguous per ADR-013.)
- [ ] Right index abduction (lateral) → J3 (index_bend) follows.
- [ ] Right index proximal flex → J4 (index_joint1) follows.
- [ ] Right index distal flex → J5 (index_joint2) follows.
- [ ] Right middle flex → J6 + J7 follow.
- [ ] Right ring flex → J8 + J9 follow.
- [ ] Right pinky flex → J10 + J11 follow.
- [ ] Right fist → all fingers close (no joint reverses).
- [ ] Right open palm → all fingers extend.

**PASS criterion** for the milestone: every checklist item passes WITHOUT a wrong direction at the joint. Within-iteration FAIL (wrong direction on a joint) triggers:

1. `Ctrl+C` / SIGTERM the running binary on PC2 (foreground OK here — operator visual is the source of truth; log truncation is acceptable for this test, M6 known issue).
2. LOCAL: flip the relevant `sign` (or rarely, adjust `clamp`) in `config.yaml` `mapping.right.<joint>`. Run §3.3 regen cadence, commit, push.
3. PC2: `git pull origin main && cd build && make -j$(nproc)`, then rerun this §4.3 with `iter<N+1>` in the log name.

Budget: SPEC §10 risk 5 says HIGH probability of sign flips, but for a small number of joints (0–6). If iteration count exceeds 6 sign flips on different joints, STOP and re-examine — likely a deeper convention mismatch (e.g., UDCAP right-hand parameter convention is fundamentally mirrored, not per-axis); document in §8 + new ADR.

### 4.4 PC2 · dual-hand simultaneous teleop

Pre-req: §4.2 PASS, every §4.3 checklist item PASS.

```bash
# PC2 — operator wears BOTH gloves.
cd ~/projects/udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --hand both --duration 60 \
    2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-dual-teleop-$(date +%F).log
```

**Verification checklist** (roadmap §M7 + SPEC §10 risk 3):

- [ ] Startup gate passes within 10 s; log shows both `hand_id=0 type=Left` and `hand_id=1 type=Right` (order may vary; both must appear).
- [ ] First-packet log line: `first packet from <192.168.x.y>`.
- [ ] Move ONLY left glove (right still): only left XHand visibly moves.
- [ ] Move ONLY right glove (left still): only right XHand visibly moves.
- [ ] Both gloves fist simultaneously: both XHands close, no perceptible inter-hand lag.
- [ ] Both gloves open palm: both XHands extend.
- [ ] Mixed gesture (left fist + right OK): each hand behaves independently and correctly.
- [ ] No `[ERROR]` lines during the run.
- [ ] Exit prints `latency_ms{n=<N> avg=<X> p95=<Y> max=<Z>}` and `valid frames` ≈ expected `60s × ~60-120 Hz × (post-deadline-loop ratio)`.

**Latency budget** (SPEC §9 phase 3.9): `p95 ≤ 20 ms` AND `max ≤ 50 ms`. Single-hand M5c baseline was avg=9.60 / p95=9.62 / max=10.68 ms; dual is expected to be ≈ 1.5–2× since `send_command` is invoked twice per tick (RS485 serializes the two HandCommand_t payloads on the same bus). Anticipated dual numbers: avg ~15–20 ms, p95 ~17–22 ms, max ~25 ms.

**FAIL (within budget but with concern)**:
- `p95 > 20 ms` AND `max ≤ 50 ms`: log as ADR-043 candidate. Document and continue to §4.5.
- `max > 50 ms` OR cycle Hz < 80: STOP. SPEC §9 threshold violated. Likely RS485 contention (Risk R3); investigate before declaring M7 done.

### 4.5 PC2 · dual-hand safety re-validation

Pre-req: §4.4 PASS.

Re-runs the M6 P1 / P2b / P3 / P5 scenarios in dual context. Each step proves the M6 safety contract still holds when both hands are in motion.

#### 4.5 P1' · dual-hand watchdog

```bash
# PC2 — terminal 1: start in background (M6 §8.4 deviation lesson — foreground +
# tee + ^C truncates log; backgrounding + SIGTERM keeps the log intact).
cd ~/projects/udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --hand both --duration 30 \
    > ~/projects/udex_to_xhand/docs/logs/m7-watchdog-dual-$(date +%F).log 2>&1 &
M7_PID=$!
echo "udex_to_xhand started, pid=$M7_PID"

# Wait until first-packet log appears (~few seconds), then turn off UDCAP on
# Windows. Wait ~10 s. Then turn UDCAP back on. Wait ~3 s.
# Then graceful shutdown:
kill -TERM "$M7_PID"
wait "$M7_PID"
```

**Expected log evidence**:
- ~10 `[WARN] watchdog: no UDP for >200ms, holding last position` lines (1 Hz rate-limit, ADR-035).
- Single `[INFO] watchdog: recovered after <N>ms` line on UDCAP return (N bounded to [0, 1000] ms per ADR-038 — the semantic, NOT total outage).
- Both hands physically held last commanded posture during the 10-s outage (operator visual).
- After SIGTERM: `Shutdown: mode=0 (passive)` (one INFO line, even though `XHandDriver::shutdown()` sends mode=0 to both ids internally), then `Device closed`, then `latency_ms{...}`, then `exited after`.

#### 4.5 P2b' · dual-hand SIGTERM

Already covered tail-end of P1'. Verify log captures the full shutdown sequence end-to-end (no truncation).

#### 4.5 P3' · dual-hand clamp on right-side joint

```bash
# LOCAL — edit mapping.right.index_joint1.clamp from current [0, 110] to [0, 30].
# Run §3.3 regen, commit "M7: P3' right index clamp narrow test (revert after)",
# push.

# PC2:
cd ~/projects/udex_to_xhand && git pull origin main && cd build && make -j$(nproc)
./udex_to_xhand --config ../config.yaml --hand both --duration 30 \
    2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-clamp-dual-$(date +%F).log
# Operator wears both gloves. Flex RIGHT index to the limit (overshoot 30°).
# Expected: right XHand J4 stops at ~30° (clamp), LEFT index J4 unaffected.
```

**After test**: REVERT the test edit (back to `[0, 110]`), regen fixture, commit "M7: revert P3' test clamp", push. Otherwise the right index will be stuck at 30° for §4.4 future runs.

#### 4.5 P5' · dual-hand startup gate timeout

```bash
# PC2 — ensure UDCAP is OFF (or gloves not worn / calib < 3 on either side).
cd ~/projects/udex_to_xhand/build
timeout 15 ./udex_to_xhand --config ../config.yaml --hand both \
    2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-startup-gate-dual-$(date +%F).log
echo "exit=$?"
```

**Expected**:
- After ~10 s: `[ERROR] startup gate: no calibrated UDP frame in 10s; aborting`.
- Exit code `2` (ADR-036).
- Driver destructor still runs `shutdown()` → log shows `mode=0 (passive)` + `Device closed` (both hands receive mode=0 because both were discovered in `open()`).

#### 4.5 P6' (new for M7) · `--hand both` fail-closed on partial discovery

Verifies the §3.1 / §3.2 code change.

```bash
# PC2 — disconnect ONE XHand from the bus (e.g., unplug right hand power +
# data). Confirm only one id enumerates:
cd ~/projects/udex_to_xhand/xhand_control_sdk/tests/build
./test_serial   # expect list_hands_id: [0]  (or just [1])

# Then attempt --hand both:
cd ~/projects/udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --hand both --duration 5 \
    2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-fail-closed-$(date +%F).log
echo "exit=$?"
```

**Expected**:
- `[ERROR] XHandDriver: open(require_both=true): missing hand(s) on bus: Right` (or `Left`, depending on which one was unplugged).
- Exit code `2` (main.cpp:329, M6 unchanged).
- NO `[INFO] XHand SDK version` after the missing-hand log (driver dtor still cleans up, but the binary exits before the control loop starts).

**Cross-check (regression guard)** — single-hand path must remain backward compatible:

```bash
# Reconnect the missing hand, then verify single-hand mode still opens with
# only-one-side-required semantics (no regression from the new require_both
# parameter defaulting to false in the M5c code paths).
./udex_to_xhand --config ../config.yaml --hand left --duration 5 \
    2>&1 | tee ~/projects/udex_to_xhand/docs/logs/m7-single-regression-$(date +%F).log
# Expected: no failure on right-side discovery; left teleop runs for 5s.
# (Operator wears both gloves so try_recv passes calib==3 on both sides.)
```

---

## 5. 验证命令 (合并速查)

```bash
# === LOCAL (macOS) ===
# Pre-flight if you'll be editing config.yaml in this session:
cd ~/Desktop/4-2/xhand/udex_to_xhand
python3 scripts/dump_mapper_baseline.py \
    --example example.json --config config.yaml \
    --out tests/fixtures/mapper_baseline.json
git diff config.yaml tests/fixtures/mapper_baseline.json

# === PC2 (ssh unitree@<pc2-ip>) ===
# §4.1 — build + unit gates
cd ~/projects/udex_to_xhand && git pull origin main
mkdir -p build && cd build
cmake -DBUILD_TESTS=ON .. && make -j$(nproc)
./test_mapper_snapshot --fixture ../tests/fixtures/mapper_baseline.json \
    --example ../example.json --config ../config.yaml
./test_safety

# §4.2 — hardware enumeration (dual XHand on single CDC-ACM)
cd ~/projects/udex_to_xhand/xhand_control_sdk/tests/build && ./test_serial

# §4.3 — right-hand single-finger flex (iterate until checklist clean)
cd ~/projects/udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --hand right --duration 120

# §4.4 — dual-hand simultaneous teleop
./udex_to_xhand --config ../config.yaml --hand both --duration 60

# §4.5 P1' — dual watchdog (background pattern; toggle UDCAP off~10s then on)
./udex_to_xhand --config ../config.yaml --hand both --duration 30 \
    > docs/logs/m7-watchdog-dual-$(date +%F).log 2>&1 &
M7_PID=$!; sleep 30; kill -TERM "$M7_PID"; wait "$M7_PID"

# §4.5 P3' — clamp test (after LOCAL narrow + regen + push + PC2 pull + make)
./udex_to_xhand --config ../config.yaml --hand both --duration 30

# §4.5 P5' — startup gate timeout (UDCAP off)
timeout 15 ./udex_to_xhand --config ../config.yaml --hand both

# §4.5 P6' — fail-closed (one XHand physically disconnected)
./udex_to_xhand --config ../config.yaml --hand both --duration 5
```

---

## 6. Commit 计划

Commits land on `main` locally first; push only when user approves (same policy as M6).

Expected sequence (5–10 commits depending on §4.3 iteration count):

1. `M7: XHandDriver::open(require_both) fail-closed on partial discovery` — `src/xhand_driver.{hpp,cpp}` + `src/main.cpp` (one-line update at main.cpp:326).
2. `M7: right hand <joint> sign|clamp fix + fixture regen` — repeated per accepted §4.3 edit. 0–N commits. Each touches `config.yaml` + `tests/fixtures/mapper_baseline.json` together (ADR-037).
3. `M7: PC2 hardware enumeration log (dual CDC-ACM multi-drop)` — `docs/logs/m7-enum-*.log`.
4. `M7: PC2 right-hand finger-flex verification logs` — `docs/logs/m7-right-finger-*-iter*.log`.
5. `M7: PC2 dual-hand teleop log` — `docs/logs/m7-dual-teleop-*.log`.
6. `M7: PC2 dual-hand safety re-validation logs (watchdog / clamp / startup-gate / fail-closed)` — 4 dual logs from §4.5.
7. `M7: revert P3' clamp test config (post-§4.5 P3')` — `config.yaml` + fixture regen back to [0, 110].
8. `M7 ✅: dual-hand integration verified on G1 PC2` — SPEC.md §3 / §10 / §12 + roadmap §M7 ✅ + plan §8 Execution Record fill-in + roadmap revision history line.

---

## 7. ADR 候选

### Pre-register (write before execution if appropriate, or as part of commit 8 above)

- **ADR-039**: Dual XHand multi-drop RS485 single-port architecture is the M7-confirmed design — one `XHandControl` instance addresses two `hand_id`s. Context cites §4.2 PASS evidence; Consequences note PC2 USB budget relief; Alternatives lists two-port fallback (rejected unless §4.2 fails) and EtherCAT (deferred, SPEC §2 already lists this as upgrade path).
- **ADR-040**: `--hand both` is **fail-closed** on partial hand discovery. Decision: `open(require_both=true)` throws; main.cpp exits 2; driver dtor runs mode=0 + close on whichever hand WAS discovered. Rationale: silent single-hand degradation under explicit dual request would mask hardware faults. The default `require_both=false` for the M5c-era single-hand / `--actions` paths preserves backward compatibility.

### Likely-from-execution (write only if triggered)

- **ADR-041** (write IF §4.3 forces ≥1 right-hand sign flip): document which joints flipped, the UDCAP axis-convention rationale (cite SPEC §10 risk 5 / §12 open question 3), and the verification log line for each. If §4.3 passes with zero flips, this ADR is skipped and §8 records "right-hand starting hypothesis (sign mirror = left) verified, no flips needed."
- **ADR-042** (write IF §4.4 yields `p95 > 20 ms`): document observed latency, cause (likely RS485 contention since two HandCommand_t serialize on one bus), and chosen mitigation. Candidate mitigations: (a) accept (still under 50 ms ceiling), (b) interleave send_left/send_right across alternating ticks (loses 1-tick sync but halves per-tick bus load), (c) drop `update_rate_hz` to 50, (d) two USB adapters → two `XHandControl` instances (architecture change, much bigger plan revision).

---

## 8. Execution Record (FILL AT END OF M7)

### 8.1 Environment

- PC2: `<TBD — kernel, SDK version, USB topology>`
- Hardware: `hand_id=0 type=Left SN=<…>`, `hand_id=1 type=Right SN=<…>`
- UDCAP source: Windows PC at `<192.168.x.y>` UDP 9000, both gloves worn.
- Bus topology: `<single /dev/ttyACMn multi-drop | two adapters — fill from §4.2 result>`

### 8.2 Right-hand sign / clamp edits applied

| Joint | Field | Before | After | Commit | Notes |
| ----- | ----- | ------ | ----- | ------ | ----- |
| _e.g._ `mapping.right.thumb_rota2` | _sign_ | _+1_ | _-1_ | _\<hash\>_ | _right-hand thumb roll inverted vs left convention_ |
| ... | | | | | |

If empty: "No right-hand edits needed; left-mirror starting hypothesis verified."

### 8.3 Latency (dual-hand 60s session, §4.4 log)

| Metric | Value | vs M5c single-hand (9.60 / 9.62 / 10.68) | Notes |
| ------ | ----- | ---------------------------------------- | ----- |
| n (valid frames) | _TBD_ | | |
| avg_ms | _TBD_ | | |
| p95_ms | _TBD_ | | SPEC §9 budget: ≤ 20 ms |
| max_ms | _TBD_ | | SPEC §9 ceiling: ≤ 50 ms |

### 8.4 Deviations from plan

(list any forced changes; if none, write "none")

### 8.5 Known issues / follow-ups (carry to M8 risk register if needed)

(latency outliers, RS485 contention symptoms, PC2 USB warmth, etc.)

---

## 9. Risks (M7-specific)

| # | Risk | Probability | Impact | Mitigation |
| - | ---- | ----------- | ------ | ---------- |
| R1 | RS485 multi-drop fails — only one `hand_id` enumerates on single port | MEDIUM | HIGH | §4.2 STOP-and-replan rule. Fallback (two adapters → two `XHandControl` instances) is a new plan, not silent. |
| R2 | Right-hand sign tuning iterates > 6 times — suggests deeper convention mismatch | LOW | MEDIUM | Stop, re-examine in case the UDCAP right-hand parameter axis convention is fundamentally mirrored vs left (M2 only verified left). Document + new ADR. |
| R3 | Dual `send_command` per tick saturates RS485 — cycle latency drops update Hz below 80 | MEDIUM | MEDIUM | §4.4 PASS criterion catches this. ADR-042 candidate covers mitigations. |
| R4 | Dual-hand startup gate races: one glove calibrates after the other — 10 s window insufficient | LOW | LOW | Acceptable; operator dons both gloves first. If 10 s insufficient in practice, bump `udcap.startup_timeout_s` per ADR-036. |
| R5 | M6 SIGINT-log-truncation pattern reappears under dual mode | HIGH | LOW | §4.5 P1' uses backgrounded process + `kill -TERM $PID` (M6 §8.4 lesson). Visual is source of truth for SIGINT; logs target SIGTERM. |
| R6 | Right XHand hardware unavailable / damaged | LOW | HIGH | §4.2 catches early. If fails, M7 is blocked on hardware procurement. No silent single-hand fallback. |
| R7 | New `require_both` parameter inadvertently breaks an existing M5c / M6 `--hand left|right` workflow | LOW | MEDIUM | §4.5 P6' cross-check rerun. `require_both` defaults to `false`; all pre-M7 call sites are unchanged at the source level. |

---

## 10. Revision 2 — Two-Port Split Architecture (2026-05-19)

### 10.0 Why this revision exists

PC2 §4.1 build succeeded; first §4.2 probe (`./udex_to_xhand --port /dev/ttyACMx --hand left --duration 3`) revealed:

- `/dev/ttyACM0` does NOT exist on PC2. USB enumeration assigned `ttyACM1`..`ttyACM4`.
- `dmesg` shows **two physical USB devices** (`usb 1-2.2`, `usb 1-2.3`), each a CDC composite exposing one primary control endpoint and one auxiliary endpoint:
  - `usb 1-2.2 → ttyACM1` (primary, 1.0) + `ttyACM4` (aux, 1.2) — physically **Right XHand**
  - `usb 1-2.3 → ttyACM2` (primary, 1.0) + `ttyACM3` (aux, 1.2) — physically **Left XHand**
- Operator probe (2026-05-19) confirmed: `/dev/ttyACM2` → Left, `/dev/ttyACM1` → Right.

This is **NOT** the single-port RS485 multi-drop topology that rev1 §0 / §3 / §4.2 / §7 assumed. Each XHand sits on its own USB-to-RS485 path with its own CDC-ACM node. Per rev1 §4.2 FAIL criterion + §9 R1: STOP and re-plan, do not silently split.

### 10.1 Architecture decision (user-approved 2026-05-19)

**Q1 = A**: `main.cpp` holds **two `XHandDriver` instances** (`driver_left`, `driver_right`), each owns one `XHandControl` instance on its own `/dev/ttyACMx`. `XHandDriver` itself stays a single-port, single-hand wrapper (its existing logic — enumerate `list_hands_id()`, label via `get_hand_type()` — already accommodates the "one hand per bus" case unchanged).

**Q2 = i**: the rev1 `XHandDriver::open(require_both)` parameter is reverted (commit-level revert of `b95ac4b`). Under two-driver model, `require_both` has no caller — fail-closed semantics move into `main.cpp`, which checks `driver_left->has_left()` / `driver_right->has_right()` after each `open()`. `has_both()` is also dropped (main.cpp directly checks `driver_left && driver_right`).

### 10.2 Section-by-section overrides

The following rev1 sections are SUPERSEDED by the rev2 specifications below. Rev1 prose remains in place for review/historical context; rev2 governs implementation.

#### 10.2.1 Override §1 — File list

**Modified** (delta from rev1 §1; ⊖ = retracted from rev1, ⊕ = new in rev2):

| File | rev2 delta |
| --- | --- |
| `src/xhand_driver.hpp` | ⊖ `require_both` arg + `has_both()` removed. File returns to pre-M7 (M5c/M6) shape. |
| `src/xhand_driver.cpp` | ⊖ Same — `open()` reverts to no-arg, no fail-closed throw. |
| `src/main.cpp` | ⊕ `XHandConfig` schema gains `left_serial_port` / `right_serial_port` (replaces single `serial_port`). `load_xhand_config` reads new keys. FULL-mode init builds 0/1/2 `XHandDriver` instances based on `--hand`. `run_actions` mirrors. Stale resend / shutdown / loop dispatch updated to address `driver_left` / `driver_right` independently. |
| `config.yaml` | ⊕ Schema migration: `xhand.serial_port: "/dev/ttyACM0"` → `xhand.left_serial_port: "/dev/ttyACM2"` + `xhand.right_serial_port: "/dev/ttyACM1"`. Legacy `left_hand_id` / `right_hand_id` (never read by code) deleted. |
| `tests/fixtures/mapper_baseline.json` | Regenerated in same commit as `config.yaml` (ADR-037). Diff restricted to `config_yaml_sha256` + `generated_at`; mapper values byte-identical (no `mapping.*` changes). |
| `docs/decisions/039-rs485-two-port-split.md` | ⊕ NEW ADR documenting the two-port choice + retiring the rev1 ADR-040 pre-registration. |

**Unchanged from rev1 §1**: `mapping.right` tuning workflow (§4.3 still applies); `docs/plans/00-roadmap.md` + `SPEC.md` updates on completion. `src/udcap_receiver.*` / `src/joint_mapper.*` / `src/safety.*` / `src/cli.*` still untouched.

#### 10.2.2 Override §3 — Module specs

##### 3.1' `XHandDriver` (no functional change vs M5c/M6)

`open()` keeps its pre-M7 signature `void open()`. It opens its port, enumerates `list_hands_id()`, labels each id via `get_hand_type()` → `'L'`/`'R'`/other. Throws on serial-open failure or empty hand list. Single-bus-with-one-hand case is naturally handled: exactly one of `hand_id_left_` / `hand_id_right_` ends up populated, the other stays `nullopt`. **No new methods; no new parameters.**

##### 3.2' `XHandConfig` + `load_xhand_config` (src/main.cpp)

```cpp
struct XHandConfig {
    std::string left_serial_port {"/dev/ttyACM2"};  // M7 rev2 default — physical Left on PC2
    std::string right_serial_port{"/dev/ttyACM1"};  // M7 rev2 default — physical Right on PC2
    int baud_rate{3000000};
    int update_rate_hz{100};
    XHandPID pid{};
};

XHandConfig load_xhand_config(const YAML::Node& root) {
    XHandConfig c;
    auto x = root["xhand"];
    if (x) {
        if (x["left_serial_port"])  c.left_serial_port  = x["left_serial_port"].as<std::string>();
        if (x["right_serial_port"]) c.right_serial_port = x["right_serial_port"].as<std::string>();
        if (x["baud_rate"])         c.baud_rate         = x["baud_rate"].as<int>();
        if (x["update_rate_hz"])    c.update_rate_hz    = x["update_rate_hz"].as<int>();
        if (x["default_kp"])        c.pid.kp            = x["default_kp"].as<int>();
        if (x["default_ki"])        c.pid.ki            = x["default_ki"].as<int>();
        if (x["default_kd"])        c.pid.kd            = x["default_kd"].as<int>();
        if (x["default_tor_max"])   c.pid.tor_max       = x["default_tor_max"].as<int>();
        if (x["control_mode"])      c.pid.mode          = x["control_mode"].as<int>();
    }
    return c;
}
```

##### 3.3' `--port` override semantics (cli + main)

`--port` is the **single-side** port override. Combination matrix:

| `--hand` | `--port /dev/ttyACMx` | Effect |
| --- | --- | --- |
| `left`  | provided | `xhand_cfg.left_serial_port  = x` |
| `right` | provided | `xhand_cfg.right_serial_port = x` |
| `both`  | provided | **cli error**, exit 2 — "use config.yaml for dual-hand ports" |
| any     | absent   | use config.yaml values verbatim |

Check in `main()` and `run_actions()` after `load_xhand_config()`.

##### 3.4' FULL-mode driver setup (src/main.cpp, replacing rev1 §3.2)

```cpp
std::optional<XHandDriver> driver_left, driver_right;
const bool want_left  = (args.hand != cli::HandSelect::Right);
const bool want_right = (args.hand != cli::HandSelect::Left);

if (!args.mock && !args.receiver_only) {
    try {
        if (want_left) {
            driver_left.emplace(xhand_cfg.left_serial_port, xhand_cfg.baud_rate, xhand_cfg.pid);
            driver_left->open();
            if (!driver_left->has_left()) {
                LOG_ERROR("xhand.left_serial_port=" << xhand_cfg.left_serial_port
                          << " did not discover a Left hand (got "
                          << (driver_left->has_right() ? "Right" : "none") << ")");
                return 2;
            }
        }
        if (want_right) {
            driver_right.emplace(xhand_cfg.right_serial_port, xhand_cfg.baud_rate, xhand_cfg.pid);
            driver_right->open();
            if (!driver_right->has_right()) {
                LOG_ERROR("xhand.right_serial_port=" << xhand_cfg.right_serial_port
                          << " did not discover a Right hand (got "
                          << (driver_right->has_left() ? "Left" : "none") << ")");
                return 2;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("XHandDriver: " << e.what());
        return 2;
    }
}
```

The sanity check (`has_left()` / `has_right()` post-open) is the rev2 equivalent of rev1's `require_both` — it now also catches cabling errors (left/right ports swapped).

##### 3.5' Main loop dispatch (rev1 §3.2 "args.hand-gated" logic now points at separate drivers)

```cpp
if (args.hand != cli::HandSelect::Right) {
    auto v = mapper.map_left(frame_opt->l);
    safety::clamp_in_place(v);
    left_rad = v;
    if (driver_left) {
        driver_left->send_left(v);
        last_left_rad = v;
    }
}
if (args.hand != cli::HandSelect::Left) {
    auto v = mapper.map_right(frame_opt->r);
    safety::clamp_in_place(v);
    right_rad = v;
    if (driver_right) {
        driver_right->send_right(v);
        last_right_rad = v;
    }
}
```

Stale-resend branch:

```cpp
} else if ((driver_left || driver_right) && wdog.has_seen_frame() && wdog.is_stale(tick_start)) {
    try {
        if (driver_left  && last_left_rad)  driver_left ->send_left (*last_left_rad);
        if (driver_right && last_right_rad) driver_right->send_right(*last_right_rad);
    } catch (...) { /* ... unchanged ... */ }
    /* ... rate-limited WARN unchanged ... */
}
```

Shutdown:

```cpp
if (driver_left)  { try { driver_left ->shutdown(); } catch (const std::exception& e) { LOG_WARN("shutdown(left): "  << e.what()); } }
if (driver_right) { try { driver_right->shutdown(); } catch (const std::exception& e) { LOG_WARN("shutdown(right): " << e.what()); } }
```

##### 3.6' `run_actions` (--actions mode) — same dual-driver pattern, scoped down

Build 1 or 2 drivers based on `--hand`. Each preset send loop dispatches via `driver_left->send_left()` / `driver_right->send_right()`. `--hand both --actions` is now supported: both XHands play the preset sequence in lockstep (useful for dual bring-up).

##### 3.7' `config.yaml` migration

```yaml
xhand:
  protocol: "RS485"
  left_serial_port:  "/dev/ttyACM2"   # M7 rev2: physical Left on PC2 (usb 1-2.3 primary)
  right_serial_port: "/dev/ttyACM1"   # M7 rev2: physical Right on PC2 (usb 1-2.2 primary)
  baud_rate: 3000000
  control_mode: 3
  default_kp: 100
  default_ki: 0
  default_kd: 0
  default_tor_max: 300
  update_rate_hz: 100
```

Removed keys: `serial_port` (replaced), `left_hand_id` / `right_hand_id` (never read by C++; hand id is auto-discovered per port). SHA-256 of `config.yaml` changes → `tests/fixtures/mapper_baseline.json` MUST be regenerated in the same commit (ADR-037).

#### 10.2.3 Override §4.2 — Hardware enumeration

The §4.2 PASS/FAIL fork is RESOLVED: hardware **is** two-port split (observed). The new §4.2 is a single sanity check per side:

```bash
# PC2 — confirm which CDC-ACM is which side (already done 2026-05-19; re-verify
# any time the USB cables are reseated):
cd ~/udex_to_xhand/build
for port in /dev/ttyACM1 /dev/ttyACM2 /dev/ttyACM3 /dev/ttyACM4; do
    echo "=== probing $port ==="
    ./udex_to_xhand --port "$port" --hand left --duration 1 2>&1 | head -10 || true
done | tee ~/udex_to_xhand/docs/logs/m7-enum-rev2-$(date +%F).log
```

PASS: exactly two ports return `hand_id=N type=Left|Right`. Match them against the values you write into `config.yaml`. Auxiliary endpoints (1.2 interface) should fail with `list_hands_id() returned empty` or a SDK 1501xxx error — that's expected (those are diagnostic ports).

#### 10.2.4 Override §4.5 P6' — fail-closed test

**Rev2 P6' scenario A** (port doesn't exist):

```bash
# Edit config.yaml left_serial_port to a non-existent path (e.g. /dev/ttyACM99),
# regen fixture, commit "M7: P6' test bad left port", push.
# PC2:
git pull && cd build && make -j$(nproc)
./udex_to_xhand --config ../config.yaml --hand both --duration 5 \
    2>&1 | tee ~/udex_to_xhand/docs/logs/m7-fail-closed-bad-port-$(date +%F).log
echo "exit=$?"
# Expected: open_serial(/dev/ttyACM99) → 1501039 → throw → main returns 2.
# Then REVERT the test edit + regen fixture + push.
```

**Rev2 P6' scenario B** (port exists but wrong side):

```bash
# Edit config.yaml: SWAP left_serial_port and right_serial_port (point left at the
# physical right's port and vice versa). Regen fixture, commit, push.
# PC2: pull + rebuild + run --hand both.
# Expected: open succeeds, but post-open check fires:
#   [ERROR] xhand.left_serial_port=/dev/ttyACM1 did not discover a Left hand (got Right)
# Exit 2. REVERT + regen + push.
```

Rev1 §4.5 P6' (single-port partial discovery) no longer applies and is RETIRED.

#### 10.2.5 Override §6 — Commit plan

Rev2 commit sequence (supersedes rev1 §6 commits 1 and 8; rev1 commits 2–7 still apply for the right-hand tuning + log archive phases):

1. **`M7 rev2: plan revision for two-port split topology`** — appends §10 to this plan file. (Already in flight as the user-facing rev2 commit.)
2. **`M7 rev2: revert require_both — superseded by two-port architecture`** — reverts commit `b95ac4b` content in `src/xhand_driver.{hpp,cpp}` + `src/main.cpp:326`. Returns to pre-M7 driver shape.
3. **`M7 rev2: two-XHandDriver dual-port + config schema + ADR-039`** — new `main.cpp` driver setup + loop dispatch + shutdown + run_actions; `config.yaml` schema migration; `tests/fixtures/mapper_baseline.json` regen; `docs/decisions/039-rs485-two-port-split.md`.
4. ...rev1 commits 2–7 unchanged (right-hand sign tuning + log archives).
5. **`M7 ✅: dual-hand integration verified on G1 PC2 (two-port split)`** — closeout.

#### 10.2.6 Override §7 — ADR candidates

- **ADR-039** (rev2): **RS485 two-port split** — each XHand on its own USB-to-RS485 path / its own CDC-ACM node. Decision: `main.cpp` instantiates two `XHandDriver` objects. Context cites this plan's §10.0 evidence; Alternatives list rejects (a) single-port multi-drop (not what the hardware exposes), (b) one `XHandDriver` holding two `XHandControl` instances (Q2-rejected). EtherCAT remains deferred per SPEC §2 upgrade path.
- **ADR-040 (rev1)**: **RETIRED** before write. The `--hand both` fail-closed semantic still applies (cabling / missing-port detection in `main.cpp` post-open), but it does NOT need a dedicated ADR — it's a straightforward "log + return 2" pattern matching ADR-036's startup-gate philosophy.
- ADR-041 / ADR-042 from rev1: unchanged (right-hand sign findings if any, latency findings if any).

#### 10.2.7 Override §9 — Risks

- **R1** (RS485 multi-drop fails): **CLOSED** — confirmed two-port split is the hardware reality. The risk did NOT materialize as "broken multi-drop"; it manifested as "the assumed topology was wrong". Lesson logged in §8 deviations.
- **R3** (dual `send_command` saturates one RS485): **CLOSED / re-scoped** — each hand has its own bus; bus contention is no longer a concern. Latency now depends on USB scheduling and SDK serialization. Re-measure in §4.4.
- **R7** (rev1 `require_both` regression): **CLOSED** — `require_both` reverted; no longer in the codebase.
- **NEW R8**: PC2 USB topology dependency — `/dev/ttyACM{1,2}` mapping is fragile. Replug order, reboot, or hub power changes can shift assignments. Mitigation: §4.2 rev2 probe + `config.yaml` declared with comment; document the physical-USB-port-to-hand mapping in `SPEC.md §2` on M7 closeout so reseating physical USB cables can be planned.
- **NEW R9**: Cabling swap (operator wires "left" cable to physical right hand) — caught by §3.4' `has_left()` / `has_right()` post-open check + clear LOG_ERROR.

---

