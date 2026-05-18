# M5c — Plan: PC2 上重跑 M3 + M4 验收（C++ `udex_to_xhand` 硬件复测）

- Milestone: M5c
- Date: 2026-05-18
- Estimate: 0.5d
- Status: Plan
- Predecessors: M5a ✅（厂商 SDK 在 PC2 上验过；ADRs 024-027）、M5b ✅（C++17 `udex_to_xhand` 二进制落地；mock / snapshot / receiver-only 全通过；ADRs 028-031）
- Deliverable on success: C++ 二进制在 G1 PC2 真实硬件上跑通 (1) 4 个预设动作（M3 等价物）和 (2) 单手实时遥操（M4 等价物）；End-to-end latency 满足 SPEC §9 phase 3.9 「<50ms」并且不退化于 Python M4 dev-PC 基线 (±5ms)。
- Operator note: 本计划作者当前**不在** G1 PC2 前。所有命令以 `[LOCAL]`（开发 Mac，x86_64 Darwin）/ `[PC2]`（G1 PC2，aarch64 Ubuntu）/ `[WIN]`（Windows，UDCAP HandDriver 宿主）三种标签明确区分。

---

## 0. Why this milestone exists（和 M5a / M5b 的边界）

M5b 已经把 Python 算法栈整体改写成 C++17 单二进制 `udex_to_xhand`，并完成了三类**非硬件**验收：

- `--mock` smoke：300 ticks 全产出 12+12 关节值（algorithm wiring OK）
- snapshot 等价测试：Python ↔ C++ ‖Δ‖∞ = 0.0 / 1.4e-17 rad（algorithm 位级一致）
- `--receiver-only`：UDCAP @ 192.168.3.24 在线 10s 收 616/1000 包（UDP/JSON 解析 + calib gating OK）

但 M5b 主动**没碰**两件事，roadmap §M5c 把它们留给本 milestone：

1. **真实 XHand 硬件**。M5b 没有让 C++ 通过 RS485 向 XHand 写命令；ADR-029 的 XHand 端启动检查（list_hands_id + get_hand_type）只在编译里。SDK error_code、CRC、电机响应延迟全部未观察。
2. **--actions 预设动作（M3 等价物）**。M5b 期间 `cli::Args::actions` 字段已留好，但 `main.cpp:118-123` 直接 `return 2;`，错误信息明示「M5c scope; not implemented in M5b」。M3 验收（fist / palm / V / OK）的 C++ 化是 M5c 的硬性任务。

M5c 是 M5（"Port to G1 PC2 (C++ 重写底层栈)"）的**收口**。M5c 通过后，M5 整体可以视为完成，M6（安全强化）才有意义启动。

### 0.1 In-scope（M5c 必须做）

- (a) 在 `src/main.cpp` 实施 `--actions fist,palm,v,ok` 模式（不走 UDP，不走 mapper，只走 driver；逐个 preset 发送 12 关节角度，hold 默认 1.0s）。预设角度表与 `legacy_python/xhand_driver.py:9-14` 严格一致（4 个动作 × 12 度数）。
- (b) 在控制循环里加一段轻量 latency 统计（min / avg / p50 / p95 / max）覆盖 `frame.recv_ts → just after send_command return`；退出时打印一次摘要。**不**改变控制循环节奏。
- (c) 在 PC2 真实硬件上跑通：
    - M3 等价物：`./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok`，左手依次执行四个动作（与 M3 Python 验收同样的可见效果）
    - M4 等价物：`./udex_to_xhand --config ../config.yaml --hand left`，戴 UDCAP 手套，左手 XHand 实时跟随；目检 5 项验收清单（拇指 / 食指 / 中指 / 无名指 / 小指 各自独立、握拳、张开）全部通过
    - 进程退出后摘要中 avg / p95 latency 数值都 < 50ms
- (d) 验证 SIGTERM 触发 mode=0 + close_device（SPEC §5 + ADR-023）。M5b 编译进去了但没单独验。M6 大动手术之前，这条「手刹」必须可用。
- (e) 完成路线图明确列出的两项基础设施前置：(d-i) `/dev/ttyACM*` 在 PC2 上稳定枚举；(d-ii) Windows UDCAP → G1 PC2 网络通路（静态 IP / 防火墙 / 有线优先）。

### 0.2 Out-of-scope（明确不在 M5c 里做）

| 项目                                              | 归属    | 原因                                                                                  |
| ------------------------------------------------- | ------- | ------------------------------------------------------------------------------------- |
| Watchdog 真实故障注入（>200ms 无 UDP 保持位置）   | **M6**  | roadmap §M6 测试 1。M5b 已经接好 `safety::Watchdog::update()` 但没断网验证。M5c 不验。 |
| Clamp 故障注入（人为收窄 clamp 看是否生效）       | **M6**  | roadmap §M6 测试 3。同上，钳位逻辑在但未单独故障注入。                                |
| 双手并行（右手 XHand 接入）                       | **M7**  | roadmap §M7 全部范围；右手硬件准备情况未知，且涉及 PC2 USB 端口预算（ADR-023 风险 8）。 |
| 右手 mapping 符号校正                             | **M7**  | SPEC §3.1：右手 sign 由 M7 验证。M5c 即便顺手接了右手也不会改 config.yaml。            |
| PID 调优 / 低通滤波                               | **M8**  | roadmap §M8。当前 kp=100 来自 SPEC §3.2 默认；M5c 不动。                              |
| 抓杯子验收                                        | **M8**  | M8 才是验收。M5c 只验「实时跟随方向正确，无明显延迟」。                              |
| 在 C++ 里恢复 Python 解释器或任何 FFI            | **永不**| ADR-028 + memory `feedback_no_unnecessary_ffi.md`。                                  |
| 删除 `legacy_python/`                             | 候选    | ADR-031 写明 post-M5c 候选。但 `joint_mapper.py` 仍是 snapshot oracle，不能删；M5c 不动。 |

### 0.3 Acceptance (DoD-level, 单行)

- C++ 二进制在 PC2 上完成 M3 行为（4 preset 顺序执行成功）+ M4 行为（戴手套左手跟随通过 5 项目检清单）+ Ctrl+C 与 SIGTERM 都触发 mode=0 + close_device。Latency 摘要 avg < 50ms 且 p95 < 50ms。

---

## 1. 文件清单

### 1.1 新增 — committed by M5c

