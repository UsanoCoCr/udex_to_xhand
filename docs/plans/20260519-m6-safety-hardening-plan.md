# M6 — Plan: Safety Layer 强化（C++）

- Milestone: M6 (Safety Layer, C++)
- Date: 2026-05-19
- Estimate: 0.5d
- Status: Plan
- Predecessors: M5 整体 ✅（M5a / M5b / M5c 全部通过；C++ 二进制 `udex_to_xhand` 在 G1 PC2 真实硬件上完成 M3 + M4 行为复测，ADRs 023-034）
- Successor blocked-by-this: M7 双手集成；M8 调优 + 验收
- Deliverable on success: 在 PC2 真实硬件上，4 个安全场景全部通过 fault-injection —— (1) Watchdog 在 UDCAP 被杀后保持位置 + 日志告警；(2) Ctrl+C 触发 mode=0 + close_device；(3) `kill -TERM <pid>` 行为等价于 Ctrl+C；(4) `config.yaml` 收窄 clamp 后 XHand 物理停在该角度。新增第 5 项：**Startup gate timeout**（无 UDCAP 数据时 binary 必须在固定秒数内带明确 LOG 退出，而不是 100Hz 空转）。
- Operator note: 本计划作者**不在** G1 PC2 前。所有命令以 `[LOCAL]`（开发 Mac，darwin/arm64）/ `[PC2]`（G1 PC2，aarch64 Ubuntu）/ `[WIN]`（Windows，UDCAP 宿主）标签明确区分。LOCAL 段可在 Mac 上落地 + 单元测试通过；PC2 段是用户回到机器跟前再跑的脚本，预期产出已写在 §3/§4/§5。

---

## 0. Why this milestone & 范围边界

M5b/M5c 已经把 safety **基础设施**装好了：

| Safety primitive            | 当前位置                                                         | 状态                                                              |
| --------------------------- | ---------------------------------------------------------------- | ----------------------------------------------------------------- |
| Watchdog class              | `src/safety.{hpp,cpp}` (`update` / `is_stale` / `has_seen_frame`) | 类已写完；`main.cpp:341` 调 `update`；**`is_stale` 从未被调用** |
| Radian-domain fail-safe clamp | `src/safety.cpp::clamp_in_place`（全局 ±90°/+110°）               | 主循环 + actions 模式都在 send 前调用 ✅                          |
| Per-joint degree-domain clamp | `src/joint_mapper.cpp:82`（config-driven）                       | M4 + 位级 snapshot 测试覆盖 ✅                                    |
| SIGINT / SIGTERM handler      | `src/safety.cpp::install_signal_handlers`                        | M5c §6.6 已视觉验证 SIGTERM → mode=0 ✅                          |
| Mode=0 + close_device 出口    | `src/xhand_driver.cpp::shutdown`                                  | M5c 视觉验证 ✅（注意：mode=0 不等于断电，ADR-018）              |
| 启动 hand-id 校验             | `src/xhand_driver.cpp::open`（list_hands_id + get_hand_type）    | ADR-029；M5c 实跑通过 ✅                                          |
| UDCAP calib gating            | `src/udcap_receiver.cpp:143-144` (`calib != 3 → nullopt`)        | M5b receiver-only + M5c teleop 都通过 ✅                          |

M5b 在 `src/safety.hpp:20` 自己写明：「The "hold last position on stale" reaction is M6 scope; M5b only wires the class.」 — **这就是 M6 要补的最后一块**。除此之外，主循环还缺一条「**等首个有效帧、否则按超时退出**」的启动门，否则 UDCAP 未启动时 binary 会无限空转 100Hz。

### 0.1 In-scope（M6 必须做）

- (a) 在 `src/main.cpp` 主循环里实现「**Watchdog stale 反应**」：当 `wdog.is_stale(now)` 为真且已见过至少一帧，重发上一帧已发送的左/右手命令（不是上一帧 raw UDP，而是上一帧 mapper+clamp 之后真正进了 send_left/send_right 的弧度数组），并以 **1Hz 限速** 写一行 `LOG_WARN`。不修改 `safety::Watchdog` 的 API。
- (b) 在 `src/main.cpp` 的非 mock 路径加一条 **Startup gate**：在进入主控制循环之前，最多等 `udcap.startup_timeout_s`（默认 10s）拿首个 `calib==3` 的帧；超时仍无包则 `LOG_ERROR` + `return 2`（不开始任何 send_command）。`signal` flag 设置时也立即退出。
- (c) `config.yaml` 顶层 `udcap:` 段新增字段 `startup_timeout_s: 10`；在 `main.cpp::UdcapConfig` 与 `load_udcap_config` 同步加载（缺字段 → 使用默认）。
- (d) 新增 `tests/test_safety.cpp` 单元测试，覆盖 `safety::Watchdog`（fresh / pre-timeout / post-timeout / 更新后复位）与 `safety::clamp_in_place`（边界、双侧越界、no-op）。`CMakeLists.txt` 的 `BUILD_TESTS` 选项下追加 `test_safety` target；不链 `xhand_control`，**LOCAL Mac 可直接 build + run**。
- (e) 在 PC2 上跑完 5 个 fault-injection 场景（roadmap §M6 测试 1/2/2b/3 + 新增的 startup-gate 5），各自产 `docs/logs/m6-*-2026-05-19.log` 一份。
- (f) Roadmap + SPEC 同步：roadmap §M6 末尾追加 ✅ 结果行；本 plan §8 Execution Record 回填；ADR 新增（候选见 §7）。

### 0.2 Out-of-scope（明确不在 M6 里做）

