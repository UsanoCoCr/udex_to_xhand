# Roadmap: UDCAP → XHand Teleoperation

## Revision History

- 2026-05-16 — 第 1 次修订 — 部署主机切换到宇树 G1 PC2 (aarch64)；新增 M5 (Port to G1 PC2)，原 M5/M6/M7 顺延为 M6/M7/M8。详见 [ADR-023](../decisions/023-roadmap-pivot-g1-pc2-as-host.md).

---

原则：**M0 用 stub 打通完整 pipeline**，后续 milestone 逐个替换为真实模块。
每个 milestone 独立可验证，不超过 1 天。

---

## M0: Skeleton — Stub Pipeline 端到端 ✅

**目标**: 所有模块以 stub 形式存在，main.py 主循环跑通。无需任何硬件。

**内容**:
- 创建 `main.py`, `udcap_receiver.py`, `joint_mapper.py`, `xhand_driver.py`, `safety.py`, `config.yaml`
- `udcap_receiver` stub: 从 `example.json` 循环读取，模拟 UDP 输入
- `joint_mapper` stub: 24 个值取前 12 个直传（不做真实映射）
- `xhand_driver` stub: 打印 12 个关节弧度值到 stdout（不连硬件）
- `safety` stub: clamp 函数存在但用宽松范围 [-2, 2] rad
- `main.py`: 100Hz 循环，串联所有模块，`--mock` 模式

**完成定义**: 运行命令后，终端以 ~100Hz 打印左右手各 12 个关节值，持续 3 秒后自动退出。

```bash
python main.py --mock --duration 3
# 预期输出:
# [tick 001] L: [0.12, 0.34, ...12个值] R: [0.08, 0.21, ...12个值]
# [tick 002] ...
# ...
# Exited after 3.0s, 300 ticks
```

**依赖**: 无

---

## M1: Real UDP Receiver ✅

**目标**: 替换 UDP stub，接收 UDCAP 真实数据。

**内容**:
- `udcap_receiver.py`: 非阻塞 UDP socket，解析 JSON，提取 Parameter 数组中的 l0-l23 / r0-r23
- 处理：JSON 解析失败 → 跳帧；CalibrationStatus != 3 → 跳帧
- 独立可运行（不依赖 XHand）

**完成定义**: Windows 上运行 UDCAP HandDriver 并发送数据，Linux 终端打印 24 个参数值且与手套动作对应。

```bash
python udcap_receiver.py --port 9000 --duration 10
# 预期输出:
# [192.168.x.x] L: l0=-47.6 l1=-60.0 ... l23=-1.0 | R: r0=-9.5 ...
# CalibStatus: L=3 R=3 | FPS: 89.2
```

**依赖**: M0（文件结构）, UDCAP 硬件 + Windows PC

---

## M2: UDCAP 参数实验验证 ✅

**目标**: 确认 l0-l23 每个参数对应哪根手指的哪个关节。SPEC.md §3.1 的映射表是假设，此步骤验证。

