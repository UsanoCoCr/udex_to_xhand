# M8 Path A 操作员 Runbook — Calibrate-first Tuning

> **What this is**: plan §3 M8a Step A.5–A.8 + M8b Step B.6–B.10 + 错误恢复路径串成的一份连续可执行操作清单。**没有引入 plan 范围之外的新算法 / 新代码 / 新文件**；这是补一份执行级文档。
>
> **Prereq**: 4 个 M8 commit 已 push 到 `origin/main`（`b31abf6 / 116c6a9 / 2bbe998 / 6bf7128`，HEAD `ce23a58` 之上）。PC2 已通过 `git pull` 拿到。
>
> **预算**：≈ 1.5 h。Phase 0 build 15 min、Phase 1 双手 calibrate 30 min（含操作员脚本动作）、Phase 2 诊断 15 min、Phase 3 改 config + push 15 min、Phase 4 PC2 视觉验收 30 min。
>
> **回退闸**：任何 Phase 出问题 → PC2 上 `sed -i 's/use_new_retarget: true/use_new_retarget: false/' ../config.yaml` → 重启 binary，立刻回 M7（不需要 `git revert`）。

---

## Phase 0 — Pre-flight (PC2)

```bash
# PC2
cd ~/udex_to_xhand
git pull
git log --oneline -5
# 期望见到 4 个新 M8 commit:
#   b31abf6 M8c: thumb retargeting prototype + UDCAP recorder scripts
#   116c6a9 M8a: --actions calibrate-udcap mode for UDCAP range capture
#   2bbe998 M8a/M8b: use_new_retarget flag + affine rescale schema + flag-gating guard
#   6bf7128 M8: plan — tuning + acceptance ...
```

### USB 端口重 probe (ADR-042 — 每次 session 必做)

```bash
# PC2
ls -l /dev/ttyACM*
# 期望 2 个 CDC-ACM 节点。两次硬件 probe 之间端口号会漂（见 M7 ADR-042）。
grep -E 'left_serial_port|right_serial_port' ../config.yaml 2>/dev/null \
    || grep -E 'left_serial_port|right_serial_port' config.yaml
# 如果实测 ACM 号与 config.yaml 不一致：
#   - LOCAL 上改 config.yaml
#   - git add config.yaml && git commit -m "M8 Phase 0: re-probe ttyACM (L=X R=Y per ADR-042)"
#   - git push
#   - PC2 git pull
# 不一致就先修，再继续。
```

### Build + 单元测试

```bash
# PC2
mkdir -p ~/udex_to_xhand/build && cd ~/udex_to_xhand/build
cmake .. -DBUILD_TESTS=ON 2>&1 | tee ../docs/logs/m8-pathA-cmake-2026-05-21.log
make -j$(nproc) 2>&1 | tee ../docs/logs/m8-pathA-make-2026-05-21.log
# 期望: 0 warning(-Wall -Wextra -Wpedantic); 产出 udex_to_xhand + tests/test_mapper_snapshot + tests/test_safety 三个 ELF aarch64.

./tests/test_mapper_snapshot \
    --fixture ../tests/fixtures/mapper_baseline.json \
    --example ../example.json \
    --config  ../config.yaml \
    2>&1 | tee ../docs/logs/m8-pathA-snapshot-baseline-2026-05-21.log
# 期望末行: "[INFO] all 24 joints within tolerance"
# 失败诊断:
#   - "SHA-256 mismatch" → LOCAL 上有人改了 config / example 没 regen fixture;
#                          LOCAL 跑 python3 scripts/dump_mapper_baseline.py ...
#                          (见 Phase 3 末段的 regen 命令)
#   - "max |Δ| > 1e-6"   → C++ vs Python oracle 漂移; 报告 + STOP

./tests/test_safety 2>&1 | tee ../docs/logs/m8-pathA-test-safety-2026-05-21.log
# 期望: all pass (M6 ADR-035/036/037/038 行为不变).
```

