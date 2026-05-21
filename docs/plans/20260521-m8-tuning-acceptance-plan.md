# M8 Implementation Plan — Tuning + Acceptance (拇指重定向 + 四指闭合行程 + 抓杯验收)

**Plan owner**: claude (assistant)
**Plan date**: 2026-05-21
**Target milestone**: roadmap §M8（roadmap revisions #6 / #9 / #11 共同界定的最终形态 — 拇指 retargeting 重做、四指 affine rescale 修复闭合行程、PID 调参 + 30 min 压测 + 抓杯验收）
**Predecessor**: M7 ✅ (2026-05-19) — 双手集成 verified on PC2，双口架构 ADR-039 落地、`config.yaml` pinned `L=/dev/ttyACM2 R=/dev/ttyACM0`、snapshot baseline 与 M5c 字节一致。
**Executor**: PC2 operator on Unitree G1 PC2 (aarch64 Linux). 计划作者在 macOS workstation 编辑代码 + 跑 Python 工具 + commit；PC2 上 git pull → cmake → make → 跑硬件验证。每条命令前缀 `LOCAL` / `PC2` 明示运行位置。`LOCAL` 命令兼容 macOS 14.x + zsh；`PC2` 命令均自包含路径 + `tee` 落盘日志。

---

## 0. Scope

### In-scope

0. **「新 retarget 算法」总开关 `mapping.use_new_retarget`（M8 横向约束 — applies to 所有 in-scope #1 + #2）**
   - `config.yaml` 在 `mapping:` 一级下新增**必填** bool 字段 `use_new_retarget`，**默认 `false`**。
   - **`use_new_retarget: false`（M7 baseline 模式 / 出厂默认）**：mapper 完全沿用 M7 现行算法 — weighted-sum + sign + offset + clamp + deg→rad，**忽略 config 中任何 `input_range` / `output_range` / 拇指 retarget 相关字段**（哪怕它们填了，仍当作没填）。保证「config 改坏 → flag 一关 → 立刻回 M7」的快速回退路径。
   - **`use_new_retarget: true`（M8 新算法模式）**：mapper 启用 M8b 的四指 affine rescale + M8c 的拇指 retargeting。两者**共用同一开关**，不分拆 — 简化心智模型，避免 M8b / M8c 部分启用时的中间态难以归因。
   - 实现位置：`JointMapper::load_hand` 读 flag 后，若 `false` 则在 load 阶段把所有 `JointConfig::input_range / output_range` 强制置空，`apply_one` 走旧路径，**0 运行时分支**（cold-path 决定 hot-path 行为，100 Hz 控制环 latency 不变）。
   - PC2 / LOCAL 上的快速 A/B 切换工作流：改 `config.yaml` 的 `use_new_retarget` 一个值，重启 `./udex_to_xhand` 即可。snapshot test 在 §4.3 做双向验证（flag=true 走当前 fixture；flag=false 走 M5b/M7 历史 baseline）。

1. **四指 (index / mid / ring / pinky) 闭合行程修复**（roadmap revision #11 — 受 `use_new_retarget=true` 守护）
   - 新增 `--actions calibrate-udcap` 子模式：纯被动 UDP 采样工具，在 N 秒内记录每根手指对应 UDCAP 源参数（以及配置里的 weighted sum 结果）的实测 min/max，dump 一个可直接粘贴回 `config.yaml` 的 YAML 片段。Calibration 模式**不读 `use_new_retarget` flag**，永远跑（采到的数值放在 config 里不会立即生效，由 flag 控制）。
   - 扩展 `joint_mapper.{hpp,cpp}` schema：新增**可选** `input_range: [min_deg, max_deg]` + `output_range: [min_deg, max_deg]` 两个字段。**仅当 `use_new_retarget: true` 且两者同时存在**时，在 weighted-sum + sign + offset 之后插入一段 affine `(value − input_min) / (input_max − input_min) × (output_max − output_min) + output_min` rescale，再走原有 clamp + deg→rad 流水线。其它情况（flag=false 或字段缺失）→ 走 M5b/M7 旧路径，byte-identical。
   - 将 9 个四指关节（`index_bend` / `index_joint1` / `index_joint2` / `mid_joint1` / `mid_joint2` / `ring_joint1` / `ring_joint2` / `pinky_joint1` / `pinky_joint2`）填 `input_range`（calibration 实测）+ `output_range`（沿用现有 clamp 值，行程 = 物理极限）。flag 一关即回 M7。
   - 同时把 schema 改动 + flag 语义应用到 `legacy_python/joint_mapper.py`（snapshot oracle，ADR-031），Python oracle 与 C++ 双向 byte-identical（≤ 1e-17 rad）— 不论 flag = true 还是 false。
   - 重跑 M4 / M5c「握拳 + 张开 + 五指对捏」视觉验收 → 指尖必须能闭合到掌心（仅 flag=true 时；flag=false 时仍是 M7 闭合余量行为）。

2. **拇指 (thumb_bend / thumb_rota1 / thumb_rota2) retargeting 算法重做**（roadmap revision #6 — 受 `use_new_retarget=true` 守护，与四指共用同一开关）
   - Offline Python prototype (`scripts/thumb_retarget_prototype.py`)：在 dev Mac 上加载若干段录制的 UDCAP 拇指序列（已抓拳头 / 张开 / 对捏 / 中性 4 种姿态），对比三种候选算法的拇指输出轨迹（plot），决定最终算法形态。三个候选：
     - A. **Per-joint affine + zero offset**（M8b 同款 schema 扩展，最小改动；用 `offset` 编码零位偏差 + `input_range`/`output_range` 编码行程对齐）
     - B. **Coupled affine** — `thumb_rota1` / `thumb_rota2` 各自从 `l3, l20` 取**带符号** weighted sum（已被现行 schema 支持，weights 可负），等价于把 UDCAP 拇指坐标系绕 1 轴线性旋转到 XHand 拇指坐标系
     - C. **Tip-direction reprojection** — 把 UDCAP `(l2 pitch, l3 yaw, l20 roll)` 看作拇指 MCP 三轴欧拉角，构 3×3 旋转矩阵，提取拇指尖方向单位向量，再 IK 解算到 XHand `(rota1, rota2, bend)`。算法上最干净，但需要 XHand 拇指 URDF（仓库内 `URDF/` 已就绪，需 verify）+ scipy 依赖 → 仅在 A/B 都无法实现可对捏时考虑；若选 C，**`use_new_retarget=true` 开关同时控制该 dedicated pipeline 的开关**（flag=false → 走 M7 weighted-sum 路径，不进 IK）。
   - 默认提案：**A → B 走梯度上移**。先尝试 A 并在 PC2 上视觉验收（对捏 + 抓杯），不行再升 B，最后才考虑 C。决策落 ADR-049。
   - 实施 = 改 `config.yaml` 拇指三条配置（schema 与 M8b 复用，因此 `use_new_retarget=true` 同时启用四指 + 拇指）+ 必要时给 `legacy_python/joint_mapper.py` 与 `src/joint_mapper.cpp` 同步算法（A/B 都不需要改 mapper 代码，C 需要新增一条 dedicated thumb pipeline 且在该 pipeline 起点判 `use_new_retarget` flag，ADR-049 同时定 schema）。

3. **PID 调参** — 在 `config.yaml.xhand.default_kp/ki/kd` 上做扫描（最多 2~3 组），目标：消除握拳→张开的指尖震荡、消除跟手延迟。最终选定一组写回 `config.yaml`，与原默认对比若有显著变化 → 落 ADR-050。
   - **C++ 二进制不做热重载**：每轮调参重启 `./udex_to_xhand`。

4. **（可选）UDP 抖动低通滤波** — 在 `src/joint_mapper.cpp` 输出端加 EMA `y_t = α · x_t + (1-α) · y_{t-1}`，α 走 `config.yaml`（默认 1.0 = no smoothing）。仅在 PID 调参后仍观察到明显抖动时启用，落 ADR-051。

5. **30 min 压测 + ~100ms outlier 排查**（M5c / M6 / M7 遗留）
   - 跑 30 min 双手 teleop（戴手套），M5c/M7 同款 latency stats（vector + sort，ADR-033）。
   - 目标：检验 `avg / p95 / max`、dropped frames、SDK errors；尝试触发并定位 ~100ms 单点 outlier 的根因（候选：USB CDC-ACM stall / 内核 RT scheduling jitter / 双 `send_command` 串行 race）。如有可操作的根因 → 落 ADR-052。

6. **抓杯验收测试** — XHand 装在 G1 末端（ADR-023），双手抓桌上杯子 → 离桌 ≥3 s → 放下，5 次尝试 ≥3 次成功 = PASS。每次尝试录视频归档（`docs/logs/m8-acceptance-attempt-{1..5}.mp4`，PC2 本地或操作员手机均可）。

### Out-of-scope（明确推到 M9 或更后）

- ROS2 / rosbag 数据采集 — 推到 M9（ADR-043/044 限定）。
- `read_state()` 反馈 — 推到 M9（ADR-044 限定 offline observation）。
- 杯子之外的物体（笔 / 球 / 球） — SPEC §9 phase 3.11 描述的「varying sizes」属于 M8 之后的鲁棒性扩展，不进 M8 acceptance。
- 自动 USB enumeration（udev symlinks by serial） — ADR-042 标注为 post-M7 polish，不进 M8。
- 拇指 URDF 路径下的 C 方案算法实现 — 仅 A 与 B 都失败时才走，且会重新拆 M8c → M8c-extended。

### Non-goals（不要做）