| Path                                                                          | One-line responsibility                                                                                                   |
| ----------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| `src/preset_actions.hpp`                                                      | `constexpr std::array` 持有 4 个预设动作的 12 关节角度（度），与 `legacy_python/xhand_driver.py:9-14` 字节一致；附 `deg_to_rad()` 辅助。 |
| `docs/decisions/032-*.md` (TBD post-run)                                      | M5c 非显然决定的 ADR（候选见 §10）。题名 + 数量在执行后定。                                                              |
| `docs/logs/m5c-actions-2026-05-18.log`                                        | `[PC2] ./udex_to_xhand --actions fist,palm,v,ok` 输出存档。                                                              |
| `docs/logs/m5c-teleop-left-2026-05-18.log`                                    | `[PC2] ./udex_to_xhand --config ../config.yaml --hand left` 30s 跟随会话输出。                                            |
| `docs/logs/m5c-sigterm-2026-05-18.log`                                        | SIGTERM 触发 graceful shutdown 的会话输出。                                                                              |
| `docs/logs/m5c-ttyacm-2026-05-18.log`                                         | `dmesg` + `udevadm info` + `ls -l /dev/ttyACM*` 输出（serial enum 证据）。                                              |
| `docs/logs/m5c-network-2026-05-18.log`                                        | `ip a` + `ping -c 5 <win-host>` + `ss -ulpn 'sport = :9000'` 输出（网络通路证据）。                                       |

### 1.2 修改 — committed by M5c

| Path                          | Change                                                                                                                                                                                                          |
| ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/main.cpp`                | (a) 移除 `if (args.actions) { return 2; }` 占位（行 118-123），插入 `run_actions(...)` 分支（详见 §3.1）。(b) 在 FULL/MOCK 模式里新增 latency stats accumulator（§3.2）。(c) `args.actions` 与 `--mock` / `--receiver-only` 互斥，与 `--hand` 兼容（默认全手）。 |
| `src/cli.cpp`                 | (a) Usage 行 "— M5c scope" 字样去掉。(b) 新增 `--hold <sec>` 解析（默认 1.0），仅 actions 模式生效。(c) actions 与 mock/receiver-only 的互斥校验。                                                            |
| `src/cli.hpp`                 | (a) `Args::actions` 注释更新（不再标 M5c scope）。(b) `Args::hold{1.0};`。                                                                                                                                       |
| `CMakeLists.txt`              | （可能）`preset_actions.hpp` 不需要 `.cpp`，所以**不动**；如果 §3.1 决定把表移到 .cpp 再说。                                                                                                                  |
| `docs/plans/00-roadmap.md`    | (a) 顶部 Revision History 加第 5 条「M5c 执行记录」。(b) `## M5: Port to G1 PC2` 标题加 ✅。(c) M5c 章节末尾 "结果" 行（沿用 M5a/M5b 的尾行风格）。(d) `**ADRs**:` 行追加新 ADR 编号。                |
| `docs/plans/20260518-m5c-pc2-hardware-revalidation-plan.md` | §11 Execution Record 在跑完后填回此文件本身（先留 placeholder）。                                                                                                                                                |
| `CLAUDE.md`                   | （可能不动）当前 "Commands" 段已经写了 `./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok`；M5c 把这条命令变为真实可用，无需改文本。如果 §3.1 决定增加 `--hold` 默认值要在示例里体现，再加一行。  |
| `SPEC.md`                     | §9 phase 3.9 "Target: <50ms" 旁标注「M5c 实测 avg=…ms / p95=…ms」（Execution Record 决定具体数值）。其他 §不动。                                                                                                |

### 1.3 不修改 (read-only inputs for M5c)

- `src/udcap_receiver.{hpp,cpp}` — M5b §6.7 已在 PC2 验证；M5c 仅读 `frame.recv_ts` 用于 latency 统计，不改 receiver 本身。
- `src/joint_mapper.{hpp,cpp}` — M5b snapshot 测试通过；位级一致；不碰。
- `src/xhand_driver.{hpp,cpp}` — `send_left` / `send_right` / `shutdown` 接口齐全；actions 模式直接复用，不改驱动。
- `src/safety.{hpp,cpp}` — clamp + 信号处理 + watchdog 接口齐全；SIGTERM 验证只是**触发**已有路径，不改实现。
- `src/logging.hpp` — 复用 `LOG_INFO/WARN/ERROR` 宏。
- `xhand_control_sdk/` — vendor，ADR-025 明令不入库改动。
- `tests/test_mapper_snapshot.cpp` + `tests/fixtures/mapper_baseline.json` — algorithm parity 由 M5b 锁定，M5c 不重做。
- `config.yaml` — mapping 数据照 M2/M4 保持；如果 M5c 跟随观感有强烈反向需求才改，但**不预期**改（M2/M4 已经在 dev PC 上验过左手方向正确，C++ 算法位级一致）。

---

## 2. 数据流

### 2.1 Actions 模式（M3 等价物，新增）

```
[PC2 终端]
    ./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok [--hold 1.0]
        │
        ▼
    cli::parse → Args{ actions="fist,palm,v,ok", port_override="/dev/ttyACM0", hold=1.0 }
        │
        ▼
    main.cpp::run_actions(args, xhand_cfg)
        │
        ├──► XHandDriver driver(port, baud, pid); driver.open()
        │       │
        │       │  XHandControl::open_serial → list_hands_id → get_hand_type
        │       │      → fill hand_id_left_ / hand_id_right_
        │       ▼
        │   LOG: "hand_id=N type=Left" / "hand_id=M type=Right"
        │
        ├──► for name in {fist, palm, v, ok}:
        │       std::array<double,12> deg = preset_actions::PRESETS[name];
        │       std::array<double,12> rad = preset_actions::deg_to_rad(deg);
        │       safety::clamp_in_place(rad)   ← 二层钳位 (ADR-021 + 一致性)
        │       if (driver.has_left())  driver.send_left(rad);
        │       if (driver.has_right()) driver.send_right(rad);
        │       LOG: "Action <name>: sent 12 joints, OK"
        │       sleep_for(args.hold seconds)
        │
        ├──► driver.shutdown()    ← mode=0 + close_device
        │
        ▼
    exit 0
```

**为什么 actions 也走 `safety::clamp_in_place`**：与 FULL 模式保持同一条「driver 之前必钳位」的不变式，避免 preset 写错时直接打硬件极限（ADR-021 的二层防御思想）。preset 的官方值在 hard limits 内时这步是 no-op。