---

## Phase 1 — UDCAP capture (PC2)

### 1a. 确认 Windows UDCAP → PC2 UDP 通路 (10s smoke)

```bash
# PC2 (在 build/ 目录)
./udex_to_xhand --config ../config.yaml --receiver-only --duration 10 \
    2>&1 | tee ../docs/logs/m8-pathA-receiver-smoke-2026-05-21.log
# 期望 stdout:
#   [INFO] config loaded: ../config.yaml
#   [INFO] first packet from <Windows IP>
#   [recv ...] L: l0=... R: r0=... | calib L=3 R=3 | fps=85.X
#
# 失败排查:
#   - 0 frames               → Windows UDCAP HandDriver 没指向 PC2 IP / 防火墙挡 9000 / 网线
#   - calib L≠3 或 R≠3       → 操作员需在 UDCAP UI 重新完成 calibration
#   - "OPEN_DEVICE_FAILED"   → 不应发生 (receiver-only 不开 XHand)，报告
```

### 1b. 左手 calibrate (30 s 操作员脚本)

```bash
# 操作员先戴左手 UDCAP，UDCAP UI 上 L_CalibrationStatus = 3.
# PC2:
./udex_to_xhand --actions calibrate-udcap --calibrate-duration 30 \
    --config ../config.yaml --hand left \
    2>&1 | tee ../docs/logs/m8a-calibrate-left-2026-05-21.log
```

**屏幕上一旦打出**：
```
[INFO] calibrate-udcap: capturing for 30.0s on 0.0.0.0:9000 (hand=left)
[INFO] operator script: 5s palm-open, 5s neutral, then 3s max-flex per finger ...
```
**操作员按下面节拍做动作**（计时从 INFO 出现开始）：

| 时段 | 动作 |
|---|---|
| 0 – 5 s | 手掌完全张开，五指伸直分开 |
| 5 – 10 s | 自然中性放松（半握） |
| 10 – 13 s | 拇指最大屈曲（指尖往掌心收） |
| 13 – 16 s | 食指最大屈曲（其余 4 指放松） |
| 16 – 19 s | 中指最大屈曲 |
| 19 – 22 s | 无名指最大屈曲 |
| 22 – 25 s | 小指最大屈曲 |
| 25 – 30 s | 完整握拳（5 指同时最大屈曲），同时拇指向手心方向用力 |

每 5 s 屏幕会打 `[INFO] calibrate: elapsed=5s frames_left=N ...`，**N 应 ≥ 450**（≥ 90 fps × 5s）。如果 N=0 → UDCAP 没在发；STOP，回 Phase 1a 排查。

**结束后 stdout 尾部一段 YAML 片段**（这是核心产物，保留它）：

```yaml
# ===== calibrate-udcap result =====
# duration=30s frames_left=2987 frames_right=0
# ----- left (2987 frames) -----
# 24 source min/max (sanity check; should each have max-min >= 30deg ...):
#   l0:  [-87.50, +4.20]    ← thumb DIP
#   l1:  [-90.10, +2.30]
#   ...                     (共 24 行)
# Paste under mapping.left.<joint>:
#   thumb_bend:    input_range: [-3.50, +85.40]
#   thumb_rota1:   input_range: [-22.10, +12.30]
#   thumb_rota2:   input_range: [-2.10, +28.40]
#   index_bend:    input_range: [-1.20, +22.50]
#   index_joint1:  input_range: [-5.20, +78.10]    ← 看这个！
#   index_joint2:  input_range: [-3.40, +71.90]
#   mid_joint1:    input_range: [-4.10, +75.30]
#   ...                     (共 12 行)
```

### 1c. 右手 calibrate (相同 30 s 脚本)