- 不改 `src/safety.{hpp,cpp}` — watchdog / clamp / signal handler 全部 M6 / M7 已 verified，不动。
- 不改 `src/udcap_receiver.{hpp,cpp}` — calib==3 双手 AND-gate 是 M5b/M7 起的工作流，calibrate-udcap 模式也走同一 receiver（calibration 只在 calib==3 的 frame 上更新统计）。
- 不改双口 RS485 架构（ADR-039）。如果 PC2 USB 端口枚举漂移（ADR-042），重 probe 并更新 `config.yaml.xhand.left_serial_port / right_serial_port`，不改架构。
- 不引入新依赖（scipy / numpy 已在 dev Mac Python 中存在，不进 C++ 二进制；mapper 仍只用 yaml-cpp + nlohmann_json）。
- 不在 100Hz 控制环引入 Python / FFI（memory `feedback_no_unnecessary_ffi.md`）。
- snapshot test (`tests/test_mapper_snapshot.cpp`) 不放宽 1e-6 rad tolerance；M8b/M8c 改 schema 后必须**配套 regen baseline** 并提交（ADR-030 + ADR-037）。fixture 跟随 `config.yaml` 当前 `use_new_retarget` 状态 — flag=true 时 fixture 是新算法输出，flag=false 时 fixture 是 M7 baseline 输出；A/B 验证依靠在 §4.3 跑一次切 flag + regen + diff 的脚本化回归。
- **不**为 `use_new_retarget` 加 CLI override（如 `--use-new-retarget=false`）— 配置语义单一源（`config.yaml`），避免 CLI 与 config 不一致时 debug 困难；现场 A/B 改 config 即可（一行 sed / 编辑器都可）。

---

## 1. 文件清单

### 新增

| File | 一句话职责 |
| --- | --- |
| `src/calibrate_udcap.hpp` | 声明 `CalibStats` 结构（per-source min/max + per-joint sign×weighted+offset min/max）与 `run_calibrate_udcap(args)` 入口；header-only 接口面，实现在 main.cpp 的 actions 分支里调用。 |
| `scripts/thumb_retarget_prototype.py` | dev Mac 上跑的拇指算法 prototype：吃一段 UDCAP 拇指录制（jsonl），对 A/B/C 三个候选算法分别生成 XHand 拇指三轴输出曲线 + matplotlib plot；用于 M8c 算法选型。 |
| `scripts/record_udcap_thumb_sequences.py` | LOCAL→PC2 兼容（纯 Python + socket）UDP 监听脚本，把 60s 拇指动作录成 jsonl（每行一帧 24 个 left + 24 个 right）。供 M8c prototype 喂数据。 |
| `scripts/verify_flag_false_byte_identical.sh` | LOCAL 自动化回归脚本（M8a Step A.0 落地）：备份 `config.yaml` + 当前 fixture → 切 `mapping.use_new_retarget: false` → regen fixture → diff 验证 `left_rad / right_rad` 区块 byte-identical M5b/M7 历史 baseline → 恢复 config 与 fixture。脚本退码 0 = flag-gating 守护成立。 |
| `tests/fixtures/mapper_baseline.json` | 不算新增 — 但 M8b 与 M8c 各 commit 一次 regen 后的版本（SHA-256 + `generated_at` 必变）。M8 ✅ 时反映 flag=true 新算法输出。 |
| `tests/fixtures/mapper_baseline_m7_frozen.json` | **新增 + 永不更新**。M8a Step A.0 末尾把当时 flag=false / 字段全缺的 baseline 复制一份过来 — 这是「M5b/M7 byte-identical」的冻结 reference，供 `verify_flag_false_byte_identical.sh` 在 M8b/M8c 之后仍能验证「flag=false → 输出 = M5b/M7」。M8 ✅ 后保留，作 M9 / 后续 milestone 的 flag-gating 长期 guard。 |
| `docs/decisions/048-four-finger-affine-rescale-and-calibration.md` | M8b 决策：affine rescale schema 设计 + 向后兼容策略 + calibration 数据流。 |
| `docs/decisions/049-thumb-retargeting-algorithm-choice.md` | M8c 决策：A/B/C 三选一的依据 + 最终 schema + 视觉验收结果。 |
| `docs/decisions/050-pid-tuning-conclusion.md` | **条件性** — 若 default_kp/ki/kd 与 M5a 厂商默认偏离 → 必填；如调参无显著差异 → 跳过。 |
| `docs/decisions/051-udp-low-pass-filter.md` | **条件性** — 仅在启用 EMA 滤波时填写。 |
| `docs/decisions/052-100ms-latency-outlier-rootcause.md` | **条件性** — 仅在 30 min 压测定位到根因时填写；定位不到则在 plan §8.5 known-issue 记录并推到 M9 / 永久 backlog。 |
| `docs/logs/m8a-{cmake,make,calibrate-left,calibrate-right,calibrate-fragment}-2026-05-21.log` | M8a 验收日志，`tee` 落盘。 |
| `docs/logs/m8b-{cmake,make,snapshot,test-safety,fist-left,fist-right,fist-dual}-2026-05-21.log` | M8b 验收日志。 |
| `docs/logs/m8c-{prototype,cmake,make,snapshot,oppose-left,oppose-right,oppose-dual}-2026-05-21.log` | M8c 验收日志。 |
| `docs/logs/m8d-pid-{tune-run-1,tune-run-2,final}-2026-05-21.log` | M8d 调参日志，每轮 kp/kd 一份。 |
| `docs/logs/m8e-{stress-30min,acceptance-attempts}-2026-05-21.log` | M8e 压测 + 5 次抓杯尝试日志。 |
| `docs/logs/m8-acceptance-attempt-{1..5}.mp4` | 5 次抓杯尝试视频。 |

### 修改

| File | 一句话职责（delta only） |
| --- | --- |
| `src/joint_mapper.hpp` | `struct JointConfig` 增加可选字段 `std::optional<std::pair<double,double>> input_range` + `output_range`；公有接口 `map_left / map_right` 不变。 |
| `src/joint_mapper.cpp` | `load_hand`：读 `input_range`/`output_range`（缺失时为空）。`apply_one`：weighted-sum + sign + offset 之后，若 `input_range && output_range` 均存在 → 应用 affine rescale，再走 clamp + deg→rad。两者缺失 → byte-identical 旧路径（ADR-030 snapshot 不变）。 |
| `src/cli.hpp` / `src/cli.cpp` | `--actions calibrate-udcap` 新增为合法 action 名（已有 actions 框架仅认 fist/palm/v/ok；这里走同一 `--actions` 入口但单一 token `calibrate-udcap`，与其它互斥）。新增 `--calibrate-duration <sec>` flag（默认 30），仅与 `calibrate-udcap` 配对。 |
| `src/main.cpp` | `run_actions` 分支：若 `args.actions == "calibrate-udcap"` → 调 `run_calibrate_udcap(args)`，否则走现有 preset 路径。新增 `run_calibrate_udcap` 函数定义（短，~80 行）：构造 `UdcapReceiver` + `JointMapper` → 进 N 秒采样循环 → 统计 → stdout dump YAML 片段。 |
| `legacy_python/joint_mapper.py` | 同步加 `input_range` / `output_range` 字段解析与 affine rescale 逻辑（snapshot oracle，ADR-031 保留为 live oracle 的依据）。两者缺失时 byte-identical 旧行为。 |
| `config.yaml` | **M8a 第一步**：`mapping:` 下加新必填 bool `use_new_retarget: false`（出厂默认走 M7）。M8b：9 个四指关节加 `input_range` + `output_range`，左右手各填，**`use_new_retarget` 切 true** 才生效。M8c：3 个拇指关节按选定的算法（A/B/C）补全字段，同 flag 守护。M8d：`xhand.default_kp / ki / kd` 若调整则更新（PID 与 retarget 算法独立，不受 flag 影响）。M8（可选）：增加 `xhand.smoothing_alpha`（仅 ADR-051 触发时）。每次 `config.yaml` 变动同 commit 重 gen `tests/fixtures/mapper_baseline.json`（ADR-037）。 |
| `docs/plans/00-roadmap.md` | 完工时加一条 revision 记录 + §M8 标 ✅。 |
| `SPEC.md` | 完工时：§4 mapping algorithm 描述补 affine rescale 公式 + 拇指 retargeting 算法说明；§9 Phase 3.10 30-min stress test 结果回填；§10 Risk #5 / #6（拇指 sign / PID 调参）状态更新；§11 config schema 补 `input_range` / `output_range` / `smoothing_alpha`；§12 open Q #5 (PID) / #6 (smoothing) 收敛。 |
| `CLAUDE.md` | 若 M8 引入 EMA 滤波 → 在 §Code conventions 加一句"低通在 mapper 输出后、send_command 前；α 走 config.yaml"。 |

### 不动（防止 scope creep）

- `src/safety.{hpp,cpp}` — M6 / M7 ADR-035/036/038 全部 verified，不动。
- `src/udcap_receiver.{hpp,cpp}` — calib==3 AND-gate 是 M5b/M7 起的现行工作流。
- `src/xhand_driver.{hpp,cpp}` — 双口架构 + `has_both()` + `require_both` 已 verified（M7 §4.2）。
- `tests/test_safety.cpp` / `tests/test_mapper_snapshot.cpp` — 源码不动；后者 fixture regen 但 test 断言不变。
- `xhand_control_sdk/` — 厂商源 pristine（ADR-025），不动。
- `legacy_python/{main,udcap_receiver,xhand_driver,safety,test_udcap_connection}.py` — 自 M5b 起 reference-only，零 runtime caller，不动。

---

## 2. 数据流

### 2.1 M8b / M8c 之后：稳态 tick（双手 affine rescale 已上线）