| 项目                                                            | 归属    | 原因                                                                                    |
| --------------------------------------------------------------- | ------- | --------------------------------------------------------------------------------------- |
| 双手并行 / 右手 RS485 寻址                                       | **M7**  | roadmap §M7 全部范围。M6 验证全部在 `--hand left` 下做（M5c 已证 left 通路）。           |
| PID kp/kd 调优；低通滤波；30 分钟压测                            | **M8**  | roadmap §M8。M6 只验「安全副作用不破坏遥操可用性」，不动控制参数。                       |
| 拇指 retargeting 算法重写                                       | **M8**  | roadmap §M8（2026-05-19 第 6 次修订）。                                                  |
| 把 `safety::clamp_in_place` 改成 per-joint 弧度域钳位            | 不做    | ADR-021 明确两层钳位分工：mapper = degree 域 per-joint，safety = radian 域全局 fail-safe。重复同一份 per-joint 表会引入配置漂移。 |
| 改 `safety::Watchdog` 公共 API                                  | 不做    | 类已稳定；只在 main.cpp 里 **使用** `is_stale`。                                          |
| 用 `sigaction` 替换 `std::signal`                               | 不做    | M5c 已视觉验证 `std::signal` 在 Linux glibc 下行为正确；不为概念优雅做无副作用的重写。  |
| 删除 `legacy_python/`                                            | 候选    | ADR-031 写明 post-M5c 候选；`joint_mapper.py` 仍是 snapshot oracle，不动。              |
| 任何对 vendor `xhand_control_sdk/` 的修改                       | 永不    | ADR-025。                                                                               |

### 0.3 Acceptance（DoD，单行）

- 5 个 fault-injection 场景全过；`tests/test_safety` LOCAL `ctest` 全绿；`udex_to_xhand --config config.yaml --hand left` 在 UDCAP 正常时跟随观感与 M5c 一致（无回退），UDCAP 异常时按 §2 的 3 条路径退出/保持。

---

## 1. 文件清单

### 1.1 新增 — committed by M6

| Path                                            | One-line responsibility                                                                                                                                                                              |
| ----------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `tests/test_safety.cpp`                         | 单元测试 `safety::Watchdog` 状态机 + `safety::clamp_in_place` 边界；不链 vendor SDK；LOCAL Mac 可跑。                                                                                              |
| `docs/decisions/035-watchdog-stale-resend.md`   | 「stale 期间重发最后命令 @ 100Hz、日志限速到 1Hz」 vs 「停发等服务端自然 hold」的取舍记录。                                                                                                          |
| `docs/decisions/036-startup-gate-timeout.md`    | 「首包超时 10s 后退出」 vs 「无限等」 / 「立即开循环空转」三选一的取舍记录，给出默认 10s 的依据。                                                                                                    |
| `docs/logs/m6-build-2026-05-19.log`             | `[PC2] cmake .. -DBUILD_TESTS=ON && make && ./tests/test_safety` 输出。                                                                                                                              |
| `docs/logs/m6-watchdog-2026-05-19.log`          | 场景 1（killUDCAP→hold）会话输出，含 stale 警告行与 valid frames 计数。                                                                                                                              |
| `docs/logs/m6-sigint-2026-05-19.log`            | 场景 2（Ctrl+C）会话输出。                                                                                                                                                                           |
| `docs/logs/m6-sigterm-2026-05-19.log`           | 场景 2b（`kill -TERM`）会话输出 — 替换 M5c 因 `tee` 截断未归档的版本，本次必须落盘干净。                                                                                                            |
| `docs/logs/m6-clamp-2026-05-19.log`             | 场景 3（config.yaml 收窄 `index_joint1.clamp` 到 [0,30]）会话输出 + 物理观测笔记。                                                                                                                  |
| `docs/logs/m6-startup-gate-2026-05-19.log`      | 场景 5（不开 UDCAP 启动 binary）会话输出 — 期望 10s 后带 `LOG_ERROR` 干净退出，return code 非零。                                                                                                    |

### 1.2 修改 — committed by M6

| Path                                                                                | Change                                                                                                                                                                                                                                                                                                                                  |
| ----------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/main.cpp`                                                                      | (a) 新增 `wait_first_valid_frame(receiver, timeout, shutdown_flag)` 自由函数（namespace `{}`），返回 `std::optional<UdcapFrame>`。(b) `UdcapConfig` 加 `int startup_timeout_s{10}`；`load_udcap_config` 读 `udcap.startup_timeout_s`。(c) 主循环里维护两个 `std::optional<std::array<double,12>> last_left_rad, last_right_rad`，发送成功后写入。(d) `if (frame_opt) { ... } else { /* stale branch */ }` ：当 `driver && wdog.has_seen_frame() && wdog.is_stale(now)` 时重发 `last_*_rad`，并经 1Hz 限速器写一行 `LOG_WARN`。stale 结束（下一个有效帧到达）时打印一行 "watchdog: recovered after Nms"。(e) 进主循环前先调 `wait_first_valid_frame`，nullopt 时返回 2。 |
| `src/safety.hpp`                                                                    | 注释更新：`Watchdog` 上的 "The 'hold last position on stale' reaction is M6 scope" 改成 "Stale reaction wired in `main.cpp` per M6 plan §2.1; see ADR-035。" 不动 API。                                                                                                                                                                  |
| `config.yaml`                                                                       | `udcap:` 段顶部加一行 `startup_timeout_s: 10` + 同一行尾注释 `# M6: abort if no calibrated UDP frame within N seconds`。其他字段全部不动。                                                                                                                                                                                            |
| `CMakeLists.txt`                                                                    | `if(BUILD_TESTS) ... endif()` 块里追加 `add_executable(test_safety tests/test_safety.cpp src/safety.cpp)` + `target_include_directories(test_safety PRIVATE src)` + `target_compile_options(test_safety PRIVATE -Wall -Wextra -Wpedantic)`。不链 `xhand_control` / yaml-cpp / nlohmann_json / openssl（不需要）。                       |
| `docs/plans/00-roadmap.md`                                                          | (a) 顶部 Revision History 加第 7 条「M6 执行记录」。(b) `## M6: Safety Layer (C++)` 标题加 ✅。(c) M6 章节末尾追加「**结果 (2026-05-19)**：…」尾行（沿用 M5a/M5b/M5c 风格，引本 plan §8 + 各 log 文件）。(d) `**ADRs**:` 追加 035 / 036（若执行中出新决定，追加更多）。                                                                |
| `SPEC.md`                                                                           | §5「Watchdog timeout」一行尾注 ：「**Stale 反应** = 重发 last commanded rad @ 100Hz，`LOG_WARN` @ 1Hz；recovered 时一行 INFO」。§11 config.yaml schema 段加 `startup_timeout_s: 200      # M6 (seconds, default 10)`（注意单位提示）。其他 § 不动。                                                                                  |
| `docs/plans/20260519-m6-safety-hardening-plan.md`（本文件）                          | §8 Execution Record 在跑完后填回。                                                                                                                                                                                                                                                                                                       |