```bash
# 操作员换右手 UDCAP，R_CalibrationStatus = 3.
./udex_to_xhand --actions calibrate-udcap --calibrate-duration 30 \
    --config ../config.yaml --hand right \
    2>&1 | tee ../docs/logs/m8a-calibrate-right-2026-05-21.log
# 操作员重复 1b 的 30s 脚本（右手）。
```

### 1d. 把两段 YAML fragment 合并成审计文件

```bash
# PC2
mkdir -p ../docs/logs
{
  echo "# Combined calibration fragment from 2026-05-21"
  echo
  awk '/===== calibrate-udcap result =====/,/calibrate-udcap: done/' \
      ../docs/logs/m8a-calibrate-left-2026-05-21.log
  echo
  awk '/===== calibrate-udcap result =====/,/calibrate-udcap: done/' \
      ../docs/logs/m8a-calibrate-right-2026-05-21.log
} > ../docs/logs/m8a-calibrate-fragment-2026-05-21.log
cat ../docs/logs/m8a-calibrate-fragment-2026-05-21.log
```

---

## Phase 2 — 诊断 (any host, 5 min)

打开 `m8a-calibrate-fragment-2026-05-21.log`，对**每个四指关节**看 `input_range[1]`（max）:

| `input_range[1]` (max, deg) | 含义 | 建议动作 |
|---|---|---|
| ≤ 90 | **(b) UDCAP 戴 UDCAP 时输入打不满** —— 这是 M8 roadmap rev 11 / SPEC §10 R10 描述的根因 | Phase 3 Example A：affine rescale，不需要改 clamp |
| 90 – 110 | 临界 | Phase 3 Example A：affine rescale 推荐 |
| 110 – 130 | **(a) clamp 真的切了** | Phase 3 Example C：raise clamp + 同步把 output_range 调宽 |
| > 130 | 操作员可能撞 XHand 物理硬限位 | 保守 clamp [0, 120]，配合 affine rescale；记入 ADR |

**对拇指**（你诊断 2 的目标）：
- `thumb_rota2.input_range[1]` ≤ 25 而 `l20.max` ≤ 30 → UDCAP MCP-Roll 本身扫不开 → 推 M8c prototype（Algorithm B coupled affine）
- `thumb_rota2.input_range[1]` 在 30 – 50 之间 → 走 Phase 3 Example B：直接 affine rescale + offset，拇指向手心方向偏

---

## Phase 3 — 改 config (LOCAL) + push

### 3a. 编辑 `config.yaml` (选一个 Example，或混合)

LOCAL 上打开 `config.yaml`，按诊断结果改。下面 3 个 Example 都是 **plan §M8b in-scope** 的合法配置。

#### Example A — Affine rescale（四指 UDCAP 输入不足，最常见）

```yaml
mapping:
  use_new_retarget: true              # ← 关键: 从 false 切到 true
  left:
    thumb_bend:    { sources: [0,1,2], weights: [0.3,0.3,0.4], sign: -1, offset: 0,  clamp: [-10, 110],
                     input_range: [-3.5, 85.4],   output_range: [0, 110] }
    thumb_rota1:   { sources: [3],     weights: [1.0],          sign: -1, offset: 0,  clamp: [-10, 110],
                     input_range: [-22.1, 12.3],  output_range: [-10, 90] }
    thumb_rota2:   { sources: [20],    weights: [1.0],          sign:  1, offset: 15, clamp: [0, 90],
                     input_range: [-2.1, 28.4],   output_range: [0, 90] }
    index_bend:    { sources: [7],     weights: [1.0],          sign: -1, offset: 0,  clamp: [-10, 30],
                     input_range: [-1.2, 22.5],   output_range: [-10, 30] }
    index_joint1:  { sources: [6],     weights: [1.0],          sign: -1, offset: 0,  clamp: [0, 110],
                     input_range: [-5.2, 78.1],   output_range: [0, 110] }
    # ... 把 12 个关节全部按上面 schema 填好，input_range 用 calibration 实测值。
  right:
    # 同上 12 个关节；input_range 用右手实测值；signs 与 left 一致（M7 验过）。
```

