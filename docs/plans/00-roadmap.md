# Roadmap: UDCAP → XHand Teleoperation

## Revision History

- 2026-05-16 — 第 1 次修订 — 部署主机切换到宇树 G1 PC2 (aarch64)；新增 M5 (Port to G1 PC2)，原 M5/M6/M7 顺延为 M6/M7/M8。详见 [ADR-023](../decisions/023-roadmap-pivot-g1-pc2-as-host.md).
- 2026-05-16 — 第 2 次修订 — 发现厂商提供的是 `xhand_control_sdk/` (aarch64 **C++** SDK + headers + .so)，**没有 Python wheel**。M5 阻塞解除；决定弃用 Python 算法栈，**在 PC2 上把 M1/M3/M4 的 Python 模块整体重写为单一 C++ 二进制**，避免 Python↔C++ FFI 跨语言开销，并匹配仓库内 `xhand_control_ros2.hpp` 的语言基线。`config.yaml`/ADRs/example.json 等已验证资产保留。M5 拆为 M5a/M5b/M5c，工期估算 0.5d → 2d。CLAUDE.md / SPEC.md 中的 Python 基线需在 M5 完成后同步更新。
- 2026-05-18 — 第 3 次修订 — M5a 在 G1 PC2 上执行通过（左手，joint 4 ±0.1 rad，SDK 1.4.3，hand_id=1，serial 012L320220250728005）。新增 ADR-024 / 025 / 026 / 027 记录 M5a 非显然决定（vendor 示例作 bring-up harness / 厂商源不入库 / 沿用厂商 PID kp=225 / joint 4 作 smoke joint）。Execution Record 见 [plan §6](./20260516-m5a-vendor-sdk-pc2-bringup-plan.md#6-execution-record--filled-2026-05-18)，日志归档 `docs/logs/m5a-test-serial-2026-05-18.log`。
- 2026-05-18 — 第 4 次修订 — M5b 在 G1 PC2 上执行通过（C++17 单二进制 `udex_to_xhand` 落地；mock 300/300 ticks；snapshot test L max |Δ|=0.0e+00 rad / R max |Δ|=1.4e-17 rad，远低于 1e-6 tolerance；receiver-only @ UDCAP 192.168.3.24 收 616/1000 包 0 parse errors；`-Wall -Wextra -Wpedantic` 无警告）。新增 ADR-028 / 029 / 030 / 031（纯 C++ 重写 / CalibrationStatus pre-check 拆 UDCAP+XHand 两侧 / snapshot fixture 用 SHA-256 自校验 / `legacy_python/` 提前到 M5b 重组）。`legacy_python/` 创建于本 milestone 作为 Python 原型的归集点。Execution Record 见 [plan §11](./20260518-m5b-cpp-rewrite-plan.md#11-6-execution-record-filled-at-end-of-m5b)，日志归档 `docs/logs/m5b-{cmake,make,mock-run,snapshot-test,receiver}-2026-05-18.log`。
- 2026-05-19 — 第 6 次修订 — M8 范围澄清：明确拇指重定向不能沿用 copy-rotation —— XHand 拇指零位与其余四指掌面近似正交，与 UDCAP 拇指零点不同源，逐关节直传会错配对掌；升级为独立的 retargeting 算法工作项，与四指 mapping 解耦。原"微调 mapping"一条拆分为「拇指重定向算法重做」+「四指 mapping 微调」两条；算法形态留到 M8 调研后定，决策走新 ADR。
- 2026-05-19 — 第 7 次修订 — **M6 在 G1 PC2 上 5/5 安全场景通过；M6 整体 ✅**。Watchdog stale 反应（10 条 1Hz `LOG_WARN` + `recovered after 87ms` + hand 物理保持）/ SIGINT mode=0（视觉，log 因 `tee` 截断不完整）/ SIGTERM mode=0（log 完整落盘）/ per-joint clamp 收窄后食指 J4 停在 30°（视觉）/ Startup gate 10s 超时 `LOG_ERROR` + driver 析构 mode=0+close —— 全部行为级符合 SPEC §5。新增 ADR-035（stale-resend @ 100Hz + LOG_WARN @ 1Hz）/ 036（startup gate 10s, exit 2）/ 037（snapshot fixture 随 config.yaml schema 变化必须 regen；M6 实测被 ADR-030 SHA 自校验在 P0 捕获）/ 038（recovered Nms 语义 = 自最近一次 WARN 起、非总停发时长；与 plan §4.2 P1 预期错位的记录）。Known issues：P2 SIGINT 下 `tee` 截断（M5c §6.6 SIGTERM 同源问题在 SIGINT 上仍存在，操作员视觉确认 OK，未来用背景化 + `kill -INT` 模式）；watchdog/clamp 长 session 单点 latency outlier ≈ 100ms（M5c 是 10.68ms；p95 稳定 9.62ms 不变；留 M8 stress test 排查）。Execution Record 见 [plan §8](./20260519-m6-safety-hardening-plan.md#8-execution-record-filled-at-end-of-m6)，日志归档 `docs/logs/m6-{build,watchdog,sigint,sigterm,clamp,startup-gate}-2026-05-19.log`（6 个）。
- 2026-05-19 — 第 8 次修订 — **M7 在 G1 PC2 上双手集成验证通过；M7 整体 ✅**（不过覆盖度低于 M5c/M6，详见下文）。架构关键转折：plan rev1 假设的单口 RS485 multi-drop 在硬件 probe 中被证伪 —— PC2 实际两个独立 USB CDC-ACM 端点，一只 XHand 一条总线。同日 plan 改 rev2（commit b2253da），代码改双 `XHandDriver`（commit 4111a82：`main.cpp` 持 `driver_left/right` 两个 `optional`、`config.yaml` schema 拆 left/right_serial_port、ADR-039 落地）。rev1 引入的 `XHandDriver::open(require_both)` 由 commit 375e8e7 revert。**执行验证状态**：§4.1 P0 build / snapshot / test_safety / §4.2 双口枚举 / §4.5 P2b' dual SIGTERM mode=0×2 + close ✅ log 实证；`--hand left` 单手回退 latency `n=310 avg=9.59 p95=9.63 max=10.69ms` 与 M5c baseline 字节一致 → 双驱动重构无单手回退；§4.5 P1' watchdog stale / P3' clamp / P5' startup gate timeout / P6' A/B fail-closed / §4.3 右手单指逐项验收 / §4.4 60s 双手干净 teleop —— **未 log 实证**，操作员视觉确认通过。**双手延迟**（m7-watchdog-dual 25s session, n=1164）`avg=19.38 p95=19.20 max=111.17 ms`：avg/p95 ≈ 2× M5c 单手 baseline 是双 `send_command` 串行 over 两条独立 USB-RS485 路径的结构性代价，p95 在 20ms 预算内 0.80ms 余量；max=111ms 单点 outlier 与 M6 roadmap rev 7 ~100ms outlier 同源，推到 M8 30 min 压测调查。新增 ADR-039（双口架构，retire rev1 ADR-040）/ ADR-041（双口延迟特性 + ~100ms outlier 推到 M8）/ ADR-042（PC2 CDC-ACM 枚举 session-local，需每会话重探）。同日两次硬件 probe 间右手端口从 ACM1 漂到 ACM0（ADR-042 直接 trigger），M7 ✅ commit 把 `config.yaml` pin 到实测 ACM2/ACM0。SPEC §2 架构图重画为双 RS485 路径；§10 R3 关闭（多 drop 不存在），新增 R11（latency outlier）/ R12（USB 枚举漂移）；§12 Q3 状态更新（右手 sign 验证留 M8），Q4 RESOLVED。Known issues 落 plan §8.4：(a) plan §4.4 / §4.5 P1'/P5'/P6' A/B 未 log captured，M8 需保持 log 纪律（背景化 + `kill -TERM`，不要 SIGINT）；(b) right-hand mapping starting hypothesis 未充分验证，M8 验收测试（抓杯）是该 hypothesis 的实战检验；(c) 双口架构 USB 枚举漂移已影响一次会话（plan rev2 内）。Execution Record 见 [plan §8](./20260519-m7-dual-hand-integration-plan.md#8-execution-record-fill-at-end-of-m7)，日志归档 `docs/logs/m7-{cmake,make,snapshot,test-safety,enum,watchdog-dual,clamp-dual,single-regression}-2026-05-19.log`（8 个）。
- 2026-05-18 — 第 5 次修订 — **M5c 在 G1 PC2 上真实硬件复测通过；M5 整体 ✅**。`--actions fist,palm,v,ok`（M3 等价物）4 个动作全部物理观察通过（hand_id=1 type=Left，SDK 1.4.3；2 次 startup CRC，ADR-017 log-not-crash 覆盖）。`--config ../config.yaml --hand left --duration 30`（M4 等价物）戴 UDCAP 手套单手实时跟随：5 项手指 + 握拳 + 张开目检通过，1773 valid frames / 3000 ticks / 0 parse errors，**latency_ms{n=1773 avg=9.60 p95=9.62 max=10.68}** —— 远低于 SPEC §9 phase 3.9 的 50ms 阈值（avg ≈ 19% ceiling）。SIGTERM → mode=0 + close_device 视觉验证通过（log 因 plan §6.6 `kill -TERM $!→tee` 截断，已 post-run 修复）。新增 ADR-032 / 033 / 034（preset 表 header-only + Python 字节一致脚本 / latency stats 用 vector+sort 而非 streaming / `--actions` 模式 tolerate 缺失 config.yaml）。§6.2 LOCAL→PC2 sync + §6.10 log pull 因 PC2 sshd 预 banner 拒连未走 rsync，改用 git push/pull + 手工 log 复制；§6.7 网络专用 log 未产，UDP 通路由 §6.8 teleop log 中 "first packet from 192.168.3.24" + 0 parse errors 反证。Execution Record 见 [plan §11](./20260518-m5c-pc2-hardware-revalidation-plan.md#11-execution-record-filled-2026-05-18)，日志归档 `docs/logs/m5c-{cmake,make,ttyacm,actions,sigterm,teleop-left}-2026-05-18.log`（6 个）。
- 2026-05-20 — 第 10 次修订 — 新增 **M10: 中文 README**，作为项目交付 milestone。在 M9 完成后写一份面向他人的中文 README，覆盖前置依赖 / 硬件接线 / 构建 / 运行 / 数据采集 / 常见故障，目标是让没参与开发的人能照着 README 把代码库跑起来并理解架构。预计 0.5d。
- 2026-05-20 — 第 9 次修订 — 新增 **M9: ROS2 数据记录**，作为项目收尾 milestone。背景：灵巧手部署在机器人本体上，用户担心 XHand action 数据与机器人 action 数据时间轴不同步；师兄机器人侧已用 ROS2 + rosbag2 录所有 action+timestamp，灵巧手做同 stack 集成 → 同一 /clock 节拍 publish 即天然同步。用例升级为「为下游 BC/IL 训练采集 (observation, action) pair」，因此 observation 必须包含 XHand `read_state()` 实测关节角，而不只是 commanded 值。本 milestone 同时打破 CLAUDE.md 现有两条约束（«Do NOT use ROS2» / «Do NOT read XHand sensors»），各走独立 ADR 限定废除范围（仅 M9 数据记录路径 / 仅 offline observation 不进控制闭环）。决策依据：2026-05-20 用户访谈四问 — ROS2（distro 待师兄确认）/ udex_to_xhand 自身变 ROS2 node 直接 publish / read_state 主循环同步满采 / sensor_msgs/JointState + 自定义 HandDiagnostics 五 topic / PC2 本地起独立 rosbag2 record。预计 2d，拆 M9a-e。

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

## M5: Port to G1 PC2 (C++ 重写底层栈) ✅

**目标**: 在 G1 PC2 (aarch64 Linux) 上把 M0–M4 验证过的算法栈用 C++ 重写为单一可执行二进制 `udex_to_xhand`，取代 main.py + 全部 Python 模块。迁移完成后 PC2 成为唯一部署目标，外置 PC 仅作开发 / 备用。

**为什么是 C++ 重写而不是 Python 绑定**:
- 厂商当前在 aarch64 上仅交付 **C++ SDK** (`xhand_control_sdk/`: headers + `libxhand_control.so` ELF aarch64)，无 Python wheel
- 若用 pybind11 / ctypes 包一层，等于在 100Hz 实时控制循环里凭空插入一段 Python↔C++ FFI 开销
- 仓库内 reference (`xhand_control_ros2.hpp:68-72`) 已是 C++，语言基线一致

**前置已解锁**:
- `xhand_control_sdk/lib/libxhand_control.so` 确认为 ELF 64-bit ARM aarch64 ✅
- `xhand_control.hpp` 暴露了 M3/M4 所需的全部接口 (`open_serial` / `list_hands_id` / `get_hand_type` / `send_command` / `read_state` / `close_device` / `get_sdk_version`)
- `data_type.hpp` 定义 `HandCommand_t` / 12×`FingerCommand_t` / `HandState_t`，与 SPEC.md §中假设一致
- 之前的「aarch64 wheel 缺失」blocker **不再适用**

**保留资产 (不重做)**:
- `config.yaml`: M2/M4 已验证的 mapping / PID / clamp 参数 — C++ 侧用 yaml-cpp 解析
- 全部 ADRs 009-022 (硬件行为级决策，与实现语言无关，照常生效)
- `example.json` (M0 mock 输入)

**内容**:

### M5a · C++ SDK 在 PC2 上原生验证 (0.5d) ✅
- PC2 安装依赖: `cmake g++ libcurl4-openssl-dev libssl-dev nlohmann-json3-dev libyaml-cpp-dev`
- 编译厂商示例: `cd xhand_control_sdk/tests && mkdir build && cd build && cmake .. && make`
- 配置 `/dev/ttyACM*` 串口权限 (dialout 组), 运行 `./test_serial` (注意厂商示例默认 `/dev/ttyUSB0`，按 ADR-014 改为 `/dev/ttyACM0`)
- 验收: 能枚举 hand_id、显示 type/SN、单关节 send_command 成功 → 证明硬件 + .so 在 PC2 上闭环可用
- **结果 (2026-05-18)**: SDK 1.4.3 / hand_id=1 / type=L / SN=012L320220250728005 / joint 4 commanded 0.1 rad → read-back 0.0843551 rad (> 0.05 阈值)。详见 [bring-up plan](./20260516-m5a-vendor-sdk-pc2-bringup-plan.md) + ADRs 024/025/026/027。

### M5b · 项目 C++ 化 (1d) ✅
- 新建 `src/` + 顶层 `CMakeLists.txt` (`find_package(xhand_control HINTS xhand_control_sdk/share)`)，产出 `udex_to_xhand` 可执行文件
- 重写以下模块（对应 Python 文件功能逐一翻译，行为不变）:
  - `main.cpp`: 100Hz 控制循环 + SIGINT/SIGTERM 处理 + CLI args (`--config`, `--hand`, `--duration`, `--mock`)
  - `udcap_receiver.{hpp,cpp}`: 非阻塞 UDP socket，nlohmann_json 解析，提取 l0-l23 / r0-r23，CalibStatus!=3 跳帧
  - `joint_mapper.{hpp,cpp}`: yaml-cpp 读 `config.yaml`，加权求和 + 符号翻转 + deg→rad + clamp
  - `xhand_driver.{hpp,cpp}`: 包装 `xhand_control::XHandControl`（open_serial → list_hands_id → send_command → close_device）；按 ADR-029 不读 XHand 端 calib
  - `safety.{hpp,cpp}`: watchdog (200ms 无 UDP) + per-joint clamp 作为 fail-safe (二层防御)
- 单元级验收: `--mock` 模式以 ~100Hz 打印左右手 12 关节值 (等价于原 Python M0 stub 验收)
- **结果 (2026-05-18)**: build 干净（g++ 11.4.0 / nlohmann_json 3.10.5 / OpenSSL 3.0.2 / CURL 7.81.0；`-Wall -Wextra -Wpedantic` 0 警告）。mock 300 ticks 全部产生 12+12 关节输出。Snapshot 等价测试 L max |Δ|=**0.0e+00 rad**、R max |Δ|=**1.4e-17 rad**（远低于 1e-6 tolerance；Python ↔ C++ 算法位级一致）。Receiver-only 在 UDCAP @ 192.168.3.24 在线时收 616/1000 ticks、0 parse errors、calib L=3 R=3 稳定 10s。Python 原型 6 个文件迁入 `legacy_python/`，`joint_mapper.py` 留作 snapshot oracle。详见 [plan §11](./20260518-m5b-cpp-rewrite-plan.md#11-6-execution-record-filled-at-end-of-m5b) + ADRs 028/029/030/031。

### M5c · PC2 上重跑 M3 / M4 验收 (0.5d) ✅
- XHand 接 PC2 USB，确认 `/dev/ttyACM*` 枚举 (ADR-014)
- 网络: Windows UDCAP → G1 网络 → PC2（静态 IP + 防火墙放行 UDP 9000）
- 重跑 M3 验收: fist / palm / V / OK 四个动作 (C++ 版本)
- 重跑 M4 验收: 戴手套单手实时跟随，方向正确，无明显延迟
- **结果 (2026-05-18)**: PC2 上 `udex_to_xhand` C++ 二进制完成 M3 + M4 行为级硬件复测。`--actions fist,palm,v,ok` 4/4 动作物理观察通过；2 次 startup CRC 未阻塞动作（ADR-017）。`--hand left --duration 30` 戴手套实时跟随：5 项手指 + 握拳 + 张开目检通过，`latency_ms{n=1773 avg=9.60 p95=9.62 max=10.68}`（**远低于 SPEC §9 50ms 阈值**，分布 max-min ≈ 1.15ms 极紧）、0 parse errors。SIGTERM → mode=0 + close_device 视觉验证通过。详见 [plan §11](./20260518-m5c-pc2-hardware-revalidation-plan.md#11-execution-record-filled-2026-05-18) + ADRs 032/033/034。

**完成定义**: PC2 上单手实时跟随通过，latency 与外置 PC 上 Python 版 M4 基线相比改善或在 ±5ms 内（C++ 版本预期等好或更稳）。**实际**：Python M4 dev-PC 没有归档具体 latency 数值（M4 plan §4 只跑视觉验收），所以以 SPEC §9 的 `<50ms` 绝对界为权威 —— M5c 实测 avg/p95 ≈ 9.6ms，约 19% ceiling，**通过**。

```bash
# M5a: 厂商 C++ SDK 原生验证
cd xhand_control_sdk/tests && mkdir -p build && cd build
cmake .. && make
./test_serial   # 预期: list_hands_id 非空, type=Left/Right, 单关节命令生效

# M5b: 编译本项目 C++ 二进制
cd <repo-root> && mkdir -p build && cd build
cmake .. && make
ls ./udex_to_xhand   # 预期: ELF aarch64 可执行文件

# M5c: PC2 上 M3 / M4 复跑
./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok
./udex_to_xhand --config config.yaml --hand left
```

**依赖**: M4 (Python 版算法已验证，`config.yaml` 数据已校准), G1 PC2 已刷 Linux, `xhand_control_sdk/` 已就绪 ✅
**ADRs**: 023 (pivot to G1 PC2); 024 (vendor sample as M5a harness); 025 (vendor source pristine, sed not committed); 026 (M5a uses vendor PID defaults, not CLAUDE.md); 027 (joint 4 ±0.1 rad as smoke joint); 028 (pure C++ rewrite, no pybind11/ctypes binding); 029 (CalibrationStatus pre-check is UDCAP-side only — XHand driver does not assert it); 030 (snapshot fixture as committed JSON with SHA-256 self-check); 031 (`legacy_python/` reorg performed during M5b, user-directed); 032 (preset action table header-only with Python byte-equality script); 033 (M5c latency stats — vector+sort for exact p95, not streaming); 034 (`--actions` mode tolerates missing/invalid `--config`)
**Post-M5 文档同步**: 完成后需更新 CLAUDE.md (Python 3.10+ 基线 → C++17 + cmake)、SPEC.md、README 中关于运行命令与依赖的部分

---

## M6: Safety Layer (C++) ✅

**目标**: 在 M5b 的 C++ 二进制基础上，把 M5b 里只是占位的安全模块强化到可无人值守运行。所有安全逻辑都在 `src/safety.{hpp,cpp}` 实现。

**内容**:
- Watchdog: 200ms 无 UDP → hold 最后位置 + 日志告警 (基于 `std::chrono::steady_clock`)
- Joint clamp: 按 `config.yaml` 中 per-joint [min, max] 钳位 (mapper 已做主钳位，safety 做 fail-safe 二次钳位，ADR-021)
- Graceful shutdown: `std::signal(SIGINT/SIGTERM, …)` → `xhand_control.send_command(mode=0)` → `close_device()` (ADR-018, ADR-023)
- 启动检查: `list_hands_id()` 非空 + `get_hand_type()` 一致 + 等待首个有效 UDP 包内 CalibrationStatus == 3

**完成定义**: 以下故障场景全部通过。

```bash
# 测试 1: Watchdog
./udex_to_xhand --config config.yaml --hand left
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

**依赖**: M5 (C++ 二进制在 PC2 上跑通)
**ADRs**: 035 (watchdog stale-resend @ 100Hz + LOG_WARN @ 1Hz); 036 (startup gate timeout 10s, exit 2); 037 (snapshot fixture regen on any config.yaml schema change); 038 ("watchdog: recovered after Nms" = time since last WARN, not total outage duration)
**结果 (2026-05-19)**: PC2 上 5/5 安全场景行为级通过 — (P1) Watchdog 关闭 UDCAP ~10s 期间出 10 条 stale `LOG_WARN`（1Hz 限速精确）+ 重开后一行 `recovered after 87.2445ms` + hand 物理保持最后姿态；(P2) Ctrl+C 视觉确认 mode=0；(P2b) `kill -TERM` log 完整落盘 `mode=0 (passive) → serial close → Device closed → latency_ms{n=309 ... max=10.6325}`；(P3) `config.yaml` 收窄 index_joint1.clamp 到 [0,30] 后操作员视觉确认食指 J4 物理停在 30°；(P5) UDCAP 不发包时 binary 在 ~10s 后干净退出 `[ERROR] startup gate: no calibrated UDP frame in 10s; aborting` + 析构走 mode=0+close。新增 ADR-035 / 036 / 037 / 038（前两个 plan §7 预登记；037 因 M6 加 `startup_timeout_s` 触发 fixture SHA mismatch 由 ADR-030 检出，必须随 config.yaml schema 变化 regen；038 因 plan §4.2 P1 expected N 与代码实际语义错位补记）。详见 [plan §8](./20260519-m6-safety-hardening-plan.md#8-execution-record-filled-at-end-of-m6)，日志归档 `docs/logs/m6-{build,watchdog,sigint,sigterm,clamp,startup-gate}-2026-05-19.log`（6 个）。Known issues 落 plan §8.4 / §8.5：P2 SIGINT 因 Ctrl+C 直发 foreground process group 导致 `tee` 截断（log 在第一条 startup CRC WARN 后断开，操作员视觉确认通过，建议未来沿 P2b 背景化 + `kill -INT` 模式）；watchdog / clamp 长 session 出现单点 latency ≈ 100ms outlier（p95 仍稳定 9.62ms，留 M8 stress test 排查）。

---

## M7: 双手集成 (C++) ✅

**目标**: 连接第二只 XHand，左右手同时遥操。

**前置检查** (ADR-023):
- PC2 暴露的 USB 数量是否够同时接两只 XHand
- RS485 双手寻址方案: 同串口两个 hand_id, 还是两个串口?

**内容**:
- 验证 RS485 双手寻址（同一串口两个 hand_id，或两个串口）
- `config.yaml` 增加 right hand mapping（可能需要符号翻转）
- `src/main.cpp` 控制循环中同时构造并发送左右手 `HandCommand_t`
- `src/xhand_driver.{hpp,cpp}` 扩展为支持持有 1 或 2 个 hand_id 的引用，复用单个 `XHandControl` 实例

**完成定义**: 双手同时独立跟随手套动作。

```bash
./udex_to_xhand --config config.yaml
# 终端输出:
# XHand Left:  hand_id=0, type=Left
# XHand Right: hand_id=1, type=Right
# Running dual-hand at 95.2 Hz
```

**验证清单**:
- [x] 只动左手 → 只有左 XHand 动，右 XHand 不动 (operator 视觉确认 2026-05-19)
- [x] 只动右手 → 同理 (operator 视觉确认 2026-05-19)
- [x] 双手同时握拳 → 双 XHand 同时握拳 (operator 视觉确认 2026-05-19)
- [x] 左右手做不同动作 → 各自正确 (operator 视觉确认 2026-05-19)

**依赖**: M6, 第二只 XHand 硬件
**ADRs**: 039 (RS485 two-port split, retires rev1 ADR-040); 041 (dual-mode latency ≈ 2× single baseline, ~100ms outlier deferred to M8); 042 (PC2 CDC-ACM enumeration is session-local — re-probe per session)
**结果 (2026-05-19)**: PC2 上 `udex_to_xhand --hand both` 双手集成验证通过。Plan rev1 假设的单口 RS485 multi-drop 被硬件 probe 证伪 → plan rev2 + 双 `XHandDriver` 架构重做 → 5 个 sub-step log 实证通过（build / enum / safety + dual SIGTERM mode=0×2 + close / `--hand left` 单手回退字节一致 M5c）+ 5 个 sub-step operator 视觉确认（§4.3 右手单指 / §4.4 60s 双手 / §4.5 P1' watchdog stale / P3' clamp / P5' / P6' A/B）。双口延迟 avg/p95 = 19.4 / 19.2 ms（M5c 单手 9.6 / 9.6 的 ≈2.0×，p95 在 20ms 预算内 0.80ms 余量），max=111ms 单点 outlier 推到 M8。同日 PC2 USB CDC-ACM 枚举从 ACM1 漂到 ACM0（ADR-042 trigger），`config.yaml` pin 到实测 ACM2/ACM0。详见 [plan §8](./20260519-m7-dual-hand-integration-plan.md#8-execution-record-fill-at-end-of-m7)，日志归档 `docs/logs/m7-{cmake,make,snapshot,test-safety,enum,watchdog-dual,clamp-dual,single-regression}-2026-05-19.log`（8 个）。

---

## M8: 调优 + 验收

**目标**: PID 调参 + 映射微调 → 通过验收测试（拿起杯子）。

**内容**:
- 在 `config.yaml` 调整 kp/kd 平衡响应速度与稳定性（消除震荡或迟滞） — C++ 二进制 hot-reload 不在范围，每轮调参重启 `./udex_to_xhand`
- **拇指重定向算法重做（关键工作项）**: XHand 拇指零位指向手掌外侧、与其余四指掌面近似正交，与 UDCAP 手套上拇指的零点姿态不同源；M2/M4 沿用的"逐关节加权求和 + 符号翻转 + 度→弧度"在拇指上等价于 copy-rotation，无法正确还原对掌 (thumb opposition)，会出现方向错配 / 行程不足 / 无法与食指对捏，直接影响验收（抓杯子依赖对掌力闭合）。需要在 `src/joint_mapper.{hpp,cpp}` 中为拇指 3 DOF (thumb_bend / thumb_rota1 / thumb_rota2) 单独实现一条专用 retargeting pipeline：把 UDCAP 中与拇指相关的参数 (l0 / l3 / l4 + l20 thumb roll，参见 ADR-013) 先组合成等价的拇指目标姿态，再映射到 XHand 拇指坐标系，与其余四指 mapping 完全解耦。算法形态在本 milestone 调研后确定（候选方向：指尖位置 IK / 对掌角度匹配 / 拇指方向向量重投影），所选算法 + 参数写回 `config.yaml`，决策落入新 ADR
- 四指 (index / mid / ring / pinky) 在现有 mapping 框架下微调权重与 clamp 范围
- 如有需要，在 `src/joint_mapper.cpp` 加低通滤波 (e.g. exponential smoothing) 平滑 UDP 抖动 — 系数走 `config.yaml`
- 连续运行 30 分钟压测，监控终端日志中的 cycle latency / dropped frames / SDK errors

**完成定义**: 通过验收测试。

```
验收测试流程:
1. XHand 装在 G1 手臂末端 (ADR-023, 不再手持)
2. ./udex_to_xhand --config config.yaml 启动, 双手进入遥操模式
3. 操作员戴手套, 控制双 XHand 抓取桌上杯子
4. 杯子离开桌面并保持 3 秒
5. 放下杯子

判定: 连续尝试 5 次, 成功 ≥3 次 = PASS
```

**依赖**: M7

---

## M9: ROS2 数据记录（训练数据采集）

**目标**: 把 `udex_to_xhand` 从纯 C++ 二进制升级为 ROS2 node，把双手实时遥操中产生的 action（commanded 12 关节）+ observation（XHand `read_state()` 12 关节实测）+ 原始 24 路 UDCAP 输入 + 三段时间戳全部 publish 到 ROS2 topic，在 PC2 本地用独立 `ros2 bag record` 落盘。输出 schema 与师兄机器人侧现有 rosbag 共用同一 `/clock`、共用 `sensor_msgs/JointState` 标准类型 → 双方 bag 可按 timestamp 直接 merge，供下游 BC / IL 模型训练消费。这是项目收尾 milestone — 把已通过 M8 验收的遥操变成可重放、可训练的数据流水线。

**为什么用 ROS2 + 为什么 read_state 必须打开**:
- 师兄机器人侧已用 ROS2 + rosbag2 录所有 action / timestamp。灵巧手做同 stack 集成是「与机器人侧时间轴对齐」成本最低的路径 — 同一 ROS `/clock` 节拍下 publish 即天然同步，不需要事后按墙钟 merge
- 用例升级为 BC/IL 训练数据采集：训练样本是 (observation, action) pair，observation 必须是 XHand 实测关节角 `read_state()`，不能只用 commanded 值（否则训出来的模型不知道真实硬件响应特征）
- 替代方案（独立 bridge node / 内嵌 rosbag2 writer）都引入多一跳时间戳同步问题；udex_to_xhand 自身变 ROS2 node 是单一 `/clock` 语义、最少跨进程跳转

**两条 CLAUDE.md 现有约束需在本 milestone 显式废除（各走独立 ADR 限定范围）**:
- 「Do NOT use ROS2 (`xhand_control_ros2/` exists in repo as reference only)」 — 原意是 100Hz 控制循环不引入 ros2_control 框架开销；M9 仅在主循环出口加 publish 路径，不引入 ros2_control / ros2_controllers / lifecycle_node 等控制框架。走 **ADR-043** 限定废除范围
- 「Do NOT read XHand sensors / 实现 force feedback」 — 原意是防止把 `read_state` 接入实时控制闭环（反馈控制需独立设计）；M9 仅在主循环 `send_command` 之后调用一次 `read_state`，结果只 publish 到 ROS topic，不参与下一帧 command 计算。走 **ADR-044** 限定废除范围

**前置已确认（2026-05-20 用户访谈）**:
- ROS 版本: ROS2（具体 distro 待师兄确认，候选 Humble / Iron / Jazzy；本 milestone M9a 第一步即敲定）
- 集成形态: `udex_to_xhand` 本身变 ROS2 node，直接 publish，不走外挂 bridge / 不走内嵌 rosbag2 writer
- `read_state` 策略: 主控环路上同步调用，100Hz 满采 — observation 与 action 同帧
- Msg schema: action + state 走 `sensor_msgs/JointState`；raw UDCAP + safety + latency 走自定义 `udex_to_xhand_msgs/HandDiagnostics`
- Topic 命名: `/xhand/{left,right}/{command,state}` 四个 JointState + `/xhand/diagnostics` 一个
- Bag ownership: PC2 本地起独立 `ros2 bag record /xhand/*`（不依赖机器人侧 record 配置）

**内容**:

### M9a · ROS2 环境 + 师兄侧对齐（0.25d）
- 与师兄敲定: 机器人侧 ROS2 distro / `/clock` 来源（rosbag --clock / sim_time / 真实墙钟）/ 现有 action topic namespace（确认 `/xhand/...` 命名不与已有 topic 冲突）/ 师兄 dataloader 是否能直接吃 `sensor_msgs/JointState`
- PC2 上安装对应 distro: `sudo apt install ros-<distro>-ros-base ros-<distro>-rclcpp ros-<distro>-sensor-msgs ros-<distro>-rosbag2-storage-default-plugins ros-<distro>-rosbag2-transport`
- 验证: `source /opt/ros/<distro>/setup.bash && ros2 topic list` 不报错
- 验收: `ros2 run demo_nodes_cpp talker` + 另一终端 `ros2 topic echo /chatter` 跑通；师兄回复以邮件 / 聊天记录附在 plan §execution-record

### M9b · `udex_to_xhand_msgs` 包 + msg schema（0.25d）
- 新建 `udex_to_xhand_msgs/` ament_cmake 子包（仓库根目录），独立于 `udex_to_xhand` 主包
- 定义 `HandDiagnostics.msg`:
  ```
  std_msgs/Header header
  float64[24] left_udcap_raw                   # l0-l23 原始度数
  float64[24] right_udcap_raw                  # r0-r23 原始度数
  builtin_interfaces/Time udcap_frame_stamp    # UDCAP JSON 内时间戳（若有，否则零）
  builtin_interfaces/Time receive_stamp        # 本机收到 UDP 时刻（steady_clock 起点 epoch 折算）
  builtin_interfaces/Time send_stamp           # send_command 调用前时刻
  float32 cycle_latency_ms                     # 本 tick UDP→send 总耗时
  uint8 safety_flags                           # bit0=stale frame, bit1=clamp triggered, bit2=parse error
  ```
- 5 个 topic 的 QoS profile 选 `rclcpp::SensorDataQoS()`（best_effort，depth=10）— 数据采集场景丢一帧可接受，绝不阻塞主循环
- 验收: `colcon build --packages-select udex_to_xhand_msgs` 干净通过；`ros2 interface show udex_to_xhand_msgs/msg/HandDiagnostics` 输出 schema 正确

### M9c · `udex_to_xhand` → ROS2 node（1d）
- 顶层 `CMakeLists.txt` 改造: 保留 `find_package(xhand_control HINTS xhand_control_sdk/share)`；新增 `find_package(rclcpp REQUIRED)` + `find_package(sensor_msgs REQUIRED)` + `find_package(udex_to_xhand_msgs REQUIRED)`；用 `ament_target_dependencies(udex_to_xhand rclcpp sensor_msgs udex_to_xhand_msgs)` 接通；保留原有 `nlohmann_json` / `yaml-cpp` / `libcurl` / `libssl` 链接
- 新增 `src/ros_publisher.{hpp,cpp}`: 持 5 个 `rclcpp::Publisher`（4 个 `JointState` + 1 个 `HandDiagnostics`），暴露 `publish_left_command(joints, stamp)` / `publish_left_state(joints, currents, stamp)` / `publish_right_*` / `publish_diagnostics(...)` API。内部封装 `rclcpp::Node`（名字 `udex_to_xhand_node`）
- `src/xhand_driver.{hpp,cpp}`: 新增 `HandState_t read_state()` 包装（封装 `xhand_control::XHandControl::read_state`），返回 12 个 position 弧度 + 12 个 current。ADR-044 范围内**仅作 observation 输出**，禁止被 `joint_mapper` 或安全层引用
- `src/main.cpp` 主循环 tick 顺序（M9 后定型）:
  1. `udcap_receiver.try_recv()` → raw 24 路 + `udcap_frame_stamp` + `receive_stamp`
  2. `joint_mapper.map()` → commanded 12 路（per hand）
  3. `xhand_driver.send_command()` → 记 `send_stamp`
  4. `xhand_driver.read_state()` → observed 12 路 + currents（双手各一次）
  5. `ros_publisher.publish_all(...)` → 4 JointState + 1 diagnostics
- CLI 新增 `--no-ros` flag，沿用 M5b 起的 standalone 模式（允许不带 ROS2 环境跑 — for snapshot test / `--actions` 预设 / regression 测试）
- 验收: `colcon build --symlink-install --packages-select udex_to_xhand`；`ros2 run udex_to_xhand udex_to_xhand --config config.yaml` 启动后另一终端 `ros2 topic hz /xhand/left/command` 显示 ≈100Hz；`ros2 topic echo /xhand/left/state` 应见 position[12]+effort[12]；`ros2 topic echo /xhand/diagnostics` 应见 raw_udcap + safety_flags + cycle_latency_ms

### M9d · Latency 回归（0.25d）
- 戴手套跑 30s 双手 teleop（UDCAP 在线），在 `--config config.yaml --hand both` 下打 `latency_ms{n avg p95 max}`（沿用 M5c / M7 的 vector+sort stats，ADR-033）
- 与 M7 单帧基线 `avg=19.38 p95=19.20 max=111.17` 对比，预算: read_state ×2（≈2-4ms RS485 同步读）+ ROS publish ×5（≈1-2ms in-process）→ M9 后 avg 估 22-26ms，p95 估 25-28ms，距 SPEC §9 50ms 阈值仍留 40%+ 余量
- 若 p95 > 40ms: 走 ADR-04x 降级到 state 30Hz 采样（访谈第 3 选项），主循环保持 100Hz 但 read_state 每 3 tick 才调一次；dataloader 侧 interpolate
- 验收: latency 报告 + 与 M7 baseline 对比表入 plan §execution-record；落日志归档 `docs/logs/m9d-latency-<date>.log`

### M9e · rosbag 录制 + 端到端验收（0.25d）
- 起独立 `ros2 bag record -o /var/log/udex_to_xhand/teleop-<ISO8601> /xhand/left/command /xhand/left/state /xhand/right/command /xhand/right/state /xhand/diagnostics`（`/var/log/udex_to_xhand/` 提前创建 + `chown` 当前用户）
- 跑一段 60s 双手 teleop，含 M8 抓杯流程（XHand 装在 G1 末端 → 抓桌上杯子 → 保持 3s → 放下）
- bag 落盘验证:
  - `ros2 bag info <path>` 输出 5 个 topic，各 ≈ 6000 messages（100Hz × 60s），无 dropped messages 警告
  - `ros2 bag play <path>` + `ros2 topic echo /xhand/left/command` 可正确重放
  - 抽 1 帧用 `ros2 bag info --verbose` 看 `header.stamp` 与 `receive_stamp` 字段，确认同帧 `/xhand/*/command` + `/xhand/*/state` 的 stamp 一致
- 师兄侧 dataloader 试加载: bag 拷给师兄，验证他的 BC 训练 pipeline 能识别 `sensor_msgs/JointState` 并读出 `(left.command, left.state, right.command, right.state)` 四元组，且与机器人侧 bag 的 robot action 在同一 `/clock` 时间轴上无明显错位（visually compare timeline in `rqt_bag` 或写一个 ros2 python script 抽 sample）

**完成定义**:
- `ros2 bag info teleop-<...>` 显示 5 topic × ≈6000 msg，时间跨度 ≈60s，无 dropped messages
- bag 内 `/xhand/*/command` 与 `/xhand/*/state` 在同一 tick 的 `header.stamp` 内成对出现（offline script 抽样验证）
- 60s session 内含一次成功抓杯（M8 acceptance test 的子集，杯子离桌 ≥3s）
- latency p95 < 30ms（M7 baseline + ROS publish + read_state 后）；avg / p95 / max 入 ADR-04x 记录基线
- 师兄确认能用他的 dataloader 加载本 bag 与机器人 bag、双方 action sequence 在同一 `/clock` 时间轴上无明显错位（书面确认入 plan §execution-record）

```bash
# M9a — ROS2 安装 + verify（distro 占位为 humble，待 M9a 确认后替换）
sudo apt install ros-humble-ros-base ros-humble-rclcpp ros-humble-sensor-msgs \
                 ros-humble-rosbag2-storage-default-plugins ros-humble-rosbag2-transport
source /opt/ros/humble/setup.bash
ros2 run demo_nodes_cpp talker &
ros2 topic echo /chatter   # 应能 echo

# M9b — msg 包
colcon build --packages-select udex_to_xhand_msgs
source install/setup.bash
ros2 interface show udex_to_xhand_msgs/msg/HandDiagnostics

# M9c — udex_to_xhand 作为 ROS2 node
colcon build --packages-select udex_to_xhand
ros2 run udex_to_xhand udex_to_xhand --config config.yaml
# 另一终端
ros2 topic hz /xhand/left/command      # 期望 ~100Hz
ros2 topic echo /xhand/left/state      # 应见 12 position + 12 effort
ros2 topic echo /xhand/diagnostics

# M9e — 录制 + 抓杯验收
mkdir -p /var/log/udex_to_xhand
ros2 bag record -o /var/log/udex_to_xhand/teleop-$(date -Iseconds) \
    /xhand/left/command /xhand/left/state \
    /xhand/right/command /xhand/right/state \
    /xhand/diagnostics &
ros2 run udex_to_xhand udex_to_xhand --config config.yaml --duration 60
# 戴手套抓杯
kill %1   # 停 bag
ros2 bag info /var/log/udex_to_xhand/teleop-<latest>
```

**依赖**: M7（双手集成已通过）、M8（抓杯 acceptance — 是本 milestone 录制场景的内容）、ROS2 已装在 PC2

**ADRs**:
- ADR-043: relax CLAUDE.md «Do NOT use ROS2» — 仅限 M9 数据记录路径（rclcpp + sensor_msgs + rosbag2），不含 ros2_control / ros2_controllers / lifecycle_node 等控制框架
- ADR-044: relax CLAUDE.md «Do NOT read XHand sensors / force feedback» — 仅限 offline observation publish，`read_state` 结果只进 ROS topic，禁止被 `joint_mapper` / `safety` / `main` 控制路径引用
- ADR-045（M9a 后落）: ROS2 distro 选型 + `/clock` 同步策略（与机器人侧 sim_time 对齐 vs PC2 wall clock + QoS 选择理由）
- ADR-046（M9b 后落）: rosbag2 storage backend 选型（sqlite3 default vs mcap）
- ADR-047（M9d 后落，条件性）: 若 latency p95 > 30ms → state 降频策略 + dataloader 侧 interpolation 约定

**Post-M9 文档同步（落 M9 ✅ 时一并完成）**:
- CLAUDE.md «Constraints — do NOT» 段两条（read XHand sensors / use ROS2）改为「除 M9 数据记录路径外不得使用」并 link ADR-043/044
- CLAUDE.md «Architecture» 段加 ROS2 publish 数据流分支 + `udex_to_xhand_msgs/` 包说明
- SPEC.md 增 §13「数据采集 schema」章节，落 topic 列表 / msg 结构 / bag 命名规约 / `/clock` 约定
- README 增加 ROS2 build + run 流程（`colcon build` 取代 `cmake .. && make`） — 注：完整 README 重写见 M10

---

## M10: 中文 README（项目交付）

**目标**: 在 M9 完成、整套数据流水线跑通后，写一份**中文、清晰、简洁**的 README，让一个**没参与开发的新人**能照着 README 完成「准备环境 → 接好硬件 → 编译 → 跑通双手遥操 → 录一段训练数据」全流程，并能在出常见故障时自行定位。这是项目对外交付物，不是开发者笔记。

**为什么单独立 milestone**:
- 现有 `CLAUDE.md` / `SPEC.md` / `docs/plans/*` / `docs/decisions/*` 都是面向**开发者 + Claude**的内部文档：信息密度高、术语多、按 milestone 时间线组织 — 不是给新人入门用的
- M9 完成时项目结构会有较大变动（cmake → ament_cmake / colcon、新增 ROS2 依赖、新增 `udex_to_xhand_msgs/` 包、新增 `--no-ros` flag），任何更早写的 README 都会过时
- README 的好坏决定「这份代码库交出去后还能不能被人复用」，是项目工程性收尾的一环

**内容**:

### M10a · 信息架构（0.1d）
按以下结构定 README 章节，先列骨架与每节字数预算（总长 ≤ 800 行 / 中文 ≤ 1.5w 字）：
1. **项目简介** — 一段话讲清楚做什么、给谁用、不做什么（拒做清单引用 SPEC §约束）
2. **架构总览** — 一张 ASCII 数据流图（Windows UDCAP → UDP → PC2 ROS2 node → XHand + rosbag2）+ 模块一句话说明
3. **前置准备** — 硬件清单（UDCAP 手套、两只 XHand、G1 PC2、网络拓扑）、操作系统、ROS2 distro、`/dev/ttyACM*` 权限
4. **构建** — 系统依赖安装（apt 一行）、`colcon build` 全流程、常见编译错误
5. **配置** — `config.yaml` 关键字段说明（左右手 serial port、mapping 不动、PID 默认值、watchdog 超时、ROS topic 命名）
6. **运行 — 单手** — `ros2 run` 启动单手 + UDCAP 连接验证 + 视觉检验清单（5 指 + 握拳 + 张开）
7. **运行 — 双手** — 双口枚举确认（ADR-042 教训：每次会话 re-probe）、双手 teleop、抓杯流程
8. **数据采集** — `ros2 bag record` 完整命令、bag 路径规约、bag 读取脚本示例（Python `rosbag2_py` 读出 (state, action) pair）
9. **安全** — Ctrl+C / SIGTERM 行为、watchdog 触发现象、clamp 触发现象、紧急停手
10. **常见故障** — `permission denied /dev/ttyACM0`、UDP 收不到包、CRC 启动告警、双手 latency 异常、bag 写不进、师兄侧 dataloader 加载失败
11. **二次开发指引** — 想改 mapping 改哪里、想加新 ROS topic 改哪里、SPEC.md / CLAUDE.md / ADR 体系怎么用、PR 流程
12. **附录** — 关键 ADR 索引、log 归档路径约定、glossary（UDCAP / XHand / RS485 / CDC-ACM / `/clock`）

### M10b · 内容编写（0.3d）
- 每节按 §M10a 骨架落字。强制要求:
  - **可复制即可运行**：所有命令必须能直接复制到 PC2 zsh 跑通，不留 `<placeholder>` 让读者猜
  - **示例输出贴近实测**：每个命令配一段真实截取的预期 stdout（取自 `docs/logs/m*-*.log`），不写理想化伪输出
  - **链 ADR 不复述 ADR**：决策细节链到 `docs/decisions/`，README 正文只讲「what / how to use」不讲「why we chose this」
  - **故障定位走「症状 → 检查命令 → 修复」三段式**，不写发散建议
- 截图 / 示意图: 至少一张 ASCII 架构图（在 §2）+ 一张 hardware 接线示意（PC2 USB 端口 / RS485 / UDCAP 网络）— ASCII 即可，不引入图床

### M10c · 冷启动验证（0.1d）
- 找一个**没参与开发的人**（同实验室同学 / 师兄 / 老师都行），让他/她从 git clone 开始照 README 跑：
  - 全程不允许问作者，遇到问题只能查 README + ADR + log
  - 目标动作：完成 M9 §M9e 的「60s 双手 teleop + 抓杯 + bag 落盘」
- 跑通后采访: 哪一节最容易卡住？哪个命令的 expected output 与实际不符？哪个故障没在 §10 覆盖？
- 回写 README 修复，至少覆盖被卡 ≥ 2 次的问题

**完成定义**:
- README.md 在仓库根目录，中文，章节按 M10a 骨架完整
- 冷启动测试人独立从 clone 走到双手 teleop + bag 落盘成功，过程中不需要追问作者
- README 内每个 bash 块都至少在一台 PC2 上实测过一次，不留死命令
- 链接（到 ADR、SPEC、plan、log）全部有效（`grep -E '\]\([^)]+\)' README.md` 可一键校验）
- 与 CLAUDE.md / SPEC.md 不冲突（不重复信息，只在 README 提概念 + 链过去看权威定义）

```bash
# M10c 冷启动验证脚本（验证人执行）
cd /tmp && rm -rf udex_to_xhand
git clone <repo-url> && cd udex_to_xhand
# 从这里开始只能照 README 操作，遇到不懂的不能问作者
# 验收: 60s 后 ros2 bag info /var/log/udex_to_xhand/teleop-* 输出 5 topic × ~6000 msg
```

**依赖**: M9（ROS2 数据流水线已落地）— README 的「数据采集」章节内容来源于 M9e 的实测命令；任何早于 M9 的 README 写法都会被 M9 改造作废

**ADRs**: 无（文档工作不引入新架构决策；如冷启动发现 ADR 表述模糊需 backlink 修复，单独 PR）

**Post-M10**:
- 项目整体交付完成
- CLAUDE.md 末尾加一行 `面向新人的入门文档见 README.md（不是 CLAUDE.md）`
- 删除 / 归档过期的开发笔记（如有）

---

## 依赖图

```
M0 (skeleton)
├── M1 (UDP receiver)
│   └── M2 (param verify)
├── M3 (XHand driver)
│
└── M4 (single-hand teleop) ← M1 + M2 + M3
    └── M5 (C++ rewrite + port to G1 PC2)   ADR-023
        ├── M5a (vendor SDK build on PC2)
        ├── M5b (project C++ port: main/receiver/mapper/driver/safety)
        └── M5c (M3 + M4 re-validation on PC2)
            └── M6 (safety hardening, C++)
            └── M7 (dual hand)
                └── M8 (tuning + acceptance)
                    └── M9 (ROS2 data logging)   ADR-043/044
                        ├── M9a (ROS2 setup + senior alignment)
                        ├── M9b (udex_to_xhand_msgs package)
                        ├── M9c (udex_to_xhand → ROS2 node)
                        ├── M9d (latency regression)
                        └── M9e (rosbag record + cup-pickup acceptance)
                            └── M10 (Chinese README, project handoff)
                                ├── M10a (info architecture)
                                ├── M10b (content writing)
                                └── M10c (cold-start validation)
```

## 时间估算

| Milestone              | 估算 | 累计 |
| ---------------------- | ---- | ---- |
| M0 Skeleton            | 0.5d | 0.5d |
| M1 UDP Receiver        | 0.5d | 1d   |
| M2 Param Verify        | 0.5d | 1.5d |
| M3 XHand Driver        | 0.5d | 2d   |
| M4 Single-hand Teleop  | 1d   | 3d   |
| M5 C++ Port to G1 PC2  | 2d   | 5d   |
| M6 Safety (C++)        | 0.5d | 5.5d |
| M7 Dual Hand           | 0.5d | 6d   |
| M8 Tuning + Acceptance | 1d   | 7d   |
| M9 ROS2 Data Logging   | 2d   | 9d   |
| M10 Chinese README     | 0.5d | 9.5d |

M1 和 M3 无依赖关系，可并行。最快路径: **9.5 天** (M5 因厂商仅提供 C++ SDK 拆 M5a/b/c 工期 2d；M9 因引入 ROS2 + msg 包 + CMake 改造 + latency 回归 + 录制验收拆 M9a-e 工期 2d；M10 中文 README 含冷启动验证 0.5d)。