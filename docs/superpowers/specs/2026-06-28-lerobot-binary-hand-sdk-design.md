# LeRobot → XHand 二值手部控制 SDK — 设计

状态：已批准（2026-06-28）。**本文档不 commit**（用户要求）。

## 目标

为现有 UDCAP→XHand retarget 系统增加**第二条、完全独立**的输入通路：接入 LeRobot
格式的 action 数据（以及未来 VLA 实时生成的结果），仅取其中两维手部开/合二值
（`action[36]=left_hand_closed`、`action[37]=right_hand_closed`），驱动两只 XHand
做出抓取（握合）/张开动作。

非目标：不处理 38 维中其余维度（base/腿/腰/臂），不读触觉/深度/雷达，不做 retarget。

## 关键约束（来自 CLAUDE.md + 用户）

- 纯 C++，运行在 G1 PC2 (aarch64 Linux)，实时（非离线预处理）。
- 不动现有任何文件的现有逻辑；不 commit。唯一对现有文件的改动 = 在
  `CMakeLists.txt` 末尾**追加**一个新 `add_executable`（不改现有 target）。
- 实时控制路径不得引入 Python / FFI（VLA 作为独立进程经 UDP 发包，等同 UDCAP 模型）。
- 下发前必须 clamp 到关节限位；启动需先发现手别；Ctrl+C → mode=0 + close。

## 架构

```
VLA / 推流进程 (任意语言)
  └─ UDP JSON  {"action":[...38...]}   端口默认 9100（与 UDCAP 9000 分开）
         ↓
新增二进制 lerobot_to_xhand
  ├─ src/lerobot_receiver.{hpp,cpp}        非阻塞 UDP + JSON 解析，取 action[36]/[37]→bool
  ├─ src/lerobot_hand_controller.{hpp,cpp} 核心 SDK：二值→姿态、相位平滑、clamp、下发
  └─ src/lerobot_hand_main.cpp             控制循环 + CLI + 启动门 + 看门狗 + 优雅退出
```

现有 `udex_to_xhand`、`src/*` 现有文件、`config.yaml` 全部不动。

## 复用（不复制、不改写）

- `XHandDriver`（open / has_left / has_right / send_left / send_right / shutdown）+ `XHandPID`
- `safety::clamp_in_place` / `safety::Watchdog` / `safety::install_signal_handlers`
- `preset_actions` 的 `fist`（握合）/ `palm`（张开）+ `deg_to_rad`
- `nlohmann_json`；UDP 收包写法照搬 `udcap_receiver.cpp`（drain-to-latest、解析失败跳帧）

## 组件契约

### lerobot_receiver
- `struct LerobotHandFrame { bool left_closed; bool right_closed; time_point recv_ts; }`
- `parse_lerobot_payload(json, threshold=0.5) -> optional<LerobotHandFrame>`
  - 主路径：对象含 `"action"` 数组且长度 ≥ 38 → `left=action[36]>thr`、`right=action[37]>thr`。
  - 兼容路径：无 `action` 但含顶层 `left_closed`/`right_closed` 数值 → 直接用（便于简单生产者/测试）。
  - 任意结构不符 → `nullopt`（绝不下发损坏数据）。
- `class LerobotReceiver`：`(bind_host, port)`；`try_recv()` drain 到最新包后解析；
  `last_sender_ip()`、`parse_errors()`。与 `UdcapReceiver` 同形。

### lerobot_hand_controller（核心 SDK，transport-agnostic）
- `LerobotControllerConfig { open_pose_deg, close_pose_deg, transition_ms=400, tick_period_ms, debounce_frames=0 }`
- `LerobotHandController(XHandDriver* left, XHandDriver* right, cfg)`
  - 构造时把 open/close 姿态 `deg→rad` 并 `clamp_in_place`。
  - 每只手维护 `phase∈[0,1]`（0=张开，1=握合），`phase_step = transition_ms<=0 ? 1 : tick_period_ms/transition_ms`。
- `set_target(bool left_closed, bool right_closed)`：更新目标（带可选 debounce）。
- `tick()`：每只手把 `phase` 朝目标推进一步 → `pose = lerp(open,close,phase)` → clamp → `send_*`；
  缓存最近下发姿态。**每 tick 都重发当前姿态**（保活 RS485 setpoint，看门狗 hold 自动满足）。
- `hold()`：重发上次姿态（供主循环在 stale 时显式调用；与逐 tick 重发等价，保留为显式 API）。
- 初始 `phase=0` → 启动即张开姿态（安全）。仅向非空指针的手下发。

### lerobot_hand_main
- 极简自带 CLI（不碰 cli.cpp）：`--config`(读现有 `xhand:` 段) `--udp-port`(默认9100)
  `--hand left|right|both` `--rate`(默认100Hz) `--transition-ms`(默认400)
  `--open-pose/--close-pose`(默认 palm/fist) `--debounce-frames`(默认0)
  `--duration`(0=直到信号) `--dry-run`(不开串口，仅解析并打印决策) `--help`
- 流程：读 config 的 xhand 段 → 按 `--hand` open driver(s) + L/R 防插反校验 →
  装信号处理 → 建 receiver → 等第一帧有效 UDP（dry-run 跳过）→ 建 controller →
  循环 `--rate`：`try_recv` 命中则 `set_target`+`wdog.update`；每 tick `controller.tick()`；
  stale 时 1Hz WARN（姿态由 tick 持续重发保持）→ 退出时 `driver.shutdown()`。

## 安全

- 启动姿态 = 张开（palm，phase=0）。
- `XHandDriver::open()` 完成手别发现 + L/R 防插反；等首帧有效 UDP 才进入跟随。
- 看门狗 >200ms 无 UDP → 保持上次姿态（tick 重发）。
- 下发前 `safety::clamp_in_place`。
- SIGINT/SIGTERM → mode=0 + close_device。
- LeRobot 流无 UDCAP `CalibrationStatus` 字段 → 该 UDCAP 专属门不适用于本输入源；其余安全门保留。

## 配套（新增、离线、不进实时回路）

- `scripts/lerobot_udp_test_sender.py`：周期翻转 36/37 发 `{"action":[..38..]}`，
  供 PC2 上 `--dry-run` 冒烟测试 UDP 链路（纯离线 dev 辅助）。

## 构建（PC2）

```bash
mkdir -p build && cd build && cmake .. && make lerobot_to_xhand
./lerobot_to_xhand --config ../config.yaml --udp-port 9100            # 双手
./lerobot_to_xhand --config ../config.yaml --dry-run                  # 无硬件验证链路
```
```

## 默认值（已与用户确认）

- 控制环 100Hz、过渡 400ms。
- 握合 = `fist` 预设，张开 = `palm` 预设。