`thumb_rota2` 加 `offset: 15` 是你诊断 2 的实现：让零位向手心偏 15°；同时 `clamp: [0, 90]` 把范围从 [0, 50] 拉宽到 [0, 90]。`output_range` 也对应 [0, 90]，让 affine 后的输出能用到这个新范围。

#### Example B — 只调拇指（保守快速试）

四指保持 M7 配置；只改拇指三关节，flag=true。其余 9 个四指关节不填 `input_range/output_range` 也合法（plan §0 in-scope #1 说"both-or-none 字段缺失 → 走 M7 路径，byte-identical"）。

```yaml
mapping:
  use_new_retarget: true
  left:
    thumb_bend:    { sources: [0,1,2], weights: [0.3,0.3,0.4], sign: -1, offset: 0,  clamp: [-10, 110],
                     input_range: [-3.5, 85.4],   output_range: [0, 110] }
    thumb_rota1:   { sources: [3],     weights: [1.0],          sign: -1, offset: 0,  clamp: [-10, 110],
                     input_range: [-22.1, 12.3],  output_range: [-10, 90] }
    thumb_rota2:   { sources: [20],    weights: [1.0],          sign:  1, offset: 15, clamp: [0, 90],
                     input_range: [-2.1, 28.4],   output_range: [0, 90] }
    index_bend:    { sources: [7],     weights: [1.0],          sign: -1, offset: 0,  clamp: [-10, 30] }
    # 四指其余 9 关节保持 M7 schema 不动
    ...
  right:
    # 同上
```

#### Example C — 纯 clamp raise（极少见，UDCAP 输入 > 110°）

```yaml
mapping:
  use_new_retarget: false             # 保持 M7 路径
  left:
    index_joint1:  { ..., clamp: [0, 130] }    # was [0, 110]
    index_joint2:  { ..., clamp: [0, 130] }
    mid_joint1:    { ..., clamp: [0, 130] }
    mid_joint2:    { ..., clamp: [0, 130] }
    ring_joint1:   { ..., clamp: [0, 130] }
    ring_joint2:   { ..., clamp: [0, 130] }
    pinky_joint1:  { ..., clamp: [0, 130] }
    pinky_joint2:  { ..., clamp: [0, 130] }
    thumb_rota2:   { ..., offset: 15, clamp: [0, 80] }    # 拇指向手心偏 + 调宽
```

### 3b. 同步 fixture（必做）

任何 config.yaml 编辑都需要 regen `mapper_baseline.json` 否则 PC2 上 snapshot test 会 SHA mismatch。

```bash
# LOCAL (macOS)
cd ~/Desktop/4-2/xhand/udex_to_xhand
python3 scripts/dump_mapper_baseline.py \
    --example example.json --config config.yaml \
    --out tests/fixtures/mapper_baseline.json
# 期望: 打印新的 config_yaml_sha256.
```

### 3c. 重新冻结 M7 reference（条件性 — 改动是否影响 M7 path 决定）

Plan 的守护脚本针对的是「flag=false 时输出 = 冻结的 M7 reference」。**如果你改的 config 字段会影响 flag=false 时的输出**（如 `clamp` / `offset` / `weights` / `sign` / `sources` 任一），就必须重新冻；只 add `input_range/output_range` 不影响（flag-gating 会 reset 掉）。

判断方法：直接跑 verify 脚本，让它告诉你需不需要重冻。

```bash
# LOCAL
bash scripts/verify_flag_false_byte_identical.sh
# 情形 A: 退码 0 + 末行 "flag=false byte-identical to M5b/M7 baseline (OK)"
#         → 不需要重冻，跳过 3d，去 3e (commit).
# 情形 B: 退码 1 + 末行 "ERROR: flag=false output diverges from M7 frozen reference ..."
#         → 你改了 M7 字段（clamp/offset/...），需要重冻：
```