```
UDCAP UDP packet (60-120 Hz, both gloves worn, calib L=R=3)
  → UdcapReceiver::try_recv()           [不变]
  → UdcapFrame { l[24], r[24], calib_left=3, calib_right=3, recv_ts }
  → main loop @ 100 Hz
      → mapper.map_left(frame.l):
            for each of 12 joints:
              acc       = Σ weights[i] * l[sources[i]]               # 旧路径
              shifted   = sign * acc + offset                        # 旧路径
              if jc.input_range && jc.output_range:                  # ★ M8 新增分支
                  # 只有 use_new_retarget=true 时 input_range/output_range
                  # 才会被 load 进 JointConfig；flag=false 时 load 阶段已
                  # 强制清空 → 这条 if 在 flag=false 时永远不进
                  rescaled = (shifted - in_min) / (in_max - in_min)
                             * (out_max - out_min) + out_min
              else:
                  rescaled = shifted                                 # M7 路径（flag=false 或字段未填）
              clamped = clamp(rescaled, clamp_min, clamp_max)        # 旧 ADR-020
              rad     = clamped * π / 180                            # 旧 ADR-021
            → 12 rad
      → mapper.map_right(frame.r): same
      → driver.send_left / send_right                                [不变]
      → cache last_left_rad / last_right_rad (M6 stale-resend)       [不变]
      → latency_stats.add(now - recv_ts)                             [不变]
```

向后兼容关键点：`mapping.use_new_retarget = false` **或** `input_range / output_range` 缺失时，C++ 路径与 M7 / M5c 完全 byte-identical（snapshot test 仍是 ‖Δ‖∞ ≤ 1e-17 rad）。所以 M8b 在 push 全套 schema 之前可以单 joint 逐个加，每加一个 commit + regen fixture。**`use_new_retarget` 是粗粒度总闸**（一开一关，影响所有 12 关节 × 2 手）；`input_range / output_range` 的存在与否是细粒度（per-joint，决定单个关节是否走新路径）。运行 / 调试 / 现场回退时优先操作粗粒度闸，细粒度只在 M8b/M8c 增量开发期手动 per-joint 添加。

### 2.2 M8a：calibrate-udcap 子模式数据流

```
LOCAL: 操作员戴左手 UDCAP，启动 UDCAP HandDriver 发包到 PC2:9000
PC2: ./udex_to_xhand --actions calibrate-udcap --calibrate-duration 30 \
       --config ../config.yaml --hand left

main.cpp::run_actions()
  → if args.actions == "calibrate-udcap" → run_calibrate_udcap(args)
      → 构 UdcapReceiver(udcap.host, udcap.port)                # 复用现有 receiver
      → 构 JointMapper(args.config)                            # 复用现有 mapper
      → CalibStats stats{}                                     # 24 src min/max + 12 joint min/max（per hand）
      → t_end = now + 30s
      → while now < t_end and !shutdown_flag:
            frame = receiver.try_recv()                        # 同样的 calib==3 gate
            if frame: 
                stats.update_left(frame.l, mapper.left_)       # 更新 24 src + 12 joint 的 min/max
                if args.hand == Both: stats.update_right(frame.r, mapper.right_)
            sleep_until(next 10ms tick)
      → 操作员（在屏幕提示下）依次做：
            - 张开手掌（5 s）
            - 自然中性位（5 s）
            - 单根手指依次最大屈曲（每根 3 s × 5 根 = 15 s）
            - 完整握拳（5 s）
        共 ≈ 30 s 即可覆盖每个 source / joint 的工作行程
      → 打印 YAML 片段到 stdout:
            # ===== left =====
            #   index_bend:    input_range: [-2.1,  43.8]
            #   index_joint1:  input_range: [-1.5, 105.4]
            #   ... 9 行
            # 同时打印 24 source 原始 min/max 作 sanity check
      → 退出，driver 不开（calibrate 模式不连 XHand）
PC2: 操作员把 YAML 片段 copy 进 config.yaml（先 commit 当前 config 作 baseline）
LOCAL: git pull → 检查 diff → 重 gen fixture → commit
```

### 2.3 M8e：30 min 压测 + 抓杯验收

```
PC2 终端 A: ./udex_to_xhand --config ../config.yaml --hand both --duration 1800 \
              2>&1 | tee docs/logs/m8e-stress-30min-2026-05-21.log &
              # 1800s = 30 min
              # 输出末端 latency_ms{n avg p95 max} + frames_dropped + sdk_errors
PC2 终端 B: 操作员戴双手 UDCAP，正常 teleop 30 min（手不必一直动；空闲也可）
            预算 inline 抓杯尝试 ≥1 次 in stress
PC2 进程结束后:
  → 操作员目视 hand 物理状态（mode=0 静止）
  → latency stats + frames_dropped 写回 plan §7.5
  → 若 max > 50ms / p95 > 30ms / 任何 SDK error → STOP, 退回 §8.5 known issues
30 min 通过后 → 抓杯验收：
PC2 终端 A: ./udex_to_xhand --config ../config.yaml --hand both \
              2>&1 | tee docs/logs/m8e-acceptance-attempts-2026-05-21.log &
PC2 终端 B: 操作员（戴手套，XHand 装 G1 末端 ADR-023）
            attempt #1: 双手包夹杯子 → 离桌 → 保持 3s → 放回 → 松手
            attempt #2..#5: 重复
            每次 attempt 录视频 m8-acceptance-attempt-N.mp4
PC2: kill -TERM $!     # 触发 graceful shutdown (mode=0 + close)
PC2: 统计 5 次 attempt 中成功 ≥ 3 → PASS
```

---

## 3. 实现拆分

### M8a — Calibration 工具 + 引入 `use_new_retarget` 总开关（est. 0.4d）

**目标**：(1) 在 mapper schema 顶层引入 `use_new_retarget: false` 总开关（默认走 M7），并验证 flag=false 时 byte-identical M7；(2) 拿到 9 个四指关节的 `input_range` 实测值，写回 `config.yaml`。拇指 3 个关节也顺便采（M8c 用）。

#### Steps

- **Step A.0 — LOCAL（最先做，建立守护）**：
  - 编辑 `src/joint_mapper.hpp`：在 `JointMapper` 类里加 `bool use_new_retarget_{false};` 私有成员 + `bool use_new_retarget() const { return use_new_retarget_; }` getter（仅 test 用）。
  - 编辑 `src/joint_mapper.cpp::JointMapper(const std::string& yaml_path)`：在 `load_hand` 之前先读 root `mapping.use_new_retarget`：
    ```cpp
    YAML::Node root = YAML::LoadFile(yaml_path);
    auto mapping = root["mapping"];
    if (!mapping || !mapping.IsMap())
        throw std::runtime_error(yaml_path + ": missing top-level 'mapping' map");
    if (!mapping["use_new_retarget"])
        throw std::runtime_error(yaml_path + ": mapping.use_new_retarget required (set to false to keep M7 baseline)");
    use_new_retarget_ = mapping["use_new_retarget"].as<bool>();
    ```
  - **注意**：本步骤 `JointConfig` 暂不加 `input_range / output_range` 字段（留到 Step B.1）。本步只引入 flag 的 plumbing。
  - 编辑 `legacy_python/joint_mapper.py`：同样加 `mapping.use_new_retarget` 必填校验 + 存到 `self.use_new_retarget`。`apply` 路径不变（M8a 阶段 flag 没有副作用，仅 schema 校验）。
  - 编辑 `config.yaml`：`mapping:` 下顶端一行 `use_new_retarget: false   # M8 retarget master switch; true → 启用 affine rescale + thumb retargeting`，左右手 mapping 配置不变。
  - **`tests/test_mapper_snapshot.cpp` 不动**，但 fixture 必须 regen（schema 变了 → SHA 变了 → ADR-030 自校验会触发 mismatch）：
    ```bash
    # LOCAL (macOS)
    python3 scripts/dump_mapper_baseline.py \
        --example example.json --config config.yaml \
        --out tests/fixtures/mapper_baseline.json
    ```
  - 跑 snapshot 验证 byte-identical（flag=false 时算法 0 改动）：
    ```bash
    # LOCAL (若 brew install cmake yaml-cpp nlohmann-json 已装)
    mkdir -p build && cd build && cmake .. && make test_mapper_snapshot
    ./tests/test_mapper_snapshot
    # 预期: L max |Δ| = 0.0e+00 rad / R max |Δ| = 1.4e-17 rad (与 M5b/M7 一致)
    # 若本地 build 失败 → 跳过 LOCAL 验证, Step A.5 PC2 build 时一并跑
    ```
  - 冻结 M7 reference fixture（在 schema 已加 flag 但 flag=false 时，输出 = M5b/M7 byte-identical）：
    ```bash
    # LOCAL
    cp tests/fixtures/mapper_baseline.json tests/fixtures/mapper_baseline_m7_frozen.json
    # 该文件永不再被脚本回写；M8 期间所有「flag=false byte-identical」断言都以它为 reference
    ```
  - commit：
    ```bash
    git add src/joint_mapper.{hpp,cpp} legacy_python/joint_mapper.py config.yaml \
            tests/fixtures/mapper_baseline.json tests/fixtures/mapper_baseline_m7_frozen.json
    git commit -m "M8a Step A.0: introduce mapping.use_new_retarget flag (default false = M7 baseline) + freeze M7 reference fixture"
    ```