### 2.2 Teleop 模式（M4 等价物，原 M5b 已实现）+ 新增 latency 统计

```
   UDCAP HandDriver (Win)
        │ UDP/JSON :9000
        ▼
   UdcapReceiver::try_recv()  →  Frame{ l[24], r[24], calib_L, calib_R, recv_ts }
        │                                                              │
        │                                                              │   ★ M5c 新增
        │                                                              │   t0 = frame.recv_ts
        ▼
   JointMapper::map_left / map_right  →  std::array<double,12> rad
        │
        ▼
   safety::clamp_in_place(rad)
        │
        ▼
   XHandDriver::send_left / send_right
        │                                                              │
        ▼                                                              ▼
   RS485 → XHand                                              t1 = steady_clock::now()
                                                              latency_stats.add(t1 - t0)

   Loop end (signal / duration):
       latency_stats.summary() → LOG_INFO "latency [ms]: min=… avg=… p50=… p95=… max=… n=…"
```

注意：t0 是 PC2 内部时钟意义下的 packet-arrived 时刻；它**不包含** Windows → PC2 的 UDP 传输时延（因为 UDCAP JSON 没有 sender timestamp，example.json 顶层只有 `"1"` 包裹一帧，无时间戳字段）。也就是说 M5c 测的是 **PC2 内部的 receive-to-send 延迟**，而非 glove-to-XHand 端到端延迟。SPEC §9 phase 3.9 的 "<50ms" 在 M4 时是按 Python `time.time()` 估的同口径数（同样不含网络段），所以 ±5ms 的横向对比仍然有意义。这条限制在 §11 Execution Record 里要明白写下来，避免审稿误解。

---

## 3. 模块规约（API / behavior / test points）

### 3.1 `src/preset_actions.hpp`（新增；header-only）

**API**：
```cpp
namespace preset_actions {

inline constexpr int kNumJoints = 12;
inline constexpr int kNumPresets = 4;

struct Preset {
    const char* name;
    std::array<double, kNumJoints> deg;   // 度，与 legacy_python/xhand_driver.py:9-14 一致
};

inline constexpr std::array<Preset, kNumPresets> kPresets = {{
    {"fist", { 11.85, 74.58, 40.0, -3.08,106.02,110.0, 109.75,107.56,107.66,110.0,109.10,109.15}},
    {"palm", {  0.00, 80.66, 33.2,  0.00,  5.11,  0.0,   6.53,  0.00,  6.76,  4.41, 10.13,  0.00}},
    {"v",    { 38.32, 90.00, 52.08, 6.21,  2.60,  0.0,   2.10,  0.00,110.00,110.00,110.00,109.23}},
    {"ok",   { 45.88, 41.54, 67.35, 2.22, 80.45, 70.82, 31.37, 10.39, 13.69, 16.88,  1.39, 10.55}},
}};

std::array<double, kNumJoints> deg_to_rad(const std::array<double, kNumJoints>& deg);
const Preset* find_preset(std::string_view name);   // case-sensitive, returns nullptr if unknown

}  // namespace preset_actions
```

**为什么 header-only**：4 个 12-元素数组 + 两个小函数，移到 `.cpp` 没有任何收益。

**Behavior**：
- `deg_to_rad`: `for i in [0,12): rad[i] = deg[i] * M_PI / 180.0;`
- `find_preset`: 线性扫 `kPresets`（N=4），相等返回指针，否则 `nullptr`。

**Test points**：
- §6.2 [LOCAL] header-only 单元 sanity（main 函数 + 一个 static_assert + 一行 print，gcc 编译过 = 通过）。本步骤不依赖 vendor .so，可在 Mac/Linux x86_64 跑。
- §6.5 [PC2] 行为验证（参见 §6 测试章节）。

### 3.2 `src/main.cpp` — actions 分支 + latency 统计

新增辅助 `run_actions(...)`：
```cpp
namespace {

int run_actions(const cli::Args& args, const XHandConfig& xc) {
    // 解析 args.actions 为 std::vector<const Preset*>
    std::vector<const preset_actions::Preset*> list;
    {
        std::stringstream ss(*args.actions);
        std::string name;
        while (std::getline(ss, name, ',')) {
            // trim spaces
            auto* p = preset_actions::find_preset(name);
            if (!p) { LOG_ERROR("unknown preset: '" << name << "'"); return 2; }
            list.push_back(p);
        }
        if (list.empty()) { LOG_ERROR("--actions: empty list"); return 2; }
    }

    XHandDriver driver(xc.serial_port, xc.baud_rate, xc.pid);
    try { driver.open(); }
    catch (const std::exception& e) { LOG_ERROR("XHandDriver: " << e.what()); return 2; }

    std::atomic<bool> shutdown_flag{false};
    safety::install_signal_handlers(shutdown_flag);

    for (const auto* p : list) {
        if (shutdown_flag.load()) break;
        auto rad = preset_actions::deg_to_rad(p->deg);
        safety::clamp_in_place(rad);

        try {
            if (driver.has_left())  driver.send_left(rad);
            if (driver.has_right()) driver.send_right(rad);
        } catch (const std::exception& e) {
            LOG_ERROR("send: " << e.what());
            break;   // driver dtor will mode=0+close
        }

        LOG_INFO("Action " << p->name << ": sent 12 joints, OK");

        auto end = std::chrono::steady_clock::now() +
                   std::chrono::duration<double>(args.hold);
        while (!shutdown_flag.load() && std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    try { driver.shutdown(); }
    catch (const std::exception& e) { LOG_WARN("shutdown: " << e.what()); }
    return 0;
}

}
```

**入口分发**：
```cpp
if (args.actions) {
    // 互斥校验已在 cli::parse 做过
    YAML::Node root;
    try { root = YAML::LoadFile(args.config_path); }
    catch (...) { /* actions 模式即便没有 config 也应能跑（仅需 xhand 串口） */
        // 仍然允许 fall-through：actions 不强依赖 config 文件。
    }
    XHandConfig xc = root.IsNull() ? XHandConfig{} : load_xhand_config(root);
    if (args.port_override) xc.serial_port = *args.port_override;
    return run_actions(args, xc);
}
```
> ⚠️ **plan note**：actions 模式不应**强制**要求 `config.yaml` 存在（M3 验收时 `legacy_python` 版本也仅靠 `--port` 跑）。但若 config.yaml 存在，复用其中的 `xhand.*` 段是合理的。CLAUDE.md "Commands" 段例子 `./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok` 没 `--config`，要保持这条命令仍可工作。实现细节里 LoadFile 失败时 `XHandConfig{}` 默认值 (`/dev/ttyACM0`, 3000000, PID 默认) 与 `--port` 覆盖共同保证。

**Latency stats**（teleop 路径，actions 路径不参与）：
```cpp
struct LatencyStats {
    std::vector<double> samples_ms;   // 简单方案；100Hz × 几分钟 ~= O(10^4) double，可承受
    void add(double ms) { samples_ms.push_back(ms); }
    void summary(std::ostream& os) const {
        if (samples_ms.empty()) { os << "no samples"; return; }
        std::vector<double> s = samples_ms;
        std::sort(s.begin(), s.end());
        size_t n = s.size();
        double sum = std::accumulate(s.begin(), s.end(), 0.0);
        auto pct = [&](double q) { return s[std::min(n-1, size_t(q*(n-1)))]; };
        os << "latency_ms{n=" << n
           << " min=" << s.front()
           << " avg=" << (sum/n)
           << " p50=" << pct(0.50)
           << " p95=" << pct(0.95)
           << " max=" << s.back() << "}";
    }
};
```
- 采样点：仅当 `driver` 真实存在（即 FULL 模式）时，在 `send_left` / `send_right` 之后取 `t1`、计算 `t1 - frame_opt->recv_ts`，调用 `stats.add(...)`。
- 退出前打印一次 `stats.summary(...)`。
- Mock 模式不统计（recv_ts 是合成的，无意义）。Receiver-only 模式不统计（不发送，分子未完整）。

**为什么不是 streaming 算法（Welford / P²）**：M5c 的目标会话规模在 100Hz × 30s ~ 100Hz × 5min ≤ 30k 样本；vector + 排序计算 p95 完全可承受，复杂度透明，调试值得。M6 长时压测如果改 30min 才考虑 streaming。

### 3.3 SIGTERM 行为复核

M5b `src/safety.cpp` 已经 `std::signal(SIGINT, …)` + `std::signal(SIGTERM, …)`（如 ADR-023 要求）。M5c **不**改实现，只在 §6.6 验证场景里 `kill -TERM <pid>` 必须触发同一条 `mode=0 → close_device` 路径。如果实际行为偏离预期，**停下来报告**，不要在 M5c 内默默打补丁。

---

## 4. CLI surface delta（vs M5b）

```
M5b 现状：                                M5c 目标：
  --config <path>                           --config <path>
  --mock                                    --mock
  --receiver-only                           --receiver-only
  --duration <sec>                          --duration <sec>
  --hand <left|right|both>                  --hand <left|right|both>
  --port <path>                             --port <path>
  --actions <names>   ← exits 2             --actions <names>     ← 真实生效
  --help, -h                                --hold <sec>          ← 新增（仅 actions 用，默认 1.0）
                                            --help, -h
```

**互斥矩阵**（cli::parse 内强制）：

| 组合                              | 允许 | 说明                                            |
| --------------------------------- | ---- | ----------------------------------------------- |
| `--actions` + `--mock`            | ❌   | actions 默认就直连硬件；要 mock 用 legacy_python 或将来加 `--actions --mock`，不在 M5c。 |
| `--actions` + `--receiver-only`   | ❌   | 语义冲突（actions 不读 UDP）。                    |
| `--actions` + `--duration`        | ❌   | actions 总时长由 N × `--hold` 决定。              |
| `--actions` + `--hand`            | ✅   | 默认 both；--hand left 时只 send_left（与现 `driver.has_left()` 一致）。 |
| `--actions` + `--port`            | ✅   | --port 覆盖 config.xhand.serial_port。           |
| `--actions` + `--config`          | ✅   | 可选；缺失时用 XHandConfig 默认。                 |
| `--hold` 而无 `--actions`         | ❌   | --hold 仅在 actions 模式有意义；其他场景 cli::parse 报错避免静默忽略。 |
| `--mock` + `--receiver-only`      | ❌   | M5b 已校验。                                     |

---

## 5. PC2 环境前置（一次性 + 每次会话）

### 5.1 一次性（M5c 第一次跑前）

已在 M5a / M5b 跑过的不重复（apt deps、dialout 组、`xhand_control_sdk/` 构建等）。M5c 额外需要确认的：

| 项                                 | [Where]    | Check command                                                                 | 期望                                                                                |
| ---------------------------------- | ---------- | ----------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| 用户在 dialout 组                  | [PC2]      | `groups`                                                                      | 输出含 `dialout`（M5a 已做；M5c 仅复查）                                            |
| `/dev/ttyACM0` 存在且可读写        | [PC2]      | `ls -l /dev/ttyACM*`                                                          | 至少一行；属主 `root dialout`；rw                                                   |
| PC2 与 Windows 同子网（有线优先）  | [PC2/WIN]  | [PC2] `ip a`；[WIN] `ipconfig`                                                | 同 /24，且 [PC2] `ping -c3 <win-ip>` 全通                                           |
| UDP 9000 在 PC2 没被防火墙挡       | [PC2]      | `sudo ufw status` (若 ufw 启用) / 否则 nftables/iptables 检查                | 9000 未在 DENY；如有 ufw，`sudo ufw allow 9000/udp`                                  |
| UDCAP HandDriver 已知 PC2 IP       | [WIN]      | UDCAP 配置 UI：目的地填 PC2 的 `ip a` 输出 IP，端口 9000                      | 与 M5b §6.7 同设置                                                                  |

### 5.2 每次会话前

- 接 XHand 到 PC2 USB，等 ~3s 看 `dmesg -w` 出 `cdc_acm` 行（ADR-014）
- `ls /dev/ttyACM*` 应该看到设备
- UDCAP HandDriver 打开，CalibrationStatus 显示 3
- 开始测试

---

## 6. 测试策略（具体命令 + 预期）

> **会话路径假设**：本仓库在 PC2 上的 working copy 路径记为 `$REPO=/home/<user>/udex_to_xhand`。在 [LOCAL] 上路径是 `/Users/kangzixi/Desktop/4-2/xhand/udex_to_xhand`。下文 `$REPO` 在 [PC2] 命令里展开为前者；[LOCAL] 命令显式给出完整路径。SSH 别名假设为 `pc2`（[LOCAL] `~/.ssh/config` 里 `Host pc2 / HostName <ip> / User <user>`），如未设置请把 `pc2` 替换为 `<user>@<ip>`。