### 3d. 重冻 frozen reference（只在 3c 报错时跑）

```bash
# LOCAL
# 临时切 flag=false 重 gen 一次 frozen reference (= M7 baseline as of new config)
python3 - <<'PY'
import yaml
with open("config.yaml") as f: cfg = yaml.safe_load(f)
saved = cfg["mapping"]["use_new_retarget"]
cfg["mapping"]["use_new_retarget"] = False
with open("config.yaml","w") as f: yaml.safe_dump(cfg, f, sort_keys=False, allow_unicode=True)
print("temporarily flipped use_new_retarget = False (was", saved, ")")
PY

python3 scripts/dump_mapper_baseline.py \
    --example example.json --config config.yaml \
    --out tests/fixtures/mapper_baseline_m7_frozen.json

# 切回原 flag (Example A/B = true; Example C = false 本来就没变)
python3 - <<'PY'
import yaml
with open("config.yaml") as f: cfg = yaml.safe_load(f)
cfg["mapping"]["use_new_retarget"] = True    # ← Example C 把这行删掉就行
with open("config.yaml","w") as f: yaml.safe_dump(cfg, f, sort_keys=False, allow_unicode=True)
print("flag restored to True")
PY

# 重新 regen live fixture (现在反映 flag=true 的新算法输出)
python3 scripts/dump_mapper_baseline.py \
    --example example.json --config config.yaml \
    --out tests/fixtures/mapper_baseline.json

# 再跑一次 verify 确认守护成立
bash scripts/verify_flag_false_byte_identical.sh
# 应该: 退码 0
```

### 3e. Commit + push

```bash
# LOCAL
git add config.yaml \
        tests/fixtures/mapper_baseline.json \
        tests/fixtures/mapper_baseline_m7_frozen.json \
        docs/logs/m8a-*.log
git status   # double-check 没把不相关文件加进来
git commit -m "M8b: paste M8a calibration + enable use_new_retarget; refresh fixtures"
git push
```

---

## Phase 4 — PC2 视觉验收

### 4a. 拉新 config，重跑 snapshot

```bash
# PC2
cd ~/udex_to_xhand && git pull
cd build
./tests/test_mapper_snapshot \
    --fixture ../tests/fixtures/mapper_baseline.json \
    --example ../example.json \
    --config  ../config.yaml \
    2>&1 | tee ../docs/logs/m8-pathA-snapshot-postedit-2026-05-21.log
# 期望: PASS (新 fixture SHA 匹配新 config SHA; ‖Δ‖∞ < 1e-6 rad).
# config-only 改动: 不需要 cmake/make rebuild — binary 启动时读 YAML.
```

### 4b. 左手单手视觉验收（关键步）

```bash
# 操作员戴左手 UDCAP, calib L=3.
./udex_to_xhand --config ../config.yaml --hand left --duration 60 \
    2>&1 | tee ../docs/logs/m8-pathA-fist-left-2026-05-21.log
```

操作员节拍 + 视觉验收清单：

| 时段 | 动作 | 看 XHand 该出现什么 |
|---|---|---|
| 0–5 s | 手掌完全张开 | 5 指伸直分开，拇指外展（不贴掌） |
| 5–15 s | 缓慢完整握拳 → 保持 5s | **四指指尖应贴到掌心**（M7 模式下做不到这点 — 这是 M8 修复的核心） |
| 15–25 s | 张开 → 拇指依次去碰食 / 中 / 无名 / 小指尖 | **拇指能到掌心区与每根指尖对捏**（M7 模式下拇指偏侧不能完成） |
| 25–35 s | 各种自然手势 | 跟随平滑，无明显震荡 |
| 35–60 s | 完整握拳保持 | 持续闭合，无 CRC warning |