### 1.3 不修改（read-only inputs for M6）

- `src/safety.cpp` — `Watchdog` / `clamp_in_place` / `install_signal_handlers` 逻辑不动；M6 完全在使用者侧 (`main.cpp`) 接入既有 API。
- `src/xhand_driver.{hpp,cpp}` — `open` 已做 list_hands_id + get_hand_type；`shutdown` 已做 mode=0 + close_device；M6 不动。
- `src/udcap_receiver.{hpp,cpp}` — calib gating 已在 `try_recv` 内 (`calib != 3 → nullopt`)；M6 在 `main.cpp` 里只读 `frame.recv_ts` 和调 `try_recv`，不改 receiver。
- `src/joint_mapper.{hpp,cpp}` — per-joint degree-domain clamp 是 M6 测试 3 的被测对象，但代码不需要改（只改 `config.yaml` 里 index_joint1.clamp 触发它，跑完场景再把 config 改回来）。
- `src/cli.{hpp,cpp}` — 不增 CLI flag；startup timeout 走 config.yaml（与现有 `udcap.timeout_ms` 同源，避免新增 CLI 表面）。
- `src/preset_actions.hpp` — 不动；actions 模式不走 watchdog（无 UDP），与 M6 无关。
- `xhand_control_sdk/` — vendor，ADR-025。
- `tests/test_mapper_snapshot.cpp` + `tests/fixtures/mapper_baseline.json` — algorithm 位级一致由 M5b 锁定，M6 不重做。

---

## 2. 数据流

### 2.1 主循环：正常 + Stale + Recovered（M6 新增分支用 ★ 标记）

```
   每 10ms (rate_hz=100) tick:
        │
        ▼
   frame_opt = (mock ? mock_frame : receiver.try_recv())
        │
        ├──[ frame_opt 有值 ]──►   wdog.update(frame.recv_ts)
        │                          map_left  → clamp → driver.send_left → last_left_rad  = rad
        │                          map_right → clamp → driver.send_right → last_right_rad = rad
        │                          (latency stats; M5c)
        │                  ★      if was_stale: LOG_INFO "watchdog: recovered after Nms"
        │                  ★                    was_stale = false
        │
        └──[ frame_opt 空 ]
              │
              ├──★ if (!driver) → continue                  // mock / receiver-only：与 stale 无关
              │
              ├──★ if (!wdog.has_seen_frame()) → continue    // 还没首包；由 §2.2 startup gate 处理
              │
              └──★ if (wdog.is_stale(now)):
                      if (last_left_rad)  driver.send_left (*last_left_rad)
                      if (last_right_rad) driver.send_right(*last_right_rad)
                      if (now - last_warn_ts >= 1s):       // 1Hz 限速
                          LOG_WARN "watchdog: no UDP for >200ms, holding last position"
                          last_warn_ts = now
                      was_stale = true
              ▼
   sleep_until(next_tick)
```

**为什么是「重发上一帧 rad」而不是「停发」**：

XHand mode=3 是位置控制；电机收一次目标位会持续 hold，但 SDK 文档没明确说不重发是否会触发 watchdog/解力。M5c 期间也没单独验过 "send 一次后停发 N 秒电机是否漂移"。**保守选择重发**，与控制器对 100Hz 节奏的预期一致，且复用 send_command 也意味着 RS485 链路在 stale 期间持续在线（断线会立刻让 SDK 抛 error_code，被 ADR-017 log-not-crash 路径捕获，操作员能看见）。落 ADR-035。

**为什么 LOG_WARN 限速 1Hz 而不是仅打一次**：

「仅打一次」会让操作员在 stale → recovered → stale 翻转时漏看；100Hz 全打会冲垮 stderr 与日志文件。1Hz 是肉眼可读 + 文件不爆炸的中点（30 分钟压测最多产 1800 行 stale 警告）。ADR-035 同时记录。

### 2.2 Startup gate（M6 新增；mock 模式不走这条路径）

```
   main(...) 进主循环之前:
        │
        ▼
   if (args.mock) skip   ← mock 直接用 example.json 合成 frame
        │
        ▼
   LOG_INFO "waiting for first calibrated UDP frame (timeout=10s)..."
        │
        ▼
   loop {
        if (shutdown_flag.load()) return 2;
        f = receiver.try_recv();
        if (f) return f;                            ← receiver 内部已过滤 calib != 3
        if (elapsed >= startup_timeout_s) {
            LOG_ERROR "startup gate: no calibrated UDP frame in <N>s; aborting";
            return 2;                               ← 此时 driver 已 open；析构会跑 shutdown()（mode=0 + close_device）
        }
        sleep_for(10ms);
   }
        │
        ▼
   首帧 f 进入主循环；wdog.update(f.recv_ts)
```

**关键不变式**：

- `driver.open()` 在 startup gate 之前；这意味着如果 gate 超时退出，`XHandDriver` 的析构会触发 mode=0+close_device（已由 `xhand_driver.cpp:14-19` 提供），与正常退出对硬件状态一致。
- 第一次成功 `try_recv` 拿到的 frame 也算 watchdog 的首次 `update` —— 把它直接喂回主循环的「frame_opt 有值」分支，避免重复 try_recv 浪费一拍。
- `signal` 在 gate 内一样生效（同一个 `shutdown_flag`），所以 `Ctrl+C` 在「等首包」阶段也能立刻退出。

### 2.3 SIGINT / SIGTERM（复核，无代码变更）

`safety::install_signal_handlers` 注册的 `on_signal` 把 `shutdown_flag` 置 true（async-signal-safe，仅一次 atomic store）。主循环条件 `while (!shutdown_flag.load() && !duration_elapsed())` 立刻退出；`driver->shutdown()` 在退出路径上发 mode=0 + close_device（`main.cpp:400-403`）。M5c §6.6 已视觉验证；M6 只把这条路径在 §4 用 fault-injection 命令再跑一遍 + 完整落盘 log。

---

## 3. 模块规约（API / behavior / test points）

### 3.1 `src/main.cpp` — startup gate 函数