- **Step A.0' — LOCAL（落地自动化守护脚本）**：写 `scripts/verify_flag_false_byte_identical.sh`：
  ```bash
  #!/usr/bin/env bash
  # Usage: bash scripts/verify_flag_false_byte_identical.sh
  # Verifies: with mapping.use_new_retarget=false in config.yaml, the regen-ed
  # baseline fixture's left_rad / right_rad arrays are byte-identical to the
  # FROZEN M7 reference (tests/fixtures/mapper_baseline_m7_frozen.json, committed
  # at M8a Step A.0 end and never rewritten).
  # Used as a flag-gating regression guard across all of M8 (M8b/M8c/...).
  set -euo pipefail
  REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
  cd "$REPO_ROOT"
  REFERENCE="tests/fixtures/mapper_baseline_m7_frozen.json"
  if [ ! -f "$REFERENCE" ]; then
      echo "ERROR: $REFERENCE missing — was A.0 freeze step performed?" >&2
      exit 2
  fi
  cp config.yaml /tmp/config.yaml.m8-verify-backup
  cp tests/fixtures/mapper_baseline.json /tmp/mapper_baseline.m8-verify-backup
  # 强制 flag=false（不管它当前是什么）
  python3 -c "
  import yaml
  with open('config.yaml') as f: cfg = yaml.safe_load(f)
  cfg['mapping']['use_new_retarget'] = False
  with open('config.yaml', 'w') as f: yaml.safe_dump(cfg, f, sort_keys=False, allow_unicode=True)
  "
  python3 scripts/dump_mapper_baseline.py \
      --example example.json --config config.yaml \
      --out /tmp/mapper_baseline.flag-false-regen.json
  # 比对 left_rad / right_rad（忽略 generated_at / sha 字段，那些必然不同）
  set +e
  python3 -c "
  import json, sys
  ref = json.load(open('$REFERENCE'))
  reg = json.load(open('/tmp/mapper_baseline.flag-false-regen.json'))
  ok = (ref['left_rad'] == reg['left_rad']) and (ref['right_rad'] == reg['right_rad'])
  sys.exit(0 if ok else 1)
  "
  RC=$?
  set -e
  # 恢复 config + fixture（脚本不留任何 side effect）
  cp /tmp/config.yaml.m8-verify-backup config.yaml
  cp /tmp/mapper_baseline.m8-verify-backup tests/fixtures/mapper_baseline.json
  if [ $RC -eq 0 ]; then
      echo "flag=false byte-identical to M5b/M7 baseline (OK)"
  else
      echo "ERROR: flag=false output diverges from M7 frozen reference — flag-gating broken" >&2
  fi
  exit $RC
  ```
  跑一次：
  ```bash
  # LOCAL
  bash scripts/verify_flag_false_byte_identical.sh
  # 预期: 末行 "flag=false byte-identical to M5b/M7 baseline (OK)"，退码 0
  ```
  commit：
  ```bash
  git add scripts/verify_flag_false_byte_identical.sh
  git commit -m "M8a Step A.0': scripts/verify_flag_false_byte_identical.sh — flag-gating regression guard"
  ```

- **Step A.1 — LOCAL**：在 macOS 上编辑 `src/cli.hpp` / `src/cli.cpp`：
  - `cli::Args` 中新增 `double calibrate_duration{30.0};`
  - 在 `--actions` 的值校验里允许 `calibrate-udcap` token（除 fist/palm/v/ok 外）
  - 新增 `--calibrate-duration <sec>` flag 解析；与 `--actions calibrate-udcap` 互斥校验：`--hold` / `--duration` / `--mock` / `--receiver-only` 与 calibrate-udcap 互斥
  - help 字符串补一行 `--actions calibrate-udcap        UDCAP range capture mode (M8a); use --calibrate-duration`
- **Step A.2 — LOCAL**：新建 `src/calibrate_udcap.hpp`，声明 `struct CalibStats` + `int run_calibrate_udcap(const cli::Args&)`. 实现放在 `src/main.cpp`（避免新增 cpp 文件改 CMakeLists.txt）。
- **Step A.3 — LOCAL**：在 `src/main.cpp` 增加 `run_calibrate_udcap` 函数：
  - 构造 `UdcapConfig` + `UdcapReceiver`（不开 XHand）
  - 构造 `JointMapper(args.config)`
  - 维护 `std::array<MinMax, 24>` 左右各一份（24 source）+ `std::array<MinMax, 12>` 左右各一份（12 joint 的 weighted+sign+offset 值）
  - 主循环 100 Hz：`try_recv()` → 更新 stats（无需做最终 clamp / rescale）
  - 屏幕每 5 s 打印一行进度（`[calib] elapsed=5s frames=487 left l0..l23 ranges so far ...`）
  - 结束时 stdout 输出可粘贴 YAML 片段（带 `# auto-generated by --actions calibrate-udcap on 2026-05-21` header）
- **Step A.4 — LOCAL**：commit
  ```bash
  # LOCAL (macOS)
  cd ~/Desktop/4-2/xhand/udex_to_xhand
  git add src/cli.hpp src/cli.cpp src/calibrate_udcap.hpp src/main.cpp
  git commit -m "M8a: add --actions calibrate-udcap mode for UDCAP range capture"
  git push
  ```
- **Step A.5 — PC2**：build
  ```bash
  # PC2 (aarch64 Linux, ssh into G1)
  cd ~/udex_to_xhand
  git pull
  cd build
  cmake .. 2>&1 | tee ../docs/logs/m8a-cmake-2026-05-21.log
  make -j$(nproc) 2>&1 | tee ../docs/logs/m8a-make-2026-05-21.log
  ```
  Expected: 0 warning（-Wall -Wextra -Wpedantic 沿用 M5b）, `udex_to_xhand` ELF rebuilt.
- **Step A.6 — PC2**：左手 calibration（戴左手 UDCAP，按屏幕脚本做动作）
  ```bash
  # PC2 — Windows 端先启动 UDCAP HandDriver → 192.168.3.x:9000
  cd ~/udex_to_xhand/build
  ./udex_to_xhand --actions calibrate-udcap --calibrate-duration 30 \
                  --config ../config.yaml --hand left \
                  2>&1 | tee ../docs/logs/m8a-calibrate-left-2026-05-21.log
  ```
  Expected: 末端 stdout 一段 YAML 片段，9 行四指 `input_range` + 3 行拇指 `input_range`；每个 range min < max，且 max-min ≥ 30°（小于说明操作员动作不够大，需重做）。
- **Step A.7 — PC2**：右手 calibration（戴右手 UDCAP，同样的动作）
  ```bash
  # PC2
  ./udex_to_xhand --actions calibrate-udcap --calibrate-duration 30 \
                  --config ../config.yaml --hand right \
                  2>&1 | tee ../docs/logs/m8a-calibrate-right-2026-05-21.log
  ```
- **Step A.8 — PC2**：把两段 YAML 合并保存为 `docs/logs/m8a-calibrate-fragment-2026-05-21.log` 作 audit trail。
- **Step A.9 — LOCAL**：`git pull` 收 log；M8a ✅，进 M8b。

#### Validation
- `--actions calibrate-udcap` 不开 XHand（log 中无 `OPEN_DEVICE`），仅 UDP；
- log 包含 `[calib] complete: frames=N (N > 1000 in 30s @ 100Hz);`
- YAML 片段语法合法 → 直接粘进 `config.yaml` 后 `yaml-cpp` 可解析（M8b 第一次 build 验证）。

---

### M8b — 四指 affine rescale 落地（est. 0.4d）

#### Steps

- **Step B.1 — LOCAL**：扩展 `src/joint_mapper.hpp`：
  ```cpp
  #include <optional>
  #include <utility>

  struct JointConfig {
      std::vector<int> sources;
      std::vector<double> weights;
      int sign{1};
      double offset{0.0};
      double clamp_min{0.0};
      double clamp_max{0.0};
      std::optional<std::pair<double,double>> input_range;   // M8b: empirical UDCAP range
      std::optional<std::pair<double,double>> output_range;  // M8b: target XHand range
  };
  ```
- **Step B.2 — LOCAL**：扩展 `src/joint_mapper.cpp::load_hand`：
  ```cpp
  // 注意：A.0 已把 use_new_retarget_ 读到 JointMapper 私有成员。
  // 把它作为 load_hand 的额外入参（或通过 member 函数访问），驱动以下逻辑：
  //   - flag=true: 解析 input_range/output_range，存入 jc，apply_one 走 affine 路径
  //   - flag=false: 仍解析 YAML（容忍字段存在），但 load 结束前强制 reset 两个 optional
  //                 → apply_one byte-identical M7 旧路径
  if (node["input_range"]) {
      auto r = node["input_range"].as<std::vector<double>>();
      if (r.size() != 2)
          throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                    ".input_range must have exactly 2 elements");
      jc.input_range = std::make_pair(r[0], r[1]);
  }
  if (node["output_range"]) {
      auto r = node["output_range"].as<std::vector<double>>();
      if (r.size() != 2)
          throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                    ".output_range must have exactly 2 elements");
      jc.output_range = std::make_pair(r[0], r[1]);
  }
  // 共生校验：input_range 与 output_range 必须 both-or-none（独立于 flag，永远校验 schema）
  if (jc.input_range.has_value() != jc.output_range.has_value())
      throw std::runtime_error(yaml_path + ": " + hand_key + "." + name +
                                ": input_range and output_range must be both present or both absent");
  // M8 flag-gating: flag=false 时强制清空 → apply_one byte-identical M7
  if (!use_new_retarget) {
      jc.input_range.reset();
      jc.output_range.reset();
  }
  ```
  对应改 `load_hand` 签名：增加 `bool use_new_retarget` 入参；构造函数里 `left_ = load_hand(yaml_path, "left", use_new_retarget_); right_ = load_hand(yaml_path, "right", use_new_retarget_);`。