### 6.1 [LOCAL] 实现 + 本地静态检查 — 把 §3.1 / §3.2 改动落地

```bash
# [LOCAL] 在 Mac 上写代码（src/preset_actions.hpp 新增；src/main.cpp / cli.{hpp,cpp} 改）
cd /Users/kangzixi/Desktop/4-2/xhand/udex_to_xhand

# 静态合理性：preset 表元素数 = 12 × 4
grep -c '^[[:space:]]*{"[a-z]*",' src/preset_actions.hpp     # 期望: 4
python3 -c '
import re, sys
txt = open("src/preset_actions.hpp").read()
for line in re.findall(r"\{\".*?\}\}", txt):
    nums = re.findall(r"-?[0-9]+\.[0-9]+", line)
    assert len(nums) == 12, f"expected 12 nums in {line!r}, got {len(nums)}"
print("OK: 4 presets × 12 floats")
'
# 期望: "OK: 4 presets × 12 floats"

# 与 legacy_python/xhand_driver.py:9-14 字节一致性校验
python3 - <<'PY'
import re
py_txt  = open("legacy_python/xhand_driver.py").read()
cpp_txt = open("src/preset_actions.hpp").read()
py_blocks  = dict(re.findall(r'"(\w+)":\s*\[([^\]]+)\]', py_txt))
cpp_blocks = dict(re.findall(r'\{"(\w+)",\s*\{([^}]+)\}\}', cpp_txt))
assert set(py_blocks) == set(cpp_blocks) == {"fist","palm","v","ok"}, (py_blocks, cpp_blocks)
for k in py_blocks:
    py_vals  = [float(x) for x in py_blocks[k].split(",")]
    cpp_vals = [float(x) for x in cpp_blocks[k].split(",")]
    assert len(py_vals) == len(cpp_vals) == 12, (k, len(py_vals), len(cpp_vals))
    for i,(a,b) in enumerate(zip(py_vals, cpp_vals)):
        assert abs(a-b) < 1e-9, (k, i, a, b)
print("OK: PRESETS byte-identical between Python and C++")
PY
# 期望: "OK: PRESETS byte-identical between Python and C++"
```

无法做的事（[LOCAL] 跳过）：完整 cmake build — vendor `libxhand_control.so` 是 aarch64 Linux ELF，Mac 链接必败（M5b §6.2 已确认）。所有 build/run 都迁到 [PC2]。

### 6.2 [LOCAL → PC2] Sync 源码

```bash
# [LOCAL]
cd /Users/kangzixi/Desktop/4-2/xhand/udex_to_xhand
rsync -av --delete \
  --exclude='.git/' --exclude='build/' --exclude='xhand_control_sdk/tests/build/' \
  --exclude='__pycache__/' --exclude='.DS_Store' \
  ./ pc2:udex_to_xhand/
# 期望: rsync 报告若干 ${transferred} files，无 error。
```

### 6.3 [PC2] 增量构建

```bash
# [PC2]
ssh pc2
cd udex_to_xhand
[ -d build ] || mkdir build
cd build
cmake -DBUILD_TESTS=ON .. 2>&1 | tee ../docs/logs/m5c-cmake-2026-05-18.log
make -j 2>&1 | tee ../docs/logs/m5c-make-2026-05-18.log
# 期望: 退出 0；`./udex_to_xhand` 与 `./test_mapper_snapshot` 都存在；无 -Wall -Wextra -Wpedantic 警告。
./test_mapper_snapshot
# 期望: M5b 的结论复现：L max |Δ|=0.0e+00 / R max |Δ|=1.4e-17，全 PASS。
# 用途：证明 actions 改动没破坏 mapper 路径（同一个 src/joint_mapper.cpp 也参与新二进制构建）。
```

### 6.4 [PC2] Serial enum 证据收集（M5c 必须项 d-i）

```bash
# [PC2] 拔掉 XHand USB
ls /dev/ttyACM* 2>&1     # 期望: "No such file or directory"

# [PC2] 重新插上 XHand，等 5s
dmesg | tail -20 | tee -a docs/logs/m5c-ttyacm-2026-05-18.log
# 期望: 见 cdc_acm 行 (例如 "cdc_acm 1-1:1.0: ttyACM0: USB ACM device")
ls -l /dev/ttyACM* | tee -a docs/logs/m5c-ttyacm-2026-05-18.log
# 期望: 出现 /dev/ttyACM0；属主 root dialout；模式 crw-rw----
udevadm info -q property -n /dev/ttyACM0 | grep -E 'ID_VENDOR|ID_MODEL|ID_SERIAL' \
  | tee -a docs/logs/m5c-ttyacm-2026-05-18.log
# 期望: ID_VENDOR/ID_MODEL 与 M5a 记录的厂商一致（SDK 1.4.3 / SN 012L320220250728005 时段的同型号）。
```

### 6.5 [PC2] M3 等价物 — `--actions fist,palm,v,ok`（M5c 必须项 c-1）

```bash
# [PC2]
cd udex_to_xhand/build
./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok 2>&1 \
  | tee ../docs/logs/m5c-actions-2026-05-18.log
```

**期望终端输出（顺序敏感）**：
```
[INFO] XHand SDK version: 1.4.3                        ← 与 M5a 记录的一致
[INFO] Serial: /dev/ttyACM0 @ 3000000 baud
[INFO] hand_id=<N> type=Left                           ← <N> 应与 M5a 的 1 一致
[INFO] Action fist: sent 12 joints, OK
[INFO] Action palm: sent 12 joints, OK
[INFO] Action v: sent 12 joints, OK
[INFO] Action ok: sent 12 joints, OK
[INFO] Shutdown: mode=0 (passive)
[INFO] Device closed
```

**期望物理观察**：
1. 接通后 XHand 不出乎意料地大动作（preset 之前手指应为初始姿态或 mode=0 软关节）。
2. fist：5 指收拢握拳，姿态稳定保持 ~1.0s。
3. palm：所有手指伸开，掌平。
4. V：食指 + 中指竖起，其他收拢；拇指外展。
5. OK：拇指 + 食指尖接近，其他三指竖直伸开。
6. 4 个动作完成后约 0.1s 内 XHand 手指软（mode=0 后无主动力矩）。