```cpp
// In anonymous namespace; pure helper. Returns the first frame with calib==3,
// or nullopt on timeout / shutdown. 'now()' is steady_clock per CLAUDE.md
// safety conventions. Polls at 100Hz to match main loop pace.
std::optional<UdcapFrame> wait_first_valid_frame(
    UdcapReceiver& rx,
    std::chrono::seconds timeout,
    const std::atomic<bool>& shutdown_flag)
{
    using namespace std::chrono;
    LOG_INFO("waiting for first calibrated UDP frame (timeout="
             << timeout.count() << "s)...");
    const auto deadline = steady_clock::now() + timeout;
    while (!shutdown_flag.load()) {
        if (auto f = rx.try_recv()) return f;
        if (steady_clock::now() >= deadline) {
            LOG_ERROR("startup gate: no calibrated UDP frame in "
                      << timeout.count() << "s; aborting");
            return std::nullopt;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return std::nullopt;
}
```

**插入位置**：在 `driver.emplace(...) + driver->open()` 之后、`safety::Watchdog wdog(...)` 之前。仅 FULL 模式调用（`!args.mock && !args.receiver_only`）。

- `receiver_only`：用户只想看 UDP 解析，不该把 gate 错误掩盖；这里也跑 gate 但 timeout 短不短无所谓，因为它没 driver 可关。**选择仍跑 gate**（一致性 > 一点点诊断便利），实现里 driver 是否 emplace 不影响 gate 调用条件 → 改成 `!args.mock`。
- mock 模式：跳过 gate，沿用 `mock_frame`。

### 3.2 `src/main.cpp` — Stale 反应 + last-cmd cache

伪代码（替换 `main.cpp:339-394` 的 `if (frame_opt) { ... }` 块）：

```cpp
std::optional<std::array<double,12>> last_left_rad, last_right_rad;
bool was_stale = false;
auto last_warn_ts = std::chrono::steady_clock::now() - std::chrono::seconds(10);

while (!shutdown_flag.load() && !duration_elapsed()) {
    auto tick_start = std::chrono::steady_clock::now();
    ++tick;

    std::optional<UdcapFrame> frame_opt;
    if (args.mock) {
        frame_opt = *mock_frame;
        frame_opt->recv_ts = tick_start;
    } else {
        frame_opt = receiver->try_recv();
    }

    if (frame_opt) {
        ++valid_frames;
        wdog.update(frame_opt->recv_ts);

        if (was_stale) {
            using namespace std::chrono;
            auto gap_ms = duration<double, std::milli>(
                tick_start - last_warn_ts).count();
            LOG_INFO("watchdog: recovered after " << gap_ms << "ms");
            was_stale = false;
        }

        // ... 现有 first_packet_logged / mapper / clamp / driver.send_* 代码 ...
        // 在每次成功 send_left 后:
        //     last_left_rad = v;
        // 在每次成功 send_right 后:
        //     last_right_rad = v;

        // ... latency_stats / print 不变 ...
    } else if (driver && wdog.has_seen_frame() && wdog.is_stale(tick_start)) {
        // STALE branch — hold last commanded position
        try {
            if (last_left_rad)  driver->send_left (*last_left_rad);
            if (last_right_rad) driver->send_right(*last_right_rad);
        } catch (const std::exception& e) {
            LOG_ERROR("stale resend: " << e.what());
            shutdown_flag.store(true);
            break;
        }
        using namespace std::chrono;
        if (tick_start - last_warn_ts >= seconds(1)) {
            LOG_WARN("watchdog: no UDP for >"
                     << udcap_cfg.timeout_ms << "ms, holding last position");
            last_warn_ts = tick_start;
        }
        was_stale = true;
    }
    // else: 还没首包（不会发生 — startup gate 保证至少一帧）
    //       或 receiver_only / mock 模式下空帧

    next_tick += period;
    std::this_thread::sleep_until(next_tick);
}
```

**为什么不在 `safety::Watchdog` 内部维护 `last_warn_ts`**：限速是「写日志」这一上层行为，把它塞回 safety 库会让 Watchdog 持有 logger 句柄、变得难单测。让 main.cpp 持有这个状态，单测保持纯 / Watchdog 类不动。

### 3.3 `src/main.cpp` — `UdcapConfig` 加字段

```cpp
struct UdcapConfig {
    std::string host{"0.0.0.0"};
    int port{9000};
    int timeout_ms{200};      // watchdog stale threshold
    int startup_timeout_s{10}; // M6: startup gate (no calibrated frame → abort)
};

UdcapConfig load_udcap_config(const YAML::Node& root) {
    UdcapConfig c;
    auto u = root["udcap"];
    if (u) {
        if (u["host"])              c.host              = u["host"].as<std::string>();
        if (u["port"])              c.port              = u["port"].as<int>();
        if (u["timeout_ms"])        c.timeout_ms        = u["timeout_ms"].as<int>();
        if (u["startup_timeout_s"]) c.startup_timeout_s = u["startup_timeout_s"].as<int>();
    }
    return c;
}
```

### 3.4 `tests/test_safety.cpp` — 单元测试（LOCAL-runnable）