**视觉验收判定**（写到 plan §7.2 / §7.3 execution record）：
- [ ] 完整握拳时 4 指指尖贴掌心（与 M7 相比明显改善）
- [ ] 拇指能逐一对捏其余 4 指
- [ ] 无指尖震荡 / 无明显跟手延迟
- [ ] log 末端无 `LOG_ERROR`；CRC `LOG_WARN` ≤ 5 条（ADR-017）

### 4c. 右手单手验收（同 4b）

```bash
./udex_to_xhand --config ../config.yaml --hand right --duration 60 \
    2>&1 | tee ../docs/logs/m8-pathA-fist-right-2026-05-21.log
```

### 4d. 双手同步验收

```bash
./udex_to_xhand --config ../config.yaml --hand both --duration 60 \
    2>&1 | tee ../docs/logs/m8-pathA-fist-dual-2026-05-21.log
# log 末端 latency_ms{n avg p95 max}:
#   - avg / p95 应在 M7 dual baseline ±2ms 内 (M7 baseline: avg=19.4 p95=19.2 max=111 ms)
#   - max < 50 ms 优秀; 50–120 ms 可接受（M7 已知 outlier）; > 200 ms 报告
```

---

## Phase 5 — 迭代 / 回退

### 看起来 OK

→ 把 4b/4c/4d 视觉验收结果填进 `docs/plans/20260521-m8-tuning-acceptance-plan.md` §7.2 / §7.3。
→ 进入 M8d PID tuning + M8e 30 min stress + 抓杯验收（plan §3 M8d/M8e）。

### 某关节超调 / 抖动 / 撞硬限位

```bash
# PC2 — 现场立即回 M7（最快）:
sed -i 's/use_new_retarget: true/use_new_retarget: false/' ../config.yaml
# 重启 binary (Ctrl+C 或 kill -TERM 现有进程，然后重跑 Phase 4b 命令)
# 现在 binary 走 M7 baseline 路径（即使 input_range/output_range 已填，flag=false 时全部忽略）.
```

→ 然后 LOCAL 上 narrow 该关节的 `output_range[1]` (e.g. 110 → 95) 或 `clamp[1]` (e.g. 130 → 110)
→ Phase 3b/3c/3d/3e 重跑 fixture + push
→ PC2 重新跑 Phase 4

### 拇指仍然不能对掌（Example B 失败）

Path A 用尽 → 切到 M8c thumb prototype。 plan §3 M8c Step C.1–C.6 是另一份 runbook（需要 60s recording + LOCAL prototype + 算法选型决策）。本 runbook 不覆盖。

---

## 附录 A — 一键回退到 M7

任何 phase 任何时刻：

```bash
# PC2
sed -i 's/use_new_retarget: true/use_new_retarget: false/' ../config.yaml
# 重启 binary. 完成. 不需要 git revert. 不需要 rebuild.
# 之后可以慢慢调，确认调好再切回 true.
```

**这是 M8 plan §0 总开关的核心设计意图**：硬件现场快速回退路径，永远 alive。

## 附录 B — 文件清单 (本 runbook 产出)

跑完 Phase 0–4 应该有这些 log 落盘（commit 时建议一起 git add）：

```
docs/logs/m8-pathA-cmake-2026-05-21.log
docs/logs/m8-pathA-make-2026-05-21.log
docs/logs/m8-pathA-snapshot-baseline-2026-05-21.log
docs/logs/m8-pathA-test-safety-2026-05-21.log
docs/logs/m8-pathA-receiver-smoke-2026-05-21.log
docs/logs/m8a-calibrate-left-2026-05-21.log
docs/logs/m8a-calibrate-right-2026-05-21.log
docs/logs/m8a-calibrate-fragment-2026-05-21.log
docs/logs/m8-pathA-snapshot-postedit-2026-05-21.log
docs/logs/m8-pathA-fist-left-2026-05-21.log
docs/logs/m8-pathA-fist-right-2026-05-21.log
docs/logs/m8-pathA-fist-dual-2026-05-21.log
```
