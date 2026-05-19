# Roadmap: UDCAP → XHand Teleoperation

## Revision History

- 2026-05-16 — 第 1 次修订 — 部署主机切换到宇树 G1 PC2 (aarch64)；新增 M5 (Port to G1 PC2)，原 M5/M6/M7 顺延为 M6/M7/M8。详见 [ADR-023](../decisions/023-roadmap-pivot-g1-pc2-as-host.md).
- 2026-05-16 — 第 2 次修订 — 发现厂商提供的是 `xhand_control_sdk/` (aarch64 **C++** SDK + headers + .so)，**没有 Python wheel**。M5 阻塞解除；决定弃用 Python 算法栈，**在 PC2 上把 M1/M3/M4 的 Python 模块整体重写为单一 C++ 二进制**，避免 Python↔C++ FFI 跨语言开销，并匹配仓库内 `xhand_control_ros2.hpp` 的语言基线。`config.yaml`/ADRs/example.json 等已验证资产保留。M5 拆为 M5a/M5b/M5c，工期估算 0.5d → 2d。CLAUDE.md / SPEC.md 中的 Python 基线需在 M5 完成后同步更新。
- 2026-05-18 — 第 3 次修订 — M5a 在 G1 PC2 上执行通过（左手，joint 4 ±0.1 rad，SDK 1.4.3，hand_id=1，serial 012L320220250728005）。新增 ADR-024 / 025 / 026 / 027 记录 M5a 非显然决定（vendor 示例作 bring-up harness / 厂商源不入库 / 沿用厂商 PID kp=225 / joint 4 作 smoke joint）。Execution Record 见 [plan §6](./20260516-m5a-vendor-sdk-pc2-bringup-plan.md#6-execution-record--filled-2026-05-18)，日志归档 `docs/logs/m5a-test-serial-2026-05-18.log`。
- 2026-05-18 — 第 4 次修订 — M5b 在 G1 PC2 上执行通过（C++17 单二进制 `udex_to_xhand` 落地；mock 300/300 ticks；snapshot test L max |Δ|=0.0e+00 rad / R max |Δ|=1.4e-17 rad，远低于 1e-6 tolerance；receiver-only @ UDCAP 192.168.3.24 收 616/1000 包 0 parse errors；`-Wall -Wextra -Wpedantic` 无警告）。新增 ADR-028 / 029 / 030 / 031（纯 C++ 重写 / CalibrationStatus pre-check 拆 UDCAP+XHand 两侧 / snapshot fixture 用 SHA-256 自校验 / `legacy_python/` 提前到 M5b 重组）。`legacy_python/` 创建于本 milestone 作为 Python 原型的归集点。Execution Record 见 [plan §11](./20260518-m5b-cpp-rewrite-plan.md#11-6-execution-record-filled-at-end-of-m5b)，日志归档 `docs/logs/m5b-{cmake,make,mock-run,snapshot-test,receiver}-2026-05-18.log`。
- 2026-05-19 — 第 6 次修订 — M8 范围澄清：明确拇指重定向不能沿用 copy-rotation —— XHand 拇指零位与其余四指掌面近似正交，与 UDCAP 拇指零点不同源，逐关节直传会错配对掌；升级为独立的 retargeting 算法工作项，与四指 mapping 解耦。原"微调 mapping"一条拆分为「拇指重定向算法重做」+「四指 mapping 微调」两条；算法形态留到 M8 调研后定，决策走新 ADR。
- 2026-05-18 — 第 5 次修订 — **M5c 在 G1 PC2 上真实硬件复测通过；M5 整体 ✅**。`--actions fist,palm,v,ok`（M3 等价物）4 个动作全部物理观察通过（hand_id=1 type=Left，SDK 1.4.3；2 次 startup CRC，ADR-017 log-not-crash 覆盖）。`--config ../config.yaml --hand left --duration 30`（M4 等价物）戴 UDCAP 手套单手实时跟随：5 项手指 + 握拳 + 张开目检通过，1773 valid frames / 3000 ticks / 0 parse errors，**latency_ms{n=1773 avg=9.60 p95=9.62 max=10.68}** —— 远低于 SPEC §9 phase 3.9 的 50ms 阈值（avg ≈ 19% ceiling）。SIGTERM → mode=0 + close_device 视觉验证通过（log 因 plan §6.6 `kill -TERM $!→tee` 截断，已 post-run 修复）。新增 ADR-032 / 033 / 034（preset 表 header-only + Python 字节一致脚本 / latency stats 用 vector+sort 而非 streaming / `--actions` 模式 tolerate 缺失 config.yaml）。§6.2 LOCAL→PC2 sync + §6.10 log pull 因 PC2 sshd 预 banner 拒连未走 rsync，改用 git push/pull + 手工 log 复制；§6.7 网络专用 log 未产，UDP 通路由 §6.8 teleop log 中 "first packet from 192.168.3.24" + 0 parse errors 反证。Execution Record 见 [plan §11](./20260518-m5c-pc2-hardware-revalidation-plan.md#11-execution-record-filled-2026-05-18)，日志归档 `docs/logs/m5c-{cmake,make,ttyacm,actions,sigterm,teleop-left}-2026-05-18.log`（6 个）。

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

## M6: Safety Layer (C++)

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

---

## M7: 双手集成 (C++)

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
- [ ] 只动左手 → 只有左 XHand 动，右 XHand 不动
- [ ] 只动右手 → 同理
- [ ] 双手同时握拳 → 双 XHand 同时握拳
- [ ] 左右手做不同动作 → 各自正确

**依赖**: M6, 第二只 XHand 硬件

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

M1 和 M3 无依赖关系，可并行。最快路径: **7 天** (M5 因厂商仅提供 C++ SDK，含 M5a 原生验证 / M5b 项目 C++ 化 / M5c PC2 验收 三个子阶段，工期从 0.5d 升到 2d)。