**失败标准（任一触发即停 + 报告，不要自改）**：
- 任意 preset 不出现「Action … : sent 12 joints, OK」日志 → SDK 报错路径异常。
- 任一动作姿态与上方描述明显不符（如 fist 实际是 palm） → preset 表或 driver 接线方向有问题。
- 进程退出但 LED / mode=0 未发生 → shutdown 路径异常。

### 6.6 [PC2] SIGTERM 验证（M5c 必须项 d）

```bash
# [PC2] 终端 A
cd udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --hand left 2>&1 \
  | tee ../docs/logs/m5c-sigterm-2026-05-18.log &
APP_PID=$!
sleep 5    # 让 driver.open() 成功，UDP 进入 waiting / 接到第一帧

# [PC2] 终端 A
kill -TERM $APP_PID
wait $APP_PID
```

**期望（tail 处的日志）**：
```
[INFO] received signal, requesting shutdown
[INFO] Shutdown: mode=0 (passive)
[INFO] Device closed
[INFO] exited after 5.x s, ... ticks, ... valid frames, 0 parse errors
```

**期望物理观察**：手指松弛（mode=0），无电机抖动 / 持续供电感。

注：M5c 在测 SIGTERM 时 UDCAP 可以**不开**——这是「优雅关机」语义本身，不需要 UDP 流量在场。

### 6.7 [PC2] 网络通路证据（M5c 必须项 d-ii）

```bash
# [PC2]
ip addr show | tee -a docs/logs/m5c-network-2026-05-18.log
# 期望: 有线网卡（一般 enpXsY）拿到与 Windows 同 /24 的 IP；记下该 IP 为 PC2_IP。

ping -c 5 <WINDOWS_IP> | tee -a docs/logs/m5c-network-2026-05-18.log
# 期望: 0% packet loss。

sudo ss -ulpn 'sport = :9000' | tee -a docs/logs/m5c-network-2026-05-18.log
# 期望: 在跑 receiver-only 或 FULL 模式时本进程持有 0.0.0.0:9000。
```

[WIN] 侧：打开 UDCAP HandDriver 设置，把目的地 IP 改为上一步的 PC2_IP，端口 9000。这一步无 CLI 可截图，因此 [WIN] 行为以 §6.8 出现 packet 计数为验。

### 6.8 [PC2] M4 等价物 — 单手实时遥操 + Latency 测量（M5c 必须项 c-2 + Acceptance）

```bash
# [PC2] 提前确认 UDCAP HandDriver 已经在 Windows 侧打开，CalibrationStatus L=3、目的地 = PC2_IP:9000。
cd udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --hand left --duration 30 2>&1 \
  | tee ../docs/logs/m5c-teleop-left-2026-05-18.log
# --duration 30 限定本次 30s 自动退出，便于 latency stats 收敛但不过长。
```

**期望终端首尾**：
```
[INFO] config loaded: ../config.yaml
[INFO] mode: FULL (UDP + XHand)
[INFO] XHand SDK version: 1.4.3
[INFO] hand_id=<N> type=Left
[INFO] waiting for first packet...
[INFO] first packet from <WINDOWS_IP>
...
[INFO] latency_ms{n=… min=… avg=… p50=… p95=… max=…}
[INFO] exited after 30.0s, ~3000 ticks, ~2700+ valid frames, 0 parse errors
```

**期望物理观察清单（戴左手 UDCAP，操作员目检）**：
- [ ] **拇指**：仅弯左手拇指 → XHand 的 J0(thumb_bend) / J1(thumb_rota1) / J2(thumb_rota2) 跟随；其他 9 关节静止。
- [ ] **食指**：仅弯左手食指 → J3(index_bend) / J4(index_joint1) / J5(index_joint2) 跟随；其他静止。
- [ ] **中指**：仅弯中指 → J6 / J7 跟随；其他静止。
- [ ] **无名指**：仅弯无名指 → J8 / J9 跟随；其他静止。
- [ ] **小指**：仅弯小指 → J10 / J11 跟随；其他静止。
- [ ] **握拳**：全手套握拳 → XHand 握拳；
- [ ] **张开**：全手套张开 → XHand 张开；
- [ ] **无可见反向 / 串扰**：任一手指方向反了或带动他指 → 失败。

**Latency Acceptance**：
- avg < 50ms（SPEC §9 phase 3.9 目标）
- p95 < 50ms（更严的鲁棒性指标，M5c 自加）
- 与 Python M4 dev-PC 基线对比 ±5ms 内（roadmap §M5 完成定义）—— Python M4 没有归档具体 latency 数值（M4 plan §4 只跑视觉验收，未量化 latency），所以这一条以 SPEC §9 的 <50ms 绝对界为权威，basis 见 §11 Execution Record 注。

**失败时**：保留 log 原样，不要自改代码；停下报告（同 M5a / M5b 模式）。常见原因排查清单：
- avg ≪ 50ms 但 p95 ≫ 50ms：调度抖动 / RS485 偶发延迟 / GC-类影响 → 看 max 与样本分布。
- avg 与 p95 都接近 50ms：UDP 转发链路问题 / Windows 端 send 频率不稳 → 用 `tcpdump -i any udp port 9000 -nn -ttt` 看间隔。
- 跟随方向错：C++ joint_mapper 已经位级等同 Python（M5b snapshot 证明），方向错只能是 config.yaml 改过或硬件接线（左/右 hand_id 错配） → 看 §6.5 hand_id=… type=Left 日志是否仍为 Left。

### 6.9 [PC2] Static-analysis pass（沿用 M5b §6.8 风格）

```bash
# [PC2]
cd udex_to_xhand/build
make clean && cmake -DBUILD_TESTS=ON .. && make 2>&1 | tee make-strict.log
grep -E 'warning:' make-strict.log
# 期望: 无输出。
file ./udex_to_xhand
# 期望: ELF 64-bit LSB pie executable, ARM aarch64
ldd ./udex_to_xhand | grep -i xhand
# 期望: libxhand_control.so 解析到 ../xhand_control_sdk/lib/...（M5b RPATH 已设置）
```

### 6.10 [PC2 → LOCAL] Log 回收

```bash
# [LOCAL] 把 PC2 上的 docs/logs/m5c-*.log 拉回本地仓库
cd /Users/kangzixi/Desktop/4-2/xhand/udex_to_xhand
rsync -av pc2:udex_to_xhand/docs/logs/m5c-*.log docs/logs/
ls docs/logs/m5c-*.log
# 期望: 6 个文件（cmake / make / actions / sigterm / teleop / network / ttyacm 中存在的全部）。
```