```cpp
// Built only when -DBUILD_TESTS=ON. Does NOT link xhand_control — pure C++
// against safety primitives. Runs on darwin/arm64 host without vendor .so.

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "safety.hpp"

namespace {
int g_failures = 0;

void check(bool cond, const char* expr, const char* file, int line) {
    if (!cond) {
        std::fprintf(stderr, "[FAIL] %s:%d %s\n", file, line, expr);
        ++g_failures;
    }
}
#define CHECK(c) check((c), #c, __FILE__, __LINE__)

void test_watchdog_fresh_is_stale() {
    safety::Watchdog w(std::chrono::milliseconds(200));
    auto now = std::chrono::steady_clock::now();
    CHECK(!w.has_seen_frame());
    CHECK( w.is_stale(now));        // No frame ever → stale by contract
}

void test_watchdog_after_update_not_stale() {
    safety::Watchdog w(std::chrono::milliseconds(200));
    auto t0 = std::chrono::steady_clock::now();
    w.update(t0);
    CHECK( w.has_seen_frame());
    CHECK(!w.is_stale(t0));                                              // 0ms
    CHECK(!w.is_stale(t0 + std::chrono::milliseconds(100)));             // 100ms < 200ms
    CHECK(!w.is_stale(t0 + std::chrono::milliseconds(200)));             // exactly at boundary — '>' in impl
    CHECK( w.is_stale(t0 + std::chrono::milliseconds(201)));             // 201ms > 200ms
    CHECK( w.is_stale(t0 + std::chrono::milliseconds(5000)));            // far past
}

void test_watchdog_update_resets_staleness() {
    safety::Watchdog w(std::chrono::milliseconds(200));
    auto t0 = std::chrono::steady_clock::now();
    w.update(t0);
    CHECK( w.is_stale(t0 + std::chrono::milliseconds(500)));
    w.update(t0 + std::chrono::milliseconds(450));                       // late update
    CHECK(!w.is_stale(t0 + std::chrono::milliseconds(500)));             // 50ms after fresh update
}

void test_clamp_in_place_within_range_noop() {
    std::array<double, 12> v{};
    for (int i = 0; i < 12; ++i) v[i] = 0.1 * i;                         // all small positive rad
    auto orig = v;
    safety::clamp_in_place(v);
    for (int i = 0; i < 12; ++i) CHECK(v[i] == orig[i]);
}

void test_clamp_in_place_clamps_high_low() {
    std::array<double, 12> v{};
    v[0] = 100.0;                                                        // way above kHardMaxRad ≈ 1.9199
    v[1] = -100.0;                                                       // way below kHardMinRad ≈ -1.5708
    v[2] = safety::kHardMaxRad;                                          // exactly upper — should remain
    v[3] = safety::kHardMinRad;                                          // exactly lower — should remain
    safety::clamp_in_place(v);
    CHECK(std::fabs(v[0] - safety::kHardMaxRad) < 1e-12);
    CHECK(std::fabs(v[1] - safety::kHardMinRad) < 1e-12);
    CHECK(std::fabs(v[2] - safety::kHardMaxRad) < 1e-12);
    CHECK(std::fabs(v[3] - safety::kHardMinRad) < 1e-12);
    for (int i = 4; i < 12; ++i) CHECK(v[i] == 0.0);
}
}  // namespace

int main() {
    test_watchdog_fresh_is_stale();
    test_watchdog_after_update_not_stale();
    test_watchdog_update_resets_staleness();
    test_clamp_in_place_within_range_noop();
    test_clamp_in_place_clamps_high_low();
    std::fprintf(stderr, "[test_safety] %s (failures=%d)\n",
                 g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
```

**为什么不引 GoogleTest / Catch2**：项目里现有 `tests/test_mapper_snapshot.cpp` 也是手卷断言风格（fprintf + g_failures 计数），保持一致；M6 不引新依赖。

### 3.5 `CMakeLists.txt` — 测试 target 增量

在现有 `if(BUILD_TESTS)` 块（CMakeLists.txt:56-68）末尾追加：

```cmake
    add_executable(test_safety
        tests/test_safety.cpp
        src/safety.cpp)
    target_include_directories(test_safety PRIVATE src)
    target_compile_options(test_safety PRIVATE -Wall -Wextra -Wpedantic)
    # 不链 xhand_control / yaml-cpp / nlohmann_json / OpenSSL — safety 是纯 C++。
```

不动 `find_package(xhand_control)` —— 那条在顶层，找 `xhand_controlConfig.cmake` 在 darwin/arm64 上同样成功（只导入 IMPORTED target，不触发链接）。只要不 `--target udex_to_xhand`，LOCAL Mac 不会尝试链接 aarch64 Linux `.so`。

---

## 4. 测试策略（具体命令 + 预期）

### 4.1 LOCAL（darwin/arm64 Mac — 可立即跑）

#### Test L1 · build + unit test

```bash
# [LOCAL] 在仓库根。前置：brew install cmake nlohmann-json yaml-cpp openssl@3 （M5b 已装过）
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build . --target test_safety test_mapper_snapshot
./test_safety
./test_mapper_snapshot
```

**预期**：
- `cmake ..` 输出含 `Found nlohmann_json` / `Found yaml-cpp` / `Found xhand_control`（找到 cmake config 文件即可，不需要能加载 .so）。
- `cmake --build . --target test_safety test_mapper_snapshot` 干净通过；`-Wall -Wextra -Wpedantic` 无新警告。
- `./test_safety` 末行：`[test_safety] PASS (failures=0)`；exit 0。
- `./test_mapper_snapshot`：照 M5b 输出 max |Δ| ≤ 1e-6 rad；exit 0。

**不在 LOCAL 跑**：`cmake --build . --target udex_to_xhand` 会试图链 `libxhand_control.so`（aarch64 Linux ELF），darwin/arm64 上必失败。这是 PC2 段任务。

#### Test L2 · 代码静态自查

```bash
# [LOCAL] grep 自检 — 确认 §3.2 的关键修改全落地
grep -n "wait_first_valid_frame"   src/main.cpp
grep -n "last_left_rad\|last_right_rad" src/main.cpp
grep -n "is_stale"                  src/main.cpp
grep -n "watchdog: recovered\|watchdog: no UDP" src/main.cpp
grep -n "startup_timeout_s"         src/main.cpp config.yaml
```

**预期**：每行 grep 都至少 1 hit。否则说明实现漏了某条规约。

### 4.2 PC2（aarch64 Ubuntu — 用户回到机器跟前后跑）

> 准备工作（每次会话）：参考 M5c §5.2。简言之：登陆 PC2 → `cd ~/udex_to_xhand` → `git pull` → 确认 `/dev/ttyACM0` 存在 + 用户在 `dialout` 组 → Windows 上 UDCAP HandDriver 已经发包到 PC2 的 UDP 9000（除非测试本身要求不发包）。

#### Test P0 · build（前置；所有后续场景都依赖）

```bash
# [PC2]
cd ~/udex_to_xhand
rm -rf build && mkdir build && cd build
cmake .. -DBUILD_TESTS=ON 2>&1 | tee ../docs/logs/m6-build-2026-05-19.log
make -j$(nproc)                     2>&1 | tee -a ../docs/logs/m6-build-2026-05-19.log
./test_safety                       2>&1 | tee -a ../docs/logs/m6-build-2026-05-19.log
./test_mapper_snapshot              2>&1 | tee -a ../docs/logs/m6-build-2026-05-19.log
ls -l udex_to_xhand
```