- **Step B.3 — LOCAL**：扩展 `apply_one`：
  ```cpp
  double JointMapper::apply_one(const JointConfig& jc, const std::array<double,24>& src) {
      double acc = 0.0;
      for (size_t i = 0; i < jc.sources.size(); ++i)
          acc += jc.weights[i] * src[jc.sources[i]];
      double deg = static_cast<double>(jc.sign) * acc + jc.offset;
      if (jc.input_range && jc.output_range) {
          const auto [in_min, in_max] = *jc.input_range;
          const auto [out_min, out_max] = *jc.output_range;
          const double span = in_max - in_min;
          if (span > 1e-9) {
              const double ratio = (deg - in_min) / span;
              deg = ratio * (out_max - out_min) + out_min;
          } else {
              deg = (out_min + out_max) * 0.5;  // 退化：input range = 0 → 取输出中点
          }
      }
      deg = std::max(jc.clamp_min, std::min(jc.clamp_max, deg));
      return deg * kDeg2Rad;
  }
  ```
- **Step B.4 — LOCAL**：同步 `legacy_python/joint_mapper.py`：
  - 解析 `input_range` / `output_range`（同 both-or-none 校验）
  - 沿用 A.0 已加的 `self.use_new_retarget`；在 load 阶段若 `False` → 强制把字段置 None（与 C++ `reset()` 等价）
  - `map()` 内插入与 C++ 等价的 affine rescale（同样的浮点顺序：`(deg - in_min) / span * (out_max - out_min) + out_min`）；保证两侧 flag-gating 行为 1:1 对应
- **Step B.5 — LOCAL**：双向 smoke test — 先**不改 config.yaml**（两个新字段都缺、`use_new_retarget` 仍 false），build + 跑 snapshot test，确认 byte-identical（向后兼容验证）。
  ```bash
  # LOCAL — 如果本地装了 cmake + g++ + yaml-cpp + nlohmann_json 可跑；否则推到 PC2
  mkdir -p build && cd build && cmake .. && make test_mapper_snapshot test_safety
  ./tests/test_mapper_snapshot
  ```
  Expected: `L max |Δ| = 0.0e+00 rad / R max |Δ| = 1.4e-17 rad`，与 M5b/M7 一致。
- **Step B.6 — LOCAL**：把 M8a 采到的 YAML 片段粘进 `config.yaml` 9 个四指关节（左右各 9），`output_range` 沿用现有 `clamp` 值。**此刻 `use_new_retarget` 仍是 false**（守护未开），所以 fixture 行为应仍 byte-identical M7（验证 flag-gating 工作）。
- **Step B.7 — LOCAL**：第一次 regen fixture 验证 flag=false guard（关键回归 — 即使 input/output_range 都填了，flag=false → 输出仍 = M7）：
  ```bash
  # LOCAL
  python3 scripts/dump_mapper_baseline.py \
      --example example.json --config config.yaml \
      --out tests/fixtures/mapper_baseline.json
  # diff 与 A.0 末尾的 baseline:
  git diff tests/fixtures/mapper_baseline.json
  # 预期: 仅 config_yaml_sha256 + generated_at 变动；left_rad / right_rad 数组 byte-identical
  ```
  若 left_rad / right_rad 有任何 numeric 变动 → **flag-gating 实现有 bug**，回退到 Step B.2 重审 `if (!use_new_retarget) { reset() }` 的位置。
- **Step B.7' — LOCAL**：切 flag = true，再 regen 一次，diff 看新算法生效：
  ```bash
  # LOCAL
  # 编辑 config.yaml: mapping.use_new_retarget: true
  python3 scripts/dump_mapper_baseline.py \
      --example example.json --config config.yaml \
      --out tests/fixtures/mapper_baseline.json
  git diff tests/fixtures/mapper_baseline.json
  # 预期: 9 个四指关节的 left_rad / right_rad 显著变动（不再是 1:1 度数直传，而是 affine rescale 结果）；
  #       3 个拇指关节仍 byte-identical (拇指还没改 input/output_range，留到 M8c)
  ```
- **Step B.8 — LOCAL**：commit（fixture 跟随 flag=true 状态，便于 PC2 build 后 snapshot 反映新算法）
  ```bash
  # LOCAL
  git add src/joint_mapper.{hpp,cpp} legacy_python/joint_mapper.py config.yaml tests/fixtures/mapper_baseline.json
  git commit -m "M8b: four-finger affine rescale with M8a calibration; flip use_new_retarget=true; regen baseline"
  git push
  ```
- **Step B.9 — PC2**：build + snapshot + safety test
  ```bash
  # PC2
  cd ~/udex_to_xhand && git pull
  cd build && cmake .. 2>&1 | tee ../docs/logs/m8b-cmake-2026-05-21.log
  make -j$(nproc) 2>&1 | tee ../docs/logs/m8b-make-2026-05-21.log
  ./tests/test_mapper_snapshot 2>&1 | tee ../docs/logs/m8b-snapshot-2026-05-21.log
  ./tests/test_safety           2>&1 | tee ../docs/logs/m8b-test-safety-2026-05-21.log
  ```
  Expected: snapshot `max |Δ| < 1e-6 rad`；safety test 全过；0 warning。
- **Step B.10 — PC2**：握拳视觉验收（左手单手 → 右手单手 → 双手）
  ```bash
  # PC2 — 左手
  ./udex_to_xhand --config ../config.yaml --hand left --duration 30 \
      2>&1 | tee ../docs/logs/m8b-fist-left-2026-05-21.log
  # 戴左手 UDCAP 完整握拳 5 s
  # 视觉验收: index/mid/ring/pinky 指尖能贴近掌心，与 M5c 视觉对比明显闭合更紧
  ```
  Right + Both 同样：
  ```bash
  # PC2 — 右手
  ./udex_to_xhand --config ../config.yaml --hand right --duration 30 \
      2>&1 | tee ../docs/logs/m8b-fist-right-2026-05-21.log
  # PC2 — 双手
  ./udex_to_xhand --config ../config.yaml --hand both --duration 60 \
      2>&1 | tee ../docs/logs/m8b-fist-dual-2026-05-21.log
  ```
  视觉验收（必须三个 session 都通过）：
  - 5 指张开 → XHand 5 指张开（与 M5c 一致即可）
  - 完整握拳 → 4 指指尖贴掌心（M5c 是不能贴的 — 这就是 M8b 修复点）
  - 食指 + 拇指对捏（拇指还没改，捏不准也无所谓 — 留到 M8c）
- **Step B.11 — LOCAL**：填 ADR-048 草稿；写 plan §7.2 execution record；M8b ✅。

#### Validation
- snapshot test 全 12 关节 ‖Δ‖∞ < 1e-6 rad（Python ↔ C++ 一致）
- 视觉验收四指能闭合到掌心
- 单手 latency `n avg p95 max` 不显著恶化（< 5% 增长，相对 M5c baseline `9.6/9.6/10.7 ms`）

---

### M8c — 拇指 retargeting（est. 0.5d；含 prototype 调研）

#### Steps

- **Step C.1 — LOCAL**：写 `scripts/record_udcap_thumb_sequences.py`（macOS 上跑，监听 UDP → jsonl）
  ```python
  # 简述：socket bind 0.0.0.0:9000，每帧解析 JSON 提取 24 left + 24 right
  # → jsonl 写到 stdout 或 --out 文件
  # 用法： --duration 60 --out thumb_<gesture>.jsonl
  ```
- **Step C.2 — PC2 (操作员 + UDCAP)**：录 4 段拇指动作（每段 15 s，可在同一终端连续录到一个 jsonl）。Windows UDCAP 发到 PC2，PC2 上跑 record 脚本（脚本 macOS 与 Linux 都跑）：
  ```bash
  # PC2
  python3 ~/udex_to_xhand/scripts/record_udcap_thumb_sequences.py \
          --duration 60 --out ~/udex_to_xhand/docs/logs/m8c-thumb-recording-2026-05-21.jsonl
  # 操作员 0-15s 张开手掌；15-30s 拇指对食指捏；30-45s 拇指绕掌心做对掌；45-60s 拳头
  ```
  把 jsonl scp / git push 回 LOCAL：
  ```bash
  # PC2: 直接 git add（jsonl 体积小，< 5 MB）
  cd ~/udex_to_xhand && git add docs/logs/m8c-thumb-recording-2026-05-21.jsonl
  git commit -m "M8c: capture UDCAP thumb gesture sequences for prototype"
  git push
  ```
- **Step C.3 — LOCAL**：写 `scripts/thumb_retarget_prototype.py`（已在 §1 列出职责）。三个算法实现 + matplotlib 4 plot（thumb_bend / thumb_rota1 / thumb_rota2 / opposition score）。无需依赖 XHand SDK / C++。
  ```bash
  # LOCAL
  git pull
  python3 scripts/thumb_retarget_prototype.py \
          --recording docs/logs/m8c-thumb-recording-2026-05-21.jsonl \
          --algo A --plot scripts/thumb-algo-A.png \
          2>&1 | tee docs/logs/m8c-prototype-A-2026-05-21.log
  python3 scripts/thumb_retarget_prototype.py --algo B --plot scripts/thumb-algo-B.png ...
  python3 scripts/thumb_retarget_prototype.py --algo C --plot scripts/thumb-algo-C.png ...
  ```
- **Step C.4 — LOCAL（决策点）**：人工目检 3 张 plot：
  - 算法 A：能否在「对捏」段产生 `thumb_rota1 + thumb_rota2` 联动？若是 → 选 A，跳到 Step C.5。
  - 算法 B（用 negative weights 编码坐标系旋转）：若 A 单独失败但 B 在对掌期能产生联动 → 选 B。
  - 算法 C：A/B 都失败再上。需另外 1.5d 工期 + URDF 验证 + scipy。STOP & 重新拆 M8c-extended。