---

## 7. Definition of Done (M5c)

(DoD 编号尽量 1:1 对应 acceptance bullets，方便 checkpoint。)

- [ ] (D1) `src/preset_actions.hpp` 提交；与 `legacy_python/xhand_driver.py:9-14` PRESETS 字节一致（§6.1 自动校验）。
- [ ] (D2) `src/main.cpp` 实施 `run_actions(...)` + latency 统计；旧的「return 2; M5c scope」占位移除。
- [ ] (D3) `src/cli.{hpp,cpp}` 新增 `--hold` 与互斥校验；usage 字符串更新。
- [ ] (D4) PC2 上 `make -j` exit 0，`-Wall -Wextra -Wpedantic` 0 警告。
- [ ] (D5) PC2 上 `./test_mapper_snapshot` 复跑 PASS（M5b 结论未退化）。
- [ ] (D6) `./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok` 4 个动作目检通过，日志归档。
- [ ] (D7) `kill -TERM <pid>` 触发 mode=0 + close_device；日志归档。
- [ ] (D8) `./udex_to_xhand --config ../config.yaml --hand left --duration 30` 5 项手指 + 握拳 + 张开 + 无反向串扰，全部目检通过。
- [ ] (D9) Latency 摘要 `avg < 50ms` 且 `p95 < 50ms`。
- [ ] (D10) 7 个 log 全部归档到 `docs/logs/m5c-*-2026-05-18.log`。
- [ ] (D11) §11 Execution Record 填回本计划文件；包含 latency 数值原文。
- [ ] (D12) `docs/plans/00-roadmap.md` 加第 5 条 Revision History；M5 主标题加 ✅；M5c 子节末尾「结果」行；ADR 行追加新编号（若有）。
- [ ] (D13) `SPEC.md` §9 phase 3.9 旁加 M5c 实测数值标注。
- [ ] (D14) 新 ADR 落档（数量 / 编号 / 题名见 §10 候选；以执行后实际为准）。
- [ ] (D15) 单次 git commit（M5a / M5b 模式）汇总本 milestone 全部变更；commit message 引用 §11 关键数字。

---

## 8. What this milestone deliberately does **not** cover

| 不做的事 | 推迟到 | 备注 |
| -------- | ------ | ---- |
| Watchdog 故障注入（kill UDCAP 验证 hold-last-position） | M6 | M5b 已经把 `safety::Watchdog::update()` 接在 receive 路径上；M5c 仅信赖既有路径，不主动断网测。 |
| Clamp 故障注入（人为收紧 clamp） | M6 | M5b 已经在 mapper 与 send 之前都做 `clamp_in_place`；M5c 不修改 config 验。 |
| 右手 XHand 接入 | M7 | 仅当 §6.5 偶然发现 hand_id 同时有 R 时记录现象，不验。 |
| 右手 mapping 符号 / 镜像验证 | M7 | SPEC §3.1 明确 M7 scope。 |
| PID 调优 | M8 | kp=100 / kd=0 维持。 |
| 抓杯子验收 | M8 | 验收测试在 M8。 |
| 30 分钟压测 | M8 | M5c 只跑 30s。 |
| 端到端（含 UDP 链路）latency | future | 因 UDCAP JSON 不含 sender timestamp，本 milestone 测的是 PC2 内部 `recv_ts→post-send`。 |

---

## 9. M5c 特定风险

| #   | 风险                                                                            | 概率 | 影响 | 缓解                                                                                                          |
| --- | ------------------------------------------------------------------------------- | ---- | ---- | ------------------------------------------------------------------------------------------------------------- |
| R1  | M5b 的 driver 实现没在硬件上跑过；首次 `send_command` 触发 SDK error_code 非 0 | MED  | MED  | ADR-017 既定行为是 LOG_WARN 不崩；§6.5 仍能看到「sent 12 joints, OK」(动作日志先打)，但要核对 stderr WARN 行。 |
| R2  | actions 模式 hold=1.0 时 XHand 来不及到位                                       | MED  | LOW  | --hold 可调；§6.5 失败时第一招把 --hold 提到 1.5 重测。                                                       |
| R3  | UDCAP HandDriver 目的地 IP 配错，进程在 `waiting for first packet…` 卡死        | MED  | LOW  | §6.7 先确认 ping + ss；如 §6.8 看不到 first packet from … 行，先回 §6.7 不要怪 C++。                          |
| R4  | latency 包含调度抖动导致 p95 偶尔 >50ms                                         | MED  | MED  | 30s 收 ~3000 样本，p95 用 floor(0.95*(n-1))；M5c 失败时记录原始 vector 决定是否真退化。                       |
| R5  | hand_id 与 M5a 不同（vendor 固件升级 / cable 换序）                             | LOW  | LOW  | M5c 不假设 hand_id 数值；只看 `get_hand_type` 返回 L/R。M5a 记录的 hand_id=1 仅为参考。                       |
| R6  | Python M4 dev-PC latency 基线缺失，「±5ms」无法落字                             | HIGH | LOW  | §6.8 已说明以 SPEC §9 <50ms 为权威；roadmap 里那句话视为指导而非硬约束。Execution Record 写清楚。              |
| R7  | SIGTERM 路径上 close_device 卡住（USB hub 异常）                               | LOW  | MED  | §6.6 `wait $APP_PID` 限 10s；超时 `kill -9` + 报告。M5c 范围内不修，记入 ADR / 下一 milestone。               |

---

## 10. 候选 ADRs（执行后定）

候选条目（最终入库的题目和编号视执行结果定，下表仅占位避免审稿人怀疑「会不会漏决定」）：

1. **ADR-032 候选**：「Preset 表 header-only + 与 Python 字节一致校验」 —— 为什么不让 C++ 自己重写一份数字；为什么不 `.csv`/`.yaml`/`.json` 持久化（与 Python 同步要靠人或脚本，header-only + 自动校验脚本是最低成本）。
2. **ADR-033 候选**：「Latency 用 vector + 排序，不上 Welford / P²」 —— 数据量 ≤ 30k，复杂度透明胜过 streaming 的近似性。M6+ 再升级。
3. **ADR-034 候选**：「M5c 不验 watchdog / clamp 故障注入」 —— 路线图划给 M6；M5c 边界明确。
4. **ADR-035 候选**：「actions 模式不强制 `--config`」 —— 与 Python M3 `xhand_driver.py --port … --actions …` 用法一致；CLAUDE.md "Commands" 段命令保持可用；XHandConfig 默认值足以撑起 M3 等价物。