**预期**：
- `udex_to_xhand` 是 aarch64 ELF；`-Wall -Wextra -Wpedantic` 无警告。
- 两个测试 binary 均 exit 0；log 末尾各有一行 `[test_xxx] PASS`。

#### Test P1 · Watchdog stale 反应（roadmap §M6 测试 1）

**步骤**（在 PC2 上跑；UDCAP 在 Windows 上已发包）：
```bash
# [PC2] 终端 A
./udex_to_xhand --config ../config.yaml --hand left --duration 20 \
    2>&1 | tee ../docs/logs/m6-watchdog-2026-05-19.log
```
**操作**：binary 启动后等 ~3s（看到 `first packet from 192.168.x.x` 与几条 `latency_ms` 后），到 Windows 上**关闭 UDCAP HandDriver**。维持 ~10s。再重开 UDCAP，等几秒。让 binary 自然跑完 20s duration 退出。

**预期**：
- 启动段：`[INFO] waiting for first calibrated UDP frame (timeout=10s)...` → `[INFO] first packet from 192.168.x.x`。
- UDCAP 关闭后 ~200ms 内出现第一条 `[WARN] watchdog: no UDP for >200ms, holding last position`。
- stale 期间 **每秒最多 1 条** 该 WARN（不应每 10ms 一条；如出现刷屏即 ADR-035 限速器实现错）。
- 重开 UDCAP 后一行 `[INFO] watchdog: recovered after Nms`（N 约等于 UDCAP 停发的毫秒数）。
- **物理观测**（眼看 + 手摸 XHand）：stale 期间 XHand 保持最后姿态；不松弛、不漂移。
- 退出后摘要 `latency_ms{n=... ...}` 仍然有数（stale 期间不计 latency 样本，§3.2 已规定）。

#### Test P2 · Ctrl+C graceful shutdown（roadmap §M6 测试 2）

```bash
# [PC2] 终端 A
./udex_to_xhand --config ../config.yaml --hand left \
    2>&1 | tee ../docs/logs/m6-sigint-2026-05-19.log
# 等 ~5s（看到几条 latency 行），按 Ctrl+C
```

**预期**：
- 终端可见 `^C`；接着 `[INFO] Shutdown: mode=0 (passive)` + `[INFO] Device closed`；最后摘要行 + `exited after Xs, ...`。
- exit code 0。
- **物理观测**：XHand 手指松弛（mode=0 表现；ADR-018 提醒：不是断电）。

#### Test P2b · SIGTERM graceful shutdown（roadmap §M6 测试 2b）

```bash
# [PC2] 终端 A
./udex_to_xhand --config ../config.yaml --hand left \
    > ../docs/logs/m6-sigterm-2026-05-19.log 2>&1 &
PID=$!
sleep 5
kill -TERM $PID
wait $PID; echo "exit=$?"
```

**注意**：M5c §6.6 用 `kill -TERM $!→tee` 撞过 stdout 重定向截断。这里改成 `> log 2>&1 &` 加 `wait` —— stdout/stderr 直接进文件，避免 `tee` 在子进程被 TERM 后才感知到 EOF 导致末尾几行漏写。

**预期**：
- log 末尾出现完整的 `[INFO] Shutdown: mode=0 (passive)` + `[INFO] Device closed` + `[INFO] exited after ~5.0s, ...`。
- `echo "exit=$?"` 出 0（SIGTERM 被主循环捕获后正常退出，不是被信号杀死）。
- **物理观测**：与 Ctrl+C 一致，手指松弛。

#### Test P3 · Clamp 验证（roadmap §M6 测试 3）

**前置**：临时改 `config.yaml` —— 把 left.index_joint1 的 clamp 从 `[0, 110]` 收窄到 `[0, 30]`（**只**这一行；其他不动）。改完不要 commit。

```bash
# [PC2]
cp ../config.yaml ../config.yaml.bak                       # 保险
sed -i 's/index_joint1:.*clamp: \[0, 110\]/index_joint1:  { sources: [6],       weights: [1.0],            sign: -1, offset: 0, clamp: [0, 30] }/' ../config.yaml
# 也可以手 edit；确认 left.index_joint1.clamp = [0, 30]
grep -A0 "left:" -n ../config.yaml | head -5

./udex_to_xhand --config ../config.yaml --hand left --duration 15 \
    2>&1 | tee ../docs/logs/m6-clamp-2026-05-19.log
```

**操作**：戴 UDCAP 左手，把食指弯到底（>60° 物理弯曲），保持 ~3 秒；放开。重复 2 次。

**预期**：
- 终端 latency 数据正常滚动；无 WARN。
- **物理观测**：左 XHand 食指 J4（index_joint1，即 MCP proximal flexion）在你把手套食指弯到底时**停在约 30° 不再继续弯**；其余手指（J5 index distal、J6/7 mid 等）跟随正常。
- duration 结束自然退出。

**收尾**：
```bash
# [PC2] 恢复 config
mv ../config.yaml.bak ../config.yaml
git diff -- ../config.yaml          # 预期：空（已恢复）
```

#### Test P5 · Startup gate timeout（M6 新增）

**前置**：**确认 UDCAP 没在发包**（Windows 上关掉 HandDriver；或拔网线；或用 `nc -ul 9999` 占住一个错误端口反向验证）。

```bash
# [PC2]
time ./udex_to_xhand --config ../config.yaml --hand left \
    2>&1 | tee ../docs/logs/m6-startup-gate-2026-05-19.log
echo "exit=$?"
```

**预期**：
- 启动出 `[INFO] waiting for first calibrated UDP frame (timeout=10s)...`。
- 约 10 秒后出 `[ERROR] startup gate: no calibrated UDP frame in 10s; aborting`。
- log 末尾出现 `[INFO] Shutdown: mode=0 (passive)` + `[INFO] Device closed`（driver 析构路径触发）。
- `time` 报 real ≈ 10s（±0.5s 抖动可接受；明显 <8s 或 >12s 说明 timeout 没读对，需排查 `load_udcap_config`）。
- `echo "exit=$?"`: 2（与 cli 错误同 exit code，参 `main.cpp:217` 现状；ADR-036 锁定）。
- **物理观测**：XHand 在 driver.open 后没收到任何 send_command，电机不动；析构发 mode=0 后手指松弛。