- **Step C.5 — LOCAL**：把选定的算法参数写入 `config.yaml` 拇指三条（在 `use_new_retarget=true` 守护下生效；flag=false 时仍走 M7 拇指 weighted sum）；regen fixture + 跑 flag 守护回归：
  ```bash
  # LOCAL
  python3 scripts/dump_mapper_baseline.py --example example.json --config config.yaml --out tests/fixtures/mapper_baseline.json
  bash scripts/verify_flag_false_byte_identical.sh     # 必跑：flag=false 仍 = M7（拇指与四指都不走新算法）
  git add config.yaml tests/fixtures/mapper_baseline.json scripts/thumb_retarget_prototype.py scripts/record_udcap_thumb_sequences.py scripts/thumb-algo-*.png
  git commit -m "M8c: thumb retargeting algorithm $(<选定 A/B/C>); regen snapshot baseline (flag-gating verified)"
  git push
  ```
- **Step C.6 — PC2**：build + snapshot + 视觉验收
  ```bash
  # PC2
  cd ~/udex_to_xhand && git pull && cd build
  cmake .. 2>&1 | tee ../docs/logs/m8c-cmake-2026-05-21.log
  make -j$(nproc) 2>&1 | tee ../docs/logs/m8c-make-2026-05-21.log
  ./tests/test_mapper_snapshot 2>&1 | tee ../docs/logs/m8c-snapshot-2026-05-21.log

  # 左手对捏验收
  ./udex_to_xhand --config ../config.yaml --hand left --duration 60 \
      2>&1 | tee ../docs/logs/m8c-oppose-left-2026-05-21.log
  # 视觉验收：拇指 ↔ 食指对捏（拇指尖触食指尖）→ 拇指 ↔ 中指、无名指、小指对捏

  # 右手 + 双手同上
  ./udex_to_xhand --config ../config.yaml --hand right --duration 60 \
      2>&1 | tee ../docs/logs/m8c-oppose-right-2026-05-21.log
  ./udex_to_xhand --config ../config.yaml --hand both --duration 60 \
      2>&1 | tee ../docs/logs/m8c-oppose-dual-2026-05-21.log
  ```
- **Step C.7 — LOCAL**：写 ADR-049（含算法选型依据 + plot 引用 + 视觉验收结果）。M8c ✅。

#### Validation
- snapshot test 通过（与 C++ 一致）
- 视觉：左右手都能与 4 根手指依次对捏
- latency 不显著恶化

---

### M8d — PID 调参（est. 0.2d）

#### Steps

- **Step D.1 — PC2**：基线复测（保持 `default_kp=100 ki=0 kd=0`，戴双手做握拳→张开→对捏→快速摆动 10 次循环），观察是否有指尖震荡 / 跟手延迟。
  ```bash
  # PC2
  ./udex_to_xhand --config ../config.yaml --hand both --duration 60 \
      2>&1 | tee ../docs/logs/m8d-pid-tune-run-1-2026-05-21.log
  ```
- **Step D.2**：若基线无明显问题 → 跳过 D.3，commit 不动 config，M8d ✅（无 ADR-050）。
- **Step D.3 — LOCAL**：若基线有震荡：把 `xhand.default_kp` 降到 80 / 60，跑 run-2；若有跟手延迟：把 kp 升到 150（厂商 M5a kp=225 是 vendor sample 上限参考，ADR-026），跑 run-3。
  ```bash
  # LOCAL — 每次改一个值 commit + push；PC2 pull → 跑 → tee log
  # PC2:
  ./udex_to_xhand --config ../config.yaml --hand both --duration 60 \
      2>&1 | tee ../docs/logs/m8d-pid-tune-run-2-2026-05-21.log
  ```
- **Step D.4**：选定一组（震荡 + 延迟综合权衡），跑 final 验证：
  ```bash
  ./udex_to_xhand --config ../config.yaml --hand both --duration 120 \
      2>&1 | tee ../docs/logs/m8d-pid-final-2026-05-21.log
  ```
- **Step D.5 — LOCAL**：写 ADR-050 if 偏离默认；commit。

#### Validation
- 视觉无明显震荡 / 无明显延迟
- latency p95 仍 < 30 ms（M7 双手 baseline `19.20 ms` 留有余量）

---

### M8e — 30 min 压测 + 抓杯验收（est. 0.3d）

#### Steps

- **Step E.1 — PC2**：30 min 压测
  ```bash
  # PC2 — XHand 装在 G1 末端 (ADR-023) — 操作员可中间放下手套休息，binary 走 watchdog 保持位置
  ./udex_to_xhand --config ../config.yaml --hand both --duration 1800 \
      2>&1 | tee ../docs/logs/m8e-stress-30min-2026-05-21.log &
  STRESS_PID=$!
  # 操作员戴双手 UDCAP，正常 teleop；中间穿插 1-2 次抓杯尝试
  # 30 min 结束后:
  wait $STRESS_PID
  ```
  从 log 末端读取 `latency_ms{n avg p95 max}` + `frames_dropped` + `sdk_errors_total`。
  - PASS 标准：`max < 50 ms`，`p95 < 30 ms`，`frames_dropped < 0.5%`，`sdk_errors == 0`。
  - 若 `max > 100 ms` 复现 M5c/M7 的 outlier → 把 outlier 触发时刻附近的 log 截取做根因分析（看是 UDP gap、send_command timing、还是 OS jitter）。可行的根因 → 写 ADR-052；不可行 → §8.5 known issues 推到 M9。
- **Step E.2 — PC2**：抓杯验收 — 5 次尝试
  ```bash
  # PC2
  ./udex_to_xhand --config ../config.yaml --hand both \
      2>&1 | tee ../docs/logs/m8e-acceptance-attempts-2026-05-21.log &
  ACC_PID=$!
  # 操作员 attempts:
  #   attempt #1: 双手包夹杯子 → 离桌 → 保持 3s → 放回 → 松手 → 录视频 m8-acceptance-attempt-1.mp4
  #   attempt #2..#5 同
  # 每次 attempt 之间间隔 ~10s 给 binary 走稳态
  # 完成后:
  kill -TERM $ACC_PID
  wait $ACC_PID
  ```
- **Step E.3 — LOCAL**：把 5 个 mp4 + log 拉回 LOCAL（git push 或 scp），整理 attempt 结果表格写入 plan §7.5。
- **Step E.4 — LOCAL**：if 成功 ≥ 3/5 → M8 ✅ → 更新 roadmap (§M8 ✅ + revision history) + SPEC §9 / §10 / §11 / §12（如 §1 「修改」表所列）+ CLAUDE.md（如 EMA 滤波启用）。否则 → 退到对应环节（拇指 / 四指 / PID）迭代。

#### Validation
- 5/5 stress test 通过预算
- 5 attempts 中 ≥ 3 抓杯成功

---

## 4. 测试策略

### 4.1 LOCAL（macOS dev）可跑的测试

| 测试 | 命令 | 预期 |
| --- | --- | --- |
| Python oracle regen + SHA self-check | `python3 scripts/dump_mapper_baseline.py --example example.json --config config.yaml --out tests/fixtures/mapper_baseline.json` | 退码 0；`mapper_baseline.json` 内 `config_yaml_sha256` 字段更新；`example_json_sha256` 不变（除非 example.json 也改） |
| C++ snapshot test (本地 build 若 cmake+g+++yaml-cpp+nlohmann_json 可用) | `mkdir -p build && cd build && cmake .. && make test_mapper_snapshot && ./tests/test_mapper_snapshot` | `L max |Δ| <= 1e-6 rad / R max |Δ| <= 1e-6 rad`；fixture SHA 与 config/example 文件 SHA 一致 |
| C++ safety unit test (本地可 build) | `cd build && make test_safety && ./tests/test_safety` | all pass（M6 ADR-035/036/037/038 行为不变） |
| Thumb prototype (M8c) | `python3 scripts/thumb_retarget_prototype.py --recording docs/logs/m8c-thumb-recording-2026-05-21.jsonl --algo A --plot scripts/thumb-algo-A.png` | 退码 0；产出 PNG；stdout 打印 algorithm A 在「对捏段」`thumb_rota1` / `thumb_rota2` 联动幅度 |
| Backward-compat smoke (M8b Step B.5) | 同上 C++ snapshot，前置：**config.yaml 没加 input_range/output_range** | `max |Δ| ≤ 1e-17 rad`（向后兼容验证；M5b/M7 字节一致） |
| **`use_new_retarget` flag-gating 守护（A.0 落地后每次 mapper / config 改动都跑一次）** | `bash scripts/verify_flag_false_byte_identical.sh` | 脚本退码 0；stdout 末端 `flag=false byte-identical to M5b/M7 baseline (OK)` |

> **macOS 上 cmake/make 注意**：仓库依赖 `xhand_control_sdk` (aarch64 .so)，主二进制 `udex_to_xhand` link 阶段会失败。**但**：`tests/test_mapper_snapshot` 与 `tests/test_safety` 只依赖 `joint_mapper` / `safety`，不 link XHand SDK。如果当前 `CMakeLists.txt` 把 SDK link 到 test target → 临时跳过 LOCAL build，只在 PC2 build。`brew install cmake yaml-cpp nlohmann-json` 满足前两个测试的依赖。

### 4.2 PC2 必须跑的测试