实际命名 / 是否合并 / 拆分由跑完后判断；要点是覆盖以上所有「不显然」的选择。

---

## 11. Execution Record（M5c 跑完后补回此节，先留 placeholder）

> Fill date / values / log excerpts after the run, mirroring M5b §11 layout.

### 11.1 Environment snapshot
- 日期：
- PC2 host / kernel / 内存：
- g++ version：
- xhand_control SDK 版本：
- 硬件 SN：
- UDCAP HandDriver 版本（如能取到）：
- Windows IP / PC2 IP / 链路：

### 11.2 Build (§6.3)
- cmake 退出码 / 警告条数：
- make 退出码 / 警告条数：
- test_mapper_snapshot PASS？ L max|Δ|= R max|Δ|=

### 11.3 Serial enum (§6.4)
- ttyACM 设备路径 / 属主：
- dmesg cdc_acm 行原文：
- udevadm vendor/model/serial：

### 11.4 Actions (§6.5)
- 4 个 preset 日志 + 物理观察（pass/fail per preset）：

### 11.5 SIGTERM (§6.6)
- kill -TERM 后 tail 日志：
- 手指松弛状态确认：

### 11.6 Network (§6.7)
- PC2 网卡 + IP：
- ping 通断 + 延迟分布：
- ss 9000 端口持有：

### 11.7 Teleop + Latency (§6.8)
- 30s 会话：valid_frames / parse_errors：
- 5 项手指 + 握拳 + 张开目检：
- latency 摘要原文（min / avg / p50 / p95 / max / n）：
- vs SPEC §9 <50ms：

### 11.8 Static checks (§6.9)
- file 输出：
- ldd xhand_control 解析：
- 警告数：

### 11.9 Anomalies / notes
- 偏离 plan 处（如果有，停下来 + 报告 + 等 user 决策 / 同意 + 再继续）：
- 新发现的 SDK 行为：
- M6+ 留观察点：

---

## 12. Commit shape（advisory，沿用 M5a / M5b 模式）

单次 commit，message 顶部例：

```
M5c ✅: udex_to_xhand C++ binary hardware re-validated on G1 PC2

- preset_actions.hpp (header-only) + run_actions(...) in main.cpp;
  actions byte-identical to legacy_python/xhand_driver.py:9-14
- latency stats (n=…, avg=…ms, p95=…ms) added on teleop send path
- M3 equiv: fist/palm/v/ok all observed correctly on LEFT hand
- M4 equiv: 30s left-hand teleop, 5 fingers + fist + palm all passed
- SIGTERM verified: mode=0 + close_device path triggered (ADR-023)
- 7 logs archived under docs/logs/m5c-*-2026-05-18.log

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

文件清单近似（以最终 git diff 为准）：

```
modified:   CLAUDE.md (optional, only if --hold default needs to surface)
modified:   SPEC.md (§9 phase 3.9 annotation)
modified:   docs/plans/00-roadmap.md (rev history #5; M5 ✅; M5c result line; ADR list)
modified:   docs/plans/20260518-m5c-pc2-hardware-revalidation-plan.md (§11 filled)
new file:   docs/decisions/032-*.md
new file:   docs/decisions/033-*.md (optional)
new file:   docs/decisions/034-*.md (optional)
new file:   docs/decisions/035-*.md (optional)
new file:   docs/logs/m5c-cmake-2026-05-18.log
new file:   docs/logs/m5c-make-2026-05-18.log
new file:   docs/logs/m5c-actions-2026-05-18.log
new file:   docs/logs/m5c-sigterm-2026-05-18.log
new file:   docs/logs/m5c-network-2026-05-18.log
new file:   docs/logs/m5c-teleop-left-2026-05-18.log
new file:   docs/logs/m5c-ttyacm-2026-05-18.log
modified:   src/cli.cpp
modified:   src/cli.hpp
modified:   src/main.cpp
new file:   src/preset_actions.hpp
```

---

## Appendix A — Operator quick card

```
# [LOCAL] 改完代码同步到 PC2
cd /Users/kangzixi/Desktop/4-2/xhand/udex_to_xhand
rsync -av --delete --exclude='.git/' --exclude='build/' \
  --exclude='xhand_control_sdk/tests/build/' --exclude='__pycache__/' \
  --exclude='.DS_Store' ./ pc2:udex_to_xhand/

# [PC2] 一站式跑（按本计划顺序）
ssh pc2
cd udex_to_xhand
mkdir -p build && cd build
cmake -DBUILD_TESTS=ON .. 2>&1 | tee ../docs/logs/m5c-cmake-2026-05-18.log
make -j 2>&1                  | tee ../docs/logs/m5c-make-2026-05-18.log
./test_mapper_snapshot

# Hardware: serial
ls -l /dev/ttyACM*            | tee ../docs/logs/m5c-ttyacm-2026-05-18.log
dmesg | tail -20             >>  ../docs/logs/m5c-ttyacm-2026-05-18.log

# Hardware: M3 equiv
./udex_to_xhand --port /dev/ttyACM0 --actions fist,palm,v,ok 2>&1 \
  | tee ../docs/logs/m5c-actions-2026-05-18.log

# Hardware: SIGTERM (UDCAP 不需要在场)
./udex_to_xhand --config ../config.yaml --hand left 2>&1 \
  | tee ../docs/logs/m5c-sigterm-2026-05-18.log &
sleep 5; kill -TERM $!; wait

# Network 证据（UDCAP HandDriver 已配置 → PC2_IP:9000）
ip addr show          | tee -a ../docs/logs/m5c-network-2026-05-18.log
ping -c 5 <WIN_IP>    | tee -a ../docs/logs/m5c-network-2026-05-18.log
sudo ss -ulpn 'sport = :9000' >> ../docs/logs/m5c-network-2026-05-18.log

# Hardware: M4 equiv + Latency
./udex_to_xhand --config ../config.yaml --hand left --duration 30 2>&1 \
  | tee ../docs/logs/m5c-teleop-left-2026-05-18.log

# [LOCAL] 回收日志
rsync -av pc2:udex_to_xhand/docs/logs/m5c-*.log docs/logs/
```