### 4.3 测试矩阵（速查）

| Test | 地点  | 触发                          | 期望 binary 行为                       | log 落盘                                 |
| ---- | ----- | ----------------------------- | -------------------------------------- | ---------------------------------------- |
| L1   | LOCAL | `./test_safety`               | exit 0, PASS                           | stdout                                   |
| L2   | LOCAL | grep                          | 5 grep 全 hit                          | shell                                    |
| P0   | PC2   | `make` + unit tests           | binary 生成 + 测试 PASS                | `m6-build-2026-05-19.log`                |
| P1   | PC2   | 中途关 UDCAP                  | stale WARN @ 1Hz，hold；recover 行     | `m6-watchdog-2026-05-19.log`             |
| P2   | PC2   | Ctrl+C                        | mode=0 + close + exit 0                | `m6-sigint-2026-05-19.log`               |
| P2b  | PC2   | `kill -TERM`                  | 等价 P2，exit 0                        | `m6-sigterm-2026-05-19.log`              |
| P3   | PC2   | config clamp 收窄到 [0,30]    | 食指 J4 停在 30°                       | `m6-clamp-2026-05-19.log`                |
| P5   | PC2   | UDCAP 不发包                  | 10s 超时退出，exit 2                   | `m6-startup-gate-2026-05-19.log`         |

---

## 5. 验证命令（汇总）

> 把这一节当 cheat-sheet：照抄即可。所有 `[PC2]` 段在用户回到 PC2 后跑。

### 5.1 LOCAL（Mac，现在即可执行）

```bash
# [LOCAL] 仓库根
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON
cmake --build . --target test_safety test_mapper_snapshot
./test_safety        # 预期: [test_safety] PASS (failures=0)
./test_mapper_snapshot
cd ..

# 静态自查
grep -n "wait_first_valid_frame"        src/main.cpp
grep -n "last_left_rad\|last_right_rad" src/main.cpp
grep -n "is_stale"                       src/main.cpp
grep -n "watchdog: recovered\|watchdog: no UDP" src/main.cpp
grep -n "startup_timeout_s"              src/main.cpp config.yaml
```

### 5.2 PC2（用户后续在 G1 PC2 上跑）

```bash
# [PC2] 准备
cd ~/udex_to_xhand
git pull                                          # 拉本 plan + §1.2 修改
ls /dev/ttyACM0                                   # 必须存在
groups | grep -q dialout && echo OK || echo "MISSING dialout"

# [PC2] P0: build + unit
rm -rf build && mkdir build && cd build
cmake .. -DBUILD_TESTS=ON 2>&1 | tee ../docs/logs/m6-build-2026-05-19.log
make -j$(nproc)                    2>&1 | tee -a ../docs/logs/m6-build-2026-05-19.log
./test_safety                      2>&1 | tee -a ../docs/logs/m6-build-2026-05-19.log
./test_mapper_snapshot             2>&1 | tee -a ../docs/logs/m6-build-2026-05-19.log

# [PC2] P1: watchdog（操作：3s 后到 Windows 关 UDCAP，10s 后重开）
./udex_to_xhand --config ../config.yaml --hand left --duration 20 \
    2>&1 | tee ../docs/logs/m6-watchdog-2026-05-19.log

# [PC2] P2: SIGINT
./udex_to_xhand --config ../config.yaml --hand left \
    2>&1 | tee ../docs/logs/m6-sigint-2026-05-19.log
# 5s 后按 Ctrl+C

# [PC2] P2b: SIGTERM（注意背景化 + wait 取代 tee）
./udex_to_xhand --config ../config.yaml --hand left \
    > ../docs/logs/m6-sigterm-2026-05-19.log 2>&1 &
PID=$!; sleep 5; kill -TERM $PID; wait $PID; echo "exit=$?"

# [PC2] P3: clamp（先备份 config）
cp ../config.yaml ../config.yaml.bak
# 手动 edit ../config.yaml 把 left.index_joint1 的 clamp 改成 [0, 30]
./udex_to_xhand --config ../config.yaml --hand left --duration 15 \
    2>&1 | tee ../docs/logs/m6-clamp-2026-05-19.log
# 戴手套食指弯到底，观察食指 J4 是否停在 30°
mv ../config.yaml.bak ../config.yaml
git diff -- ../config.yaml         # 必须空

# [PC2] P5: startup gate（前置：Windows UDCAP 关闭）
time ./udex_to_xhand --config ../config.yaml --hand left \
    2>&1 | tee ../docs/logs/m6-startup-gate-2026-05-19.log
echo "exit=$?"                     # 必须 2
```

### 5.3 回归核对（PC2 全跑完后）

```bash
# [PC2] 确认所有 6 个 log 文件都已落盘
ls -l docs/logs/m6-*-2026-05-19.log
# 期望: 6 个文件 — build / watchdog / sigint / sigterm / clamp / startup-gate

# [PC2] commit + push
git status
git add src/ tests/ CMakeLists.txt config.yaml docs/
git commit                                    # 见 §6 commit 范本
git push
```

---

## 6. Commit 计划（按顺序）

1. **`M6: add Watchdog stale reaction + startup gate (+ unit tests)`** — `src/main.cpp` + `config.yaml` + `tests/test_safety.cpp` + `CMakeLists.txt` + `src/safety.hpp` 注释。验证：LOCAL `test_safety` PASS。
2. **`M6: ADR-035 watchdog-stale-resend, ADR-036 startup-gate-timeout`** — 两个 ADR 文件。
3. **`M6: PC2 fault-injection logs (watchdog / sigint / sigterm / clamp / startup-gate)`** — `docs/logs/m6-*-2026-05-19.log` 7 个。
4. **`M6 ✅: safety hardening verified on G1 PC2`** — `docs/plans/00-roadmap.md` (Revision History + §M6 ✅) + `SPEC.md` (§5 stale 注释 + §11 startup_timeout_s) + 本 plan §8 Execution Record。

每个 commit 都附 `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>`。

---

## 7. ADR 候选（执行前预登记；落实在跑完后定稿）