| 测试 | 命令 | 预期 |
| --- | --- | --- |
| 完整 build | `cd ~/udex_to_xhand && mkdir -p build && cd build && cmake .. && make -j$(nproc)` | `-Wall -Wextra -Wpedantic` 0 warning；产出 `udex_to_xhand` ELF aarch64 |
| M8a calibration（左 + 右） | `./udex_to_xhand --actions calibrate-udcap --calibrate-duration 30 --config ../config.yaml --hand <left|right>` | log 末端打印可粘 YAML 片段；每个 joint range max-min ≥ 30°（小于 → 操作员重做） |
| M8b 握拳视觉验收 | `./udex_to_xhand --config ../config.yaml --hand <left|right|both> --duration 30~60` | 四指指尖贴掌心（与 M5c 视觉对比明显闭合更紧） |
| M8c 对捏视觉验收 | 同上 | 拇指与 4 指可逐一对捏 |
| M8d PID 视觉验收 | 同上 | 无指尖震荡 / 无明显跟手延迟 |
| 30 min stress | `./udex_to_xhand --config ../config.yaml --hand both --duration 1800` | latency `max < 50 ms / p95 < 30 ms / frames_dropped < 0.5% / sdk_errors == 0` |
| 抓杯验收（5 次） | `./udex_to_xhand --config ../config.yaml --hand both` + 操作员 | 5 中 ≥ 3 成功（杯子离桌 ≥ 3 s 不掉） |
| SIGTERM 双手 graceful shutdown 回归（任何 session 结束都顺便验） | `kill -TERM <pid>` | log 末端 `mode=0 (passive) → serial close (×2) → Device closed` + 视觉松弛 |

### 4.3 回归（M5c / M6 / M7 行为不应破坏 + `use_new_retarget` flag 双向验证）

| 回归测试 | 命令 | 预期 |
| --- | --- | --- |
| **flag=false byte-identical M7（LOCAL 自动化回归）** | `bash scripts/verify_flag_false_byte_identical.sh` — 临时切 `use_new_retarget: true→false` → regen → 与 `tests/fixtures/mapper_baseline_m7_frozen.json` diff → 恢复 config/fixture（无 side effect） | 脚本退码 0；stdout 末行 `flag=false byte-identical to M5b/M7 baseline (OK)` |
| **flag=true 新算法生效（LOCAL 手动验证一次，M8b/M8c 各跑）** | 手动：当前 fixture 是 flag=true 生成的；与 `mapper_baseline_m7_frozen.json` 做 diff | `diff tests/fixtures/mapper_baseline.json tests/fixtures/mapper_baseline_m7_frozen.json` 中 9 个四指（M8b 后）+ 3 个拇指（M8c 后）的 left_rad/right_rad 浮点字段显著不同 |
| 单手回退 latency = M5c baseline（PC2） | `./udex_to_xhand --config ../config.yaml --hand left --duration 30 --use-new-retarget=false` *不*存在；通过编辑 `config.yaml.mapping.use_new_retarget=false` → push → PC2 pull → 直接跑 `--hand left` | `n ≈ 1773 avg ≈ 9.6 p95 ≈ 9.6 max < 12 ms`，与 `docs/logs/m5c-teleop-left-2026-05-18.log` ±5%。**这是最强的回归 — 主二进制 + 真硬件下 flag=false 走 M7 路径** |
| Watchdog stale → recovered（PC2） | 戴左手 → 启动 → 关 UDCAP 10s → 重开 | 10 条 `LOG_WARN stale @1Hz` + 1 条 `recovered after Nms`（M6 ADR-035/038） |
| Joint clamp 在 affine rescale 之后仍有效（PC2） | `use_new_retarget=true`；config.yaml 收窄 `index_joint1.clamp` 到 [0, 30]；戴左手 → 握拳到底 | XHand 食指 J4 物理停在 30°（ADR-021 二层防御在 affine rescale 之后仍是最终关 — flag 不影响 clamp 顺序） |
| --actions fist,palm,v,ok（PC2） | `./udex_to_xhand --actions fist,palm,v,ok --port /dev/ttyACM2 --hand left` | 4 个动作物理依次执行（ADR-032/034）；不读 mapper，因此 flag 状态不影响 |
| **A/B 物理对比（PC2 — 选做但强烈推荐）** | 同一硬件状态下：先 `use_new_retarget=false` 跑握拳 30s 录像；改 true（push→pull→重启）再跑 30s | 视觉对比：flag=false 时四指闭合留余量（M5c 行为）；flag=true 时贴掌心（M8b 修复点）。直接证据 = flag 确实在切控制路径 |

---

## 5. 验证命令（最终验收一条 — checklist）

> 以下命令是 M8 ✅ 当天 PC2 + LOCAL 各一次走的完整顺序。每步成功才能继续；失败回到对应 M8a-e step。

```bash
# === LOCAL (macOS, 准备 + commit + push) ===
cd ~/Desktop/4-2/xhand/udex_to_xhand
git status                                                    # working tree clean
git log --oneline -5                                          # M8e 最后一个 commit 在 HEAD

# 0a) flag-gating 守护回归（M8 横向约束）— 必跑
bash scripts/verify_flag_false_byte_identical.sh
# 预期: 末行 "flag=false byte-identical to M5b/M7 baseline (OK)"，退码 0
# 若失败 → 立即修，禁止进 PC2 验证

# 0b) 确认 mapping.use_new_retarget 当前状态（M8 ✅ 时应为 true）
grep 'use_new_retarget' config.yaml
# 预期: "  use_new_retarget: true   # M8 ✅ baseline"

python3 scripts/dump_mapper_baseline.py \
    --example example.json --config config.yaml \
    --out tests/fixtures/mapper_baseline.json                # fixture 与 config 同步
git diff tests/fixtures/mapper_baseline.json                  # 只有 generated_at 应该变（schema 已定型）
git push

# === PC2 (aarch64 Linux) ===
ssh g1-pc2
cd ~/udex_to_xhand && git pull

# 1) 双口枚举 + 端口 sanity check (ADR-042)
ls -l /dev/ttyACM*                                            # 期望见 ACM0 + ACM2 (或重新探到的最新)
grep 'left_serial_port\|right_serial_port' config.yaml        # 确认与实测一致；不一致 → 改 config 再 push

# 2) Build
mkdir -p build && cd build
cmake .. 2>&1 | tee ../docs/logs/m8-final-cmake-2026-05-21.log
make -j$(nproc) 2>&1 | tee ../docs/logs/m8-final-make-2026-05-21.log
# 预期: 0 warning，udex_to_xhand ELF aarch64 已就绪

# 3) Snapshot + safety unit
./tests/test_mapper_snapshot 2>&1 | tee ../docs/logs/m8-final-snapshot-2026-05-21.log
./tests/test_safety            2>&1 | tee ../docs/logs/m8-final-test-safety-2026-05-21.log
# 预期: snapshot max |Δ| < 1e-6 rad；safety all pass

# 4) 单手回退 latency 与 M5c baseline 比对（flag=true，新算法路径）
./udex_to_xhand --config ../config.yaml --hand left --duration 30 \
    2>&1 | tee ../docs/logs/m8-final-single-regression-2026-05-21.log
# 预期: latency_ms{n avg p95 max}，与 docs/logs/m5c-teleop-left-2026-05-18.log ±5%

# 4b) flag=false 回退现场切换演练（PC2 上验证 flag 真的能切控制路径）
sed -i 's/use_new_retarget: true/use_new_retarget: false/' ../config.yaml
./udex_to_xhand --config ../config.yaml --hand left --duration 15 \
    2>&1 | tee ../docs/logs/m8-final-flag-false-rollback-2026-05-21.log
# 预期视觉: 戴左手 → 握拳 → XHand 四指闭合留余量（与 M5c 一致，未贴掌心）→ 证实 flag 切换 + 算法回退生效
sed -i 's/use_new_retarget: false/use_new_retarget: true/' ../config.yaml
# 把 flag 切回 true，进入正式验收
# 预期: latency_ms{n avg p95 max}，与 docs/logs/m5c-teleop-left-2026-05-18.log ±5%

# 5) 双手 60s 干净 teleop（包含 5 指 / 握拳 / 对捏的视觉自检）
./udex_to_xhand --config ../config.yaml --hand both --duration 60 \
    2>&1 | tee ../docs/logs/m8-final-dual-clean-2026-05-21.log
# 预期: 视觉 5/5 项 PASS

# 6) 30 min stress
./udex_to_xhand --config ../config.yaml --hand both --duration 1800 \
    2>&1 | tee ../docs/logs/m8-final-stress-30min-2026-05-21.log
# 预期: max < 50ms / p95 < 30ms / frames_dropped < 0.5% / 0 sdk error

# 7) 抓杯验收 (5 attempts)
./udex_to_xhand --config ../config.yaml --hand both \
    2>&1 | tee ../docs/logs/m8-final-acceptance-2026-05-21.log &
ACC_PID=$!
# 操作员 attempts ×5；每次录 mp4
# attempt 完成后:
kill -TERM $ACC_PID
wait $ACC_PID
# 预期: 5 attempts ≥ 3 成功；log 末端见 mode=0 + close (×2)

# === LOCAL ===
git pull                                                      # 拿回 PC2 端的 docs/logs
# 整理 plan §7.5 execution record，标 M8 ✅，更新 roadmap + SPEC + CLAUDE.md
```

---

## 6. ADR 计划

| ADR | 触发条件 | 内容要点 |
| --- | --- | --- |
| **048**（必填） | M8a A.0 完成（flag 引入）+ M8b 完成 | (i) `mapping.use_new_retarget` 总开关：粗粒度 / 必填 / 默认 false / 不加 CLI override 的设计理由；flag-gating 实现在 load-time（cold path）而非 apply_one（hot path）以保 100Hz 控制环 0 额外分支。(ii) `input_range` / `output_range` schema 决定（both-or-none 校验、与 flag 的协作语义、calibration 走 weighted-sum-after 而非 source-level 的理由）；calibrate-udcap 模式 = 复用 receiver + mapper（不开 XHand，不读 flag）。(iii) `verify_flag_false_byte_identical.sh` 作为 M8 期间的固定回归手段。 |
| **049**（必填） | M8c 完成 | 三选一决策（A / B / C 中选哪个），plot 引用，对捏视觉验收结果；如选 C → 单独记录新 schema |
| **050**（条件） | M8d 选定的 PID 与厂商默认偏离 | 偏离幅度 + 决策依据 + 与 ADR-026（M5a 沿用厂商默认）的关系 |
| **051**（条件） | M8 启用 EMA 滤波 | α 取值、位置（mapper 输出后 / send 前）、与 ADR-021 二层 clamp 顺序的关系 |
| **052**（条件） | M8e 30 min stress 定位到 ~100ms outlier 根因 | 根因 + 复现方法 + 缓解措施；定位不到 → 直接 §8.5 known issues |