**内容**:
- 编写 `scripts/udcap_param_identify.py`：实时显示所有参数值，高亮变化量 >5° 的参数
- 操作流程：逐根手指单独弯曲，记录哪些 l-index 响应
- 输出：更新 `config.yaml` 中的 mapping.left / mapping.right
- **实际完成方式**: 找到官方文档 ([JSON SDK 手册](https://udexreal.gitbook.io/udexreal-docs/docs-cn/c++-python-sdk/json-c++python-sdk-guan-jie-jiao-du-shi-yong-shou-ce))，提供了权威的参数映射，替代了实验数据。实验脚本保留用于未来验证。

**关键发现** (vs SPEC.md 假设):
- 参数排序: DIP→PIP→MCP (远端→近端)，非 SPEC 假设的近端→远端
- l20-l22 = MCP Roll (拇指/食指/小指)，**不是**手腕参数
- l23 = 手势识别标志 (始终-1)，不是关节
- JSON SDK 中**没有手腕参数**

**完成定义**: 产出一份已验证的参数映射表（写入 config.yaml），每个 l0-l23 都有明确的手指-关节-轴对应，附实验截图或日志。

```bash
python scripts/udcap_param_identify.py --port 9000
# 预期输出（交互式）:
# === 请只弯曲【左手拇指】 ===
# 变化参数: l0 Δ=-42.3  l3 Δ=-18.7  l4 Δ=-12.1
# 其余参数 Δ<2°
# → 确认: l0=Thumb CM Pitch, l3=Thumb MP, l4=Thumb IP
```

**依赖**: M1
**ADRs**: 009 (docs over experiment), 010 (no wrist params), 011 (DIP-first ordering), 012 (5 params discarded), 013 (thumb roll non-contiguous)

---

## M3: Real XHand Driver（单手） ✅

**目标**: 替换 XHand stub，通过 RS485 连接一只 XHand 并执行预设动作。

**内容**:
- `xhand_driver.py`: 封装 open_serial → list_hands_id → send_command → close_device
- 支持独立运行预设动作（fist / palm / V / OK）
- 验证 SDK 安装、串口权限、hand_id 发现

**完成定义**: 运行命令后 XHand 依次执行 fist → palm → V → OK，每个动作保持 1 秒。

```bash
# 在 Linux 上, conda activate xhand
python xhand_driver.py --port /dev/ttyACM0 --actions fist,palm,v,ok
# 预期: XHand 物理执行四个动作
# 终端输出:
# Connected: hand_id=0, type=Left, SDK=x.x.x
# Action fist: sent 12 joints, OK
# Action palm: sent 12 joints, OK
# ...
# Device closed.
```

**依赖**: M0（文件结构）, XHand 硬件 + Linux PC + SDK 安装
**ADRs**: 014 (CDC-ACM not ttyUSB), 015 (auto-discover hand mapping), 016 (template reuse), 017 (log-not-crash on send errors), 018 (mode=0 not powerless), 019 (deferred SDK import)

---

## M4: 单手实时遥操 ✅

**目标**: 首次端到端真实遥操——一只手套控制一只 XHand。

**内容**:
- `joint_mapper.py`: 实现 config.yaml 驱动的映射（加权求和 + 符号翻转 + 度→弧度 + clamp）
- 接入真实 UDP + 真实 XHand driver
- 基于 M2 的验证结果填写正确的 mapping 参数
- 先做左手

**完成定义**: 戴手套弯曲每根手指，XHand 对应手指跟随弯曲，方向正确，无明显反向或串扰。

```bash
python main.py --config config.yaml --hand left
# 终端输出:
# UDP: connected (192.168.x.x:9000)
# XHand: hand_id=0, type=Left, mode=3
# Running at 98.7 Hz | Latency: 12.3ms avg
# Ctrl+C to stop
```

**验证清单**:
- [x] 拇指弯曲 → J0 跟随，J1/J2 不动
- [x] 食指弯曲 → J4/J5 跟随，其余不动
- [x] 中/无/小指同理
- [x] 握拳 → XHand 握拳
- [x] 张开 → XHand 张开

**依赖**: M1, M2, M3
**ADRs**: 020 (degree-domain clamping), 021 (two-layer clamping), 022 (JOINT_ORDER constant)

---

## M5: Port to G1 PC2

**目标**: 将 M0–M4 已验证的栈从外置 Linux 开发 PC 迁移到宇树 G1 板载 PC2 (aarch64 Linux)。迁移完成后 PC2 成为唯一部署目标，外置 PC 仅作开发 / 备用。

**前置硬关卡**:
- 获取或编译匹配 aarch64 的 `xhand_controller` wheel
- 不可用则 M5 阻塞，需单独 ADR 决策（vendor 协助 / 源码编译 / 回退外置 PC）

**内容**:
- PC2 上确认 Python 3.10+，建 conda 环境，安装 aarch64 版 xhand_controller wheel
- XHand 接 PC2 USB，验证 `/dev/ttyACM*` 枚举 + dialout 串口权限
- 配置网络路径：Windows UDCAP → G1 网络 → PC2（静态 IP / 防火墙放行）
- 在 PC2 上重跑 M3 验收（fist/palm/V/OK）
- 在 PC2 上重跑 M4 验收（单手实时跟随，方向正确）

**完成定义**: 在 PC2 上单手实时跟随通过，latency 与外置 PC 上 M4 基线相比 ±5ms 内。

```bash
# 在 G1 PC2 上 (aarch64 Linux), conda activate xhand
python xhand_driver.py --port /dev/ttyACM0 --actions fist,palm,v,ok
# 预期: 与 M3 验收输出一致

python main.py --config config.yaml --hand left
# 预期: 与 M4 验收一致, latency 在 dev PC 基线 ±5ms 内
```

**依赖**: M4（算法栈已验证）, G1 PC2 已刷 Linux, aarch64 SDK wheel 可用
**ADRs**: 023 (pivot to G1 PC2)

---

## M6: Safety Layer

**目标**: 加入所有安全机制，使系统可以无人值守运行。

**内容**:
- Watchdog: 200ms 无 UDP → hold 最后位置 + 日志告警
- Joint clamp: 按 config.yaml 中 per-joint [min, max] 钳位
- Graceful shutdown: Ctrl+C 或 SIGTERM → mode=0 → close_device (ADR-023: PC2 lifecycle 可能被外部管理)
- 启动检查: hand_id 验证 + CalibrationStatus == 3

**完成定义**: 以下故障场景全部通过。

```bash
# 测试 1: Watchdog
python main.py --config config.yaml --hand left
# → 运行中关闭 UDCAP 软件
# 预期: 终端 200ms 后打印 "WATCHDOG: no UDP for 200ms, holding position"
#        XHand 保持最后姿态不动

# 测试 2: Graceful shutdown (Ctrl+C)
# → 按 Ctrl+C
# 预期: 终端打印 "Shutdown: setting mode=0 (passive)"
#        XHand 手指松弛（无力模式）

# 测试 2b: Graceful shutdown (SIGTERM)
# → kill -TERM <pid>
# 预期: 行为同 Ctrl+C, 终端打印 "Shutdown: setting mode=0 (passive)"

# 测试 3: Clamp
# → 在 config.yaml 中设 index_joint1 clamp 为 [0, 30] (度)
# → 戴手套把食指弯到底
# 预期: XHand 食指停在 30° 位置不继续弯
```

**依赖**: M5 (PC2 部署已通过)

---

## M7: 双手集成

**目标**: 连接第二只 XHand，左右手同时遥操。

**前置检查** (ADR-023):
- PC2 暴露的 USB 数量是否够同时接两只 XHand
- RS485 双手寻址方案: 同串口两个 hand_id, 还是两个串口?

**内容**:
- 验证 RS485 双手寻址（同一串口两个 hand_id，或两个串口）
- config.yaml 增加 right hand mapping（可能需要符号翻转）
- main.py 循环中同时发送左右手指令

**完成定义**: 双手同时独立跟随手套动作。

```bash
python main.py --config config.yaml
# 终端输出:
# XHand Left:  hand_id=0, type=Left
# XHand Right: hand_id=1, type=Right
# Running dual-hand at 95.2 Hz
```

**验证清单**:
- [ ] 只动左手 → 只有左 XHand 动，右 XHand 不动
- [ ] 只动右手 → 同理
- [ ] 双手同时握拳 → 双 XHand 同时握拳
- [ ] 左右手做不同动作 → 各自正确

**依赖**: M6, 第二只 XHand 硬件

---

## M8: 调优 + 验收

**目标**: PID 调参 + 映射微调 → 通过验收测试（拿起杯子）。

**内容**:
- 调整 kp/kd 平衡响应速度与稳定性（消除震荡或迟滞）
- 微调 mapping 权重和 clamp 范围（尤其是拇指，3 DOF 映射最复杂）
- 如有需要，加低通滤波平滑 UDP 抖动
- 连续运行 30 分钟压测

**完成定义**: 通过验收测试。

```
验收测试流程:
1. XHand 装在 G1 手臂末端 (ADR-023, 不再手持)
2. 启动系统, 双手进入遥操模式
3. 操作员戴手套, 控制双 XHand 抓取桌上杯子
4. 杯子离开桌面并保持 3 秒
5. 放下杯子

判定: 连续尝试 5 次, 成功 ≥3 次 = PASS
```

**依赖**: M7

---

## 依赖图

```
M0 (skeleton)
├── M1 (UDP receiver)
│   └── M2 (param verify)
├── M3 (XHand driver)
│
└── M4 (single-hand teleop) ← M1 + M2 + M3
    └── M5 (port to G1 PC2)   ADR-023
        └── M6 (safety)
            └── M7 (dual hand)
                └── M8 (tuning + acceptance)
```

## 时间估算

| Milestone               | 估算 | 累计 |
| ----------------------- | ---- | ---- |
| M0 Skeleton             | 0.5d | 0.5d |
| M1 UDP Receiver         | 0.5d | 1d   |
| M2 Param Verify         | 0.5d | 1.5d |
| M3 XHand Driver         | 0.5d | 2d   |
| M4 Single-hand Teleop   | 1d   | 3d   |
| M5 Port to G1 PC2       | 0.5d | 3.5d |
| M6 Safety               | 0.5d | 4d   |
| M7 Dual Hand            | 0.5d | 4.5d |
| M8 Tuning + Acceptance  | 1d   | 5.5d |

M1 和 M3 无依赖关系，可并行。最快路径: **4.5 天** (前提: aarch64 wheel 可用; 否则 M5 工期不确定)。