- **ADR-035: Watchdog stale reaction — resend last cmd @ 100Hz, log @ 1Hz.** 替代方案与拒绝原因：(a) 停发 → 电机 hold 行为 SDK 未明确文档化，不冒险；(b) 100Hz 全打 LOG_WARN → 30 分钟产 180k 行，操作员看不过来。
- **ADR-036: Startup gate timeout default = 10s, abort with exit 2 if exceeded.** 为什么不是 ∞ / 不是 1s：1s 装备开机过程中合法网络初始化都不止；∞ 会让 misconfig（错 IP / 防火墙）变成"binary 跑着但没反应"的最坏 UX。10s 是 UDCAP HandDriver 冷启动 + 静态 IP 协商的工程上限；操作员可以通过 `udcap.startup_timeout_s` 单点调整。

> 如果跑完 §4 PC2 测试过程中发现 **3 项**以上非显然决定（参 ADR-024 ~ 034 的密度），考虑再加 ADR-037+。

---

## 8. Execution Record（filled at end of M6）

> 跑完 §4 + §5 后回填本节，沿用 M5c §11 风格。先留 placeholder，PR 不带数据不合并。

### 8.1 Environment
- PC2 hostname / kernel / SDK 版本 / `git rev-parse HEAD`：__TBD__
- LOCAL hostname / cmake / clang or gcc 版本：__TBD__

### 8.2 LOCAL results
- `cmake .. -DBUILD_TESTS=ON`：__pass/fail__ + 警告条目（应为 0）
- `./test_safety`：__pass/fail__ + 失败用例（应为空）
- `./test_mapper_snapshot`：__pass/fail__（M5b 已通过；本次为回归确认）

### 8.3 PC2 results — 逐场景

| Test | exit | log path                                  | 关键摘要（latency / WARN 次数 / 物理观测）         |
| ---- | ---- | ----------------------------------------- | -------------------------------------------------- |
| P0   | __  | `docs/logs/m6-build-2026-05-19.log`       | 无警告 / unit PASS                                 |
| P1   | __  | `docs/logs/m6-watchdog-2026-05-19.log`    | stale 期间 WARN 次数 = __；recovered Nms = __     |
| P2   | __  | `docs/logs/m6-sigint-2026-05-19.log`      | latency avg/p95 = __；mode=0 行 = __              |
| P2b  | __  | `docs/logs/m6-sigterm-2026-05-19.log`     | 同上；log 完整无 tee 截断                          |
| P3   | __  | `docs/logs/m6-clamp-2026-05-19.log`       | 食指 J4 物理停在 __° （目测）                      |
| P5   | __  | `docs/logs/m6-startup-gate-2026-05-19.log`| time real = __s；exit = 2                          |

### 8.4 Deviations from plan
- __（如有）__

### 8.5 Follow-ups / 新 ADR / 风险登记更新
- __（如有）__

---

## 9. Risks / Open Questions

| #   | 风险 / 问题                                                                                                                                                | 概率   | 影响   | 缓解 / 验证点                                                                                                              |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | ------ | -------------------------------------------------------------------------------------------------------------------------- |
| R1  | Stale 期间持续 send 让 SDK 累积错误（CRC / busy）—— M5c 已观察 2 次 startup CRC，ADR-017 是 log-not-crash                                                  | LOW    | MEDIUM | P1 log 里数 `[WARN] send_command(...)` 行数；若 stale 10s 内出现 >5 次 SDK error，留 ADR 记录 + M7 重评                  |
| R2  | `std::signal` 在 binary 持有 vendor SDK 内创建的线程时的 reentrancy —— vendor SDK 是否在 background 线程持锁不可知                                          | LOW    | HIGH   | M5c SIGTERM 视觉验证通过；本次 P2/P2b 必须看到 `mode=0` + `Device closed` 两行同时出现；任何一行缺失即升级 ADR / 退回 sigaction |
| R3  | Startup gate 超时单位（秒 vs 毫秒）写错 —— config 里是 `_s` 后缀，但代码若漏用 `seconds{}` 而用 `milliseconds{}` 会导致 10ms 立刻 timeout                  | LOW    | LOW    | LOCAL `time ./udex_to_xhand ...` 在 P5 必须 ≈ 10s；明显 <1s 即说明类型搞错                                                |
| R4  | P3 用 `sed` 改 yaml 失败（yaml-cpp 对 `{}` 行内字典的解析是宽松的，但 sed 模式可能漏匹配）                                                                  | MEDIUM | LOW    | 改用手 edit；plan §4.2 / §5.2 的 sed 命令在文档里只作示意，操作者优先用 editor                                              |
| R5  | UDCAP 在 P1 重开后第一帧可能 calib==2 → receiver 丢；从用户角度看会以为 "recovered 行延迟出现"                                                            | LOW    | LOW    | log 里要看 receiver 的 valid_frames 数和 `parse_errors`，必要时让 UDCAP 充分热身后再操作                                  |
| R6  | 本 plan 作者 ≠ PC2 操作者 —— 命令拼接漏字符 / shell 量化错误                                                                                                | LOW    | LOW    | §5 段所有命令均经 ChatGPT/Gemini reviewer 复核；P0 失败立即停，不要硬跑 P1+                                              |

---

## 10. 参考链接

- 上游 roadmap：`docs/plans/00-roadmap.md#m6-safety-layer-c`
- SPEC §5 Safety Mechanisms：`SPEC.md:194-203`
- ADR-018 (mode=0 not powerless)：`docs/decisions/018-mode0-does-not-achieve-powerless.md`
- ADR-021 (two-layer clamping)：`docs/decisions/021-two-layer-clamping-defense-in-depth.md`
- ADR-023 (SIGTERM as same path as SIGINT)：`docs/decisions/023-roadmap-pivot-g1-pc2-as-host.md`
- ADR-029 (CalibrationStatus precheck UDCAP-only)：`docs/decisions/029-calibrationstatus-precheck-udcap-side-only.md`
- M5b plan §3 (safety primitives wiring)：`docs/plans/20260518-m5b-cpp-rewrite-plan.md`
- M5c plan §3.3 + §6.6 (SIGTERM 视觉验证经验)：`docs/plans/20260518-m5c-pc2-hardware-revalidation-plan.md`