> M9 已预占 043-047 (roadmap rev9)。M8 ADRs 从 048 起编号以避免冲突。

---

## 7. 执行记录（Execution Record — fill at end of each sub-step）

### 7.1 M8a (flag + calibration) — fill 2026-MM-DD

- [ ] Step A.0 LOCAL `use_new_retarget` flag 引入：commit hash =
- [ ] Step A.0' LOCAL flag-gating 守护脚本：commit hash = ；首次跑 `verify_flag_false_byte_identical.sh` 退码 =
- [ ] Step A.1-A.4 LOCAL calibrate-udcap mode 代码：commit hash =
- [ ] Step A.5 PC2 build：log = `docs/logs/m8a-{cmake,make}-...`
- [ ] Step A.6-A.7 PC2 calibrate：log = `docs/logs/m8a-calibrate-{left,right}-...`，每个 joint range max-min =
- [ ] Step A.8 PC2 YAML fragment 保存：`docs/logs/m8a-calibrate-fragment-...`
- 备注（特别注意：M8a 结束时 `use_new_retarget` 仍应是 `false` — calibration 数据虽采到，但配置里不触发新算法；切 true 留到 M8b Step B.7'）：

### 7.2 M8b (four-finger affine) — fill 2026-MM-DD

- [ ] Step B.1-B.4 LOCAL schema extension：commit =
- [ ] Step B.5 LOCAL backward-compat smoke：max |Δ| = ___ rad
- [ ] Step B.6-B.7 LOCAL config + fixture regen：commit =
- [ ] Step B.9 PC2 build + tests：snapshot max |Δ| = ___ rad；test_safety = ___ pass
- [ ] Step B.10 PC2 视觉验收：left = ___ / right = ___ / dual = ___
- ADR-048：
- 备注：

### 7.3 M8c (thumb retargeting) — fill 2026-MM-DD

- [ ] Step C.1 prototype 脚本 commit =
- [ ] Step C.2 PC2 recording：duration = ___s，commit =
- [ ] Step C.3-C.4 LOCAL prototype 跑 A/B/C：选定 = A / B / C，理由 =
- [ ] Step C.5 LOCAL config + fixture regen：commit =
- [ ] Step C.6 PC2 build + 视觉验收：left oppose = ___ / right = ___ / dual = ___
- ADR-049：
- 备注：

### 7.4 M8d (PID tuning) — fill 2026-MM-DD

- [ ] Step D.1 PC2 baseline 现象：
- [ ] Step D.2-D.4 调参轮次（kp_run1 = ___ → kp_run2 = ___ → kp_final = ___）
- ADR-050（若需）：
- 备注：

### 7.5 M8e (stress + acceptance) — fill 2026-MM-DD

- [ ] Step E.1 PC2 30 min stress：`n = ___ avg = ___ p95 = ___ max = ___ ms`；`frames_dropped = ___`；`sdk_errors = ___`
  - ~100ms outlier 出现次数 = ___；根因结论 =
- [ ] Step E.2 抓杯 5 attempts:
  | # | 杯子位置 | 接触动作 | 离桌 ≥ 3s ? | 失败原因（如有） | mp4 |
  | --- | --- | --- | --- | --- | --- |
  | 1 |   |   |   |   |   |
  | 2 |   |   |   |   |   |
  | 3 |   |   |   |   |   |
  | 4 |   |   |   |   |   |
  | 5 |   |   |   |   |   |
  - 总成功数 = ___ / 5
- [ ] Step E.4 M8 ✅ 文档同步：
  - roadmap revision history 加一行 + §M8 → ✅
  - SPEC §4 / §9 / §10 / §11 / §12 更新
  - CLAUDE.md（若适用）
- ADR-052（若需）：
- 备注：

---

## 8. 风险 / 回退 / Known Issues

### 8.0 现场快速回退到 M7 baseline（任何 M8 子里程碑出问题都可触发）
- 触发：M8 任意阶段在 PC2 上发现新算法表现不如 M7（e.g. 突发抖动 / 误触 / clamp 失效 / latency 退化）
- 回退动作：`sed -i 's/use_new_retarget: true/use_new_retarget: false/' config.yaml` → 重启 `./udex_to_xhand`，立即回 M7 行为
- 后续：在 LOCAL 上分析根因 → 修 → push → PC2 pull → 切回 true 复测。**不需要 git revert 任何 commit**，flag 即可作为现场总闸。

### 8.1 拇指算法 A/B/C 都不行
- 触发：M8c Step C.6 视觉验收对捏失败
- 回退：先走 §8.0 (`use_new_retarget=false`) 回到 M7；然后 M8b 部分继续单独验证四指闭合（保留 input/output_range 但因 flag 关掉而不生效）；M8c 抓杯失败 → M8 不 PASS，开 M8b' (拇指 retargeting v2) 单独立项

### 8.2 Calibration 行程不够
- 触发：M8a YAML 片段中某关节 `max - min < 30°`
- 回退：操作员重做 calibration，特别加强失败关节的极限屈伸；若 ≥ 2 次都不达 → 检查 UDCAP 物理校准状态（重新做 `CalibrationStatus == 3` 重置）

### 8.3 Snapshot test 在 fixture regen 后仍 mismatch
- 触发：M8b/M8c 后 `./tests/test_mapper_snapshot` 报 `max |Δ| > 1e-6 rad`
- 根因：Python oracle 与 C++ 浮点顺序不一致（典型：rescale 系数计算顺序差异）
- 处理：对照 `legacy_python/joint_mapper.py::map()` 与 `src/joint_mapper.cpp::apply_one()` 的算术表达式做 token 级 diff；遵循 M5b ADR-030 教训。

### 8.4 100ms latency outlier 仍找不到根因
- 触发：M8e 30 min stress max latency 仍 ~100ms
- 处理：§8.5 known issues 推到 M9 / 永久 backlog；本身不阻塞 M8 ✅（M7 已确认 p95 在 20 ms 预算内，max 是单点 outlier 而非系统性退化，与抓杯无强相关）

### 8.5 已知问题（不阻塞 M8 ✅，落 known issues）
- (a) ~100ms latency outlier 根因未定位（如 ADR-052 未触发） — 推到 M9 或永久 backlog
- (b) `--actions calibrate-udcap` 要求操作员手动跟随屏幕脚本动作，没有自动检测「行程是否打满」 — 留作 polish
- (c) USB CDC-ACM enumeration drift（ADR-042）— 仍需每会话 re-probe；udev symlinks by serial 是 post-M8 polish
- (d) 拇指 retargeting 算法 C（tip-direction reprojection）虽未实现，但理论上是 algorithm A/B 的清洁 superset — 若 M9 后续工作需要更精细的拇指控制可重启 M8c-extended

---

## 9. 时间估算

| Sub-step | 估算 | 累计 |
| --- | --- | --- |
| M8a flag (A.0/A.0') + calibration tool | 0.4d | 0.4d |
| M8b four-finger rescale                | 0.4d | 0.8d |
| M8c thumb retargeting                  | 0.5d | 1.3d |
| M8d PID tuning                         | 0.2d | 1.5d |
| M8e stress + acceptance                | 0.3d | 1.8d |
| 文档同步 (roadmap/SPEC/CLAUDE/ADRs)     | 0.1d | 1.9d |

总估算 ≈ **1.9d**，落在 roadmap §M8 的 1.0d 估算外（roadmap 的 1d 是简化估算，未拆 a-e + 未含 flag-gating 守护工作）。该差距来自拇指 retargeting 的 prototype 工作量 + 30 min stress 的 wall-clock 时长 + flag 守护脚本与双向验证。

---

## 10. 出仓清单（Definition of Done）

- [ ] `mapping.use_new_retarget: true` 落 `config.yaml`（M8 ✅ 最终状态走新算法）
- [ ] `bash scripts/verify_flag_false_byte_identical.sh` 退码 0（M7 baseline 一键回退路径仍 alive）
- [ ] PC2 现场 `sed flag true ↔ false` + 重启演练通过：flag=true 四指贴掌心 / flag=false 留余量（视觉对比成立）
- [ ] `config.yaml` 12 关节 × 双手 = 24 行均已通过 calibration 数据 + 视觉验收
- [ ] `src/joint_mapper.{hpp,cpp}` + `legacy_python/joint_mapper.py` schema 完全同步（含 flag-gating）
- [ ] `tests/fixtures/mapper_baseline.json` 与最新 `config.yaml` SHA 一致 + `test_mapper_snapshot` 通过
- [ ] ADRs 048/049 必填；050/051/052 按触发条件填
- [ ] `docs/logs/m8{a,b,c,d,e}-*.log` 全部归档
- [ ] `docs/plans/00-roadmap.md` revision history + §M8 ✅
- [ ] SPEC.md §4 / §9 / §10 R5 R6 / §11 / §12 Q5 Q6 同步
- [ ] CLAUDE.md（若 EMA 启用）
- [ ] `docs/logs/m8-acceptance-attempt-{1..5}.mp4` 已归档；5 中 ≥ 3 成功
- [ ] `git push` 全部远端就绪，PC2 与 LOCAL diff = 0
