# M5b — Project C++ Rewrite (Python prototype → `udex_to_xhand` ELF aarch64)

| Field                | Value                                                                                                                                                                                            |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Date                 | 2026-05-18 (planned)                                                                                                                                                                             |
| Milestone            | M5b (subset of M5; see [00-roadmap.md §M5](./00-roadmap.md))                                                                                                                                     |
| Prereqs              | M5a ✅ (vendor SDK + .so + LEFT XHand verified on PC2 — commit `7275a1a`)                                                                                                                        |
| Spec refs            | [SPEC.md §2 / §7 / §8 / §9.1.5 / §12 Q10](../../SPEC.md), [CLAUDE.md "Architecture" + "Code conventions"](../../CLAUDE.md)                                                                       |
| Status               | Draft — implementation pending                                                                                                                                                                   |
| Scope                | Author + build a **single C++17 binary `udex_to_xhand`** that functionally replaces `main.py`+`udcap_receiver.py`+`joint_mapper.py`+`xhand_driver.py`+`safety.py`. **No hardware run** in M5b — `--mock` + `--receiver-only` cover M5b acceptance; hardware re-validation is M5c. |
| Where each step runs | **Dev Mac (this box)**: write code, run Python snapshot generator, git push. **PC2 (aarch64 Linux)**: cmake + make + `--mock` run + `--receiver-only` run + snapshot equivalence test.            |
| Non-obvious ADRs (to draft) | M5b: 1 ADR on **pure C++ rewrite vs pybind11/ctypes binding** (already pre-flagged in roadmap §M5 "待新增 ADR"). Other M5b decisions captured as ADRs only if they survive review (see §10). |

> **What M5b proves**: code compiles to ELF aarch64 on PC2, links against vendor `libxhand_control.so`, and at `--mock` mode produces the same 12-joint per-hand output as the Python M4 baseline (algorithmic parity). What it does **not** prove: real-time latency on hardware, UDP under load, dual-hand, or safety behavior under fault injection — those are M5c / M6 / M7.

---

## 0. Why C++ rewrite (and not a Python binding) — preamble

Pre-flagged in roadmap §M5 ("待新增 ADR"). Recap rationale so reviewers can challenge it before any code lands:

1. **Vendor ships C++17 only on aarch64** — `xhand_control_sdk/lib/libxhand_control.so` + headers. No Python wheel exists for aarch64 (ADR-023, roadmap revision 2). A binding (pybind11 / ctypes / cffi) would be net-new code we have to maintain.
2. **100 Hz realtime loop sits inside the FFI boundary** in any binding option — every cycle would cross Python ⇄ C++. CLAUDE.md "Constraints — do NOT" §`Do NOT add a pybind11 / ctypes / cffi layer` already encodes this; M5b is the milestone that *enforces* it by deleting the Python runtime path.
3. **In-tree reference is already C++** (`xhand_control_ros2/.../xhand_control_ros2.hpp:68-72`). Keeping the language baseline aligned avoids "two flavors of joint enum drift" later.
4. **Python prototype is preserved as `.py` files** until M5c ratifies the rewrite (SPEC.md §8). They're read-only oracles for the snapshot equivalence test in §7.

Decision is captured as **ADR-028** at the end of M5b (after build proves the cost estimate is real, not before — ADRs document made decisions, not plans).

---

## 1. 文件清单

### 1.1 新增 — committed by M5b

| Path                                          | One-line responsibility                                                                                                                  |
| --------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `docs/plans/20260518-m5b-cpp-rewrite-plan.md` | **This file** — frozen plan + execution record skeleton. Filled at end of M5b, then committed together with the source tree.             |
| `CMakeLists.txt` (top-level)                  | cmake ≥ 3.10, C++17, `find_package(xhand_control HINTS xhand_control_sdk/share)`, `find_package(yaml-cpp REQUIRED)`, `find_package(nlohmann_json 3 REQUIRED)`, `find_package(CURL REQUIRED)`. Builds one target: `udex_to_xhand`. Mirrors vendor `tests/CMakeLists.txt` link options (`-Wl,--disable-new-dtags`) so RPATH to `xhand_control_sdk/lib/` survives. |
| `src/main.cpp`                                | Entry point. Parses CLI, loads config, wires receiver→mapper→driver→safety, runs the ~100 Hz loop. Handles `SIGINT`/`SIGTERM` (ADR-023). |
| `src/udcap_receiver.hpp` + `src/udcap_receiver.cpp` | Non-blocking UDP socket; nlohmann_json parse; extract `l0`-`l23` + `r0`-`r23`; drop frame on parse error / `CalibrationStatus != 3` (SPEC §5).            |
| `src/joint_mapper.hpp` + `src/joint_mapper.cpp` | Parses `config.yaml` once; per-cycle applies weighted-sum + sign + offset + clamp (degree domain, ADR-020) + deg→rad (ADR-021). Pure function, no IO. |
| `src/xhand_driver.hpp` + `src/xhand_driver.cpp` | Thin wrapper over `xhand_control::XHandControl` — `open_serial` → `list_hands_id` → `get_hand_type` → `send_command` → `close_device`. Supports 1 or 2 hands (M7 keeps the interface stable). |
| `src/safety.hpp` + `src/safety.cpp`           | Watchdog (200 ms no-UDP → hold last command), fail-safe per-joint clamp re-check (ADR-021 two-layer), signal handler glue.               |
| `src/cli.hpp` + `src/cli.cpp`                 | Hand-rolled flag parser (no external dep — keep CLAUDE.md "Do NOT add abstractions beyond what the task requires"). One file so `main.cpp` stays focused.       |
| `src/logging.hpp`                             | Header-only thin wrapper: `LOG_INFO("...")` → `std::cerr << "[INFO] " << ...`. No spdlog dep. Used by all modules.                       |
| `tests/fixtures/mapper_baseline.json`         | One-shot snapshot of `joint_mapper.py` output on `example.json` + `config.yaml`. **Generated on dev Mac** by `scripts/dump_mapper_baseline.py`, then committed as fixture. Consumed by snapshot test. |
| `tests/test_mapper_snapshot.cpp`              | Standalone executable. Loads fixture + runs C++ `JointMapper` on the same input + asserts `‖cpp - python‖∞ < 1e-6` per joint. Built only with `-DBUILD_TESTS=ON`. |
| `scripts/dump_mapper_baseline.py`             | New Python helper. Reads `example.json` + `config.yaml`, runs `legacy_python/joint_mapper.py`, writes `tests/fixtures/mapper_baseline.json`. Run **once on dev Mac**, output committed. |
| `docs/logs/m5b-mock-run-2026-05-18.log`       | Captured stdout from `./udex_to_xhand --mock --duration 3` on PC2.                                                                       |
| `docs/logs/m5b-snapshot-test-2026-05-18.log`  | Captured stdout from `./test_mapper_snapshot` on PC2.                                                                                    |
| `docs/decisions/028-pure-cpp-rewrite-not-binding.md` | ADR: rewrite reasons (see §0). Written *after* M5b builds, so the cost estimate (compile time / LOC / dev effort) reflects reality.       |

### 1.2 修改 — committed by M5b

| Path           | Change                                                                                                                                                                              |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CLAUDE.md`    | "Migration status" paragraph: change "Until M5b lands, the Python files remain as reference" → "M5b lands the C++ runtime stack as authoritative; Python files now reference-only." |
| `SPEC.md`      | §8 File Structure: mark `*.py` lines as "**Reference only post-M5b. Authoritative implementations in `src/`.**" §12 Q10 (C++/Python equivalence): mark resolved with link to snapshot fixture + test. |
| `.gitignore`   | No changes needed — `build/` already covered; `tests/fixtures/` and `docs/logs/` are NOT under ignore patterns (latter has an explicit negation rule from M5a).                      |

### 1.3 Reorganized — moved to `legacy_python/` (post-implementation reorg, user-directed 2026-05-18)

`legacy_python/{main,udcap_receiver,joint_mapper,xhand_driver,safety,test_udcap_connection}.py` — all 6 moved from repo root into `legacy_python/` via `git mv` as part of the M5b commit. Rationale for keeping the files (unchanged from the original §1.3):

- **Snapshot oracle**: `legacy_python/joint_mapper.py` is what `tests/fixtures/mapper_baseline.json` is generated from. Deleting it severs the oracle.
- **Reviewer cross-reference**: ChatGPT/Gemini reviewers and future humans will want to diff "what the Python did" vs "what the C++ does" to validate parity. Easier when both live in the tree.
- **Deletion is reversible-by-revert but the loss of the trial-and-error history isn't** — keeping them costs ~30 KB and clarifies intent.

Minor edits applied during the move (so standalone runs from repo root continue to work):
- `legacy_python/udcap_receiver.py`: `os.path.dirname(__file__)` → `os.path.join(os.path.dirname(__file__), "..")` for `example.json` lookup.
- `legacy_python/joint_mapper.py`: same `..` adjustment for `example.json` + `config.yaml` lookups in the `__main__` block.
- `scripts/dump_mapper_baseline.py`: sys.path now points at `legacy_python/` for `from joint_mapper import …`.

> **Plan deviation vs original §1.3**: the first draft said "stay in repo root" with removal post-M5c. User overrode 2026-05-18 to do the folder move *during* M5b for cleaner project structure. Removal remains a post-M5c candidate. See `legacy_python/README.md`.

### 1.4 不修改 (read-only inputs)

- `config.yaml` — M2/M4 verified data. Re-used as-is by the C++ mapper.
- `example.json` — M0 reference UDCAP packet. Re-used as-is by `--mock` + the snapshot generator.
- `xhand_control_sdk/` — vendor tree, ADR-025 keeps it pristine.
- All ADRs 001–027.

---

## 2. 数据流

```
                       ┌─────────────────────────── Dev Mac (this box) ─────────────────────────┐
                       │                                                                         │
[edit src/*.cpp,       │   (1) python3 scripts/dump_mapper_baseline.py                           │
 CMakeLists.txt,       │        ↳ reads:  example.json + config.yaml                             │
 fixtures, etc.]       │        ↳ writes: tests/fixtures/mapper_baseline.json                    │
                       │                                                                         │
                       │   (2) git add . && git commit && git push origin <m5b-branch>           │
                       └─────────────────────────────┬───────────────────────────────────────────┘
                                                     │
                                                     ↓
                       ┌───────────────────── G1 PC2 (aarch64 Linux) ──────────────────────┐
                       │                                                                    │
                       │   (3) git pull --ff-only                                           │
                       │                                                                    │
                       │   (4) mkdir -p build && cd build && cmake -DBUILD_TESTS=ON ..      │
                       │       make -j$(nproc)                                              │
                       │       → ./udex_to_xhand        (ELF aarch64, links libxhand…so)    │
                       │       → ./test_mapper_snapshot (ELF aarch64, no xhand link)        │
                       │                                                                    │
                       │   (5) ./test_mapper_snapshot                                       │
                       │        ↳ loads tests/fixtures/mapper_baseline.json                 │
                       │        ↳ asserts C++ mapper matches Python output                  │
                       │                                                                    │
                       │   (6) ./udex_to_xhand --mock --duration 3                          │
                       │        ↳ reads example.json on loop (no UDP socket)                │
                       │        ↳ prints L/R 12-joint vectors at ~100 Hz                    │
                       │        ↳ no /dev/ttyACM open (mock skips xhand_driver)             │
                       │                                                                    │
                       │   (7) ./udex_to_xhand --config ../config.yaml --receiver-only      │
                       │            --duration 10                                           │
                       │        ↳ opens UDP socket on port 9000                             │
                       │        ↳ prints raw l0-l23 / r0-r23 + CalibrationStatus            │
                       │        ↳ no XHand open — exercises receiver + parse only          │
                       │       (requires Windows UDCAP HandDriver to be sending; see §6)    │
                       │                                                                    │
                       └────────────────────────────────────────────────────────────────────┘
```

Runtime control loop (steady state, full mode — exercised in M5c, not M5b):

```
main() loop @ 100 Hz, while !shutdown_requested:
    1. udcap_receiver.try_recv()  →  Frame{l0..l23, r0..r23, calib_l, calib_r, ts}
       on parse error or calib != 3: skip frame
    2. joint_mapper.map_left(frame.l)   →  std::array<double, 12> left_rad
       joint_mapper.map_right(frame.r)  →  std::array<double, 12> right_rad
    3. safety.clamp(left_rad);  safety.clamp(right_rad);     // ADR-021 fail-safe layer
    4. safety.watchdog_tick(frame.ts) — if stale: replace with last_good
    5. xhand_driver.send_left(left_rad);  xhand_driver.send_right(right_rad);
    6. sleep_until(next_tick)
On signal:
    safety.shutdown() → xhand_driver.send(mode=0) → xhand_driver.close()
```

---

## 3. 模块规约 (API + behavior + test points)

Each module is small enough to keep the .hpp under ~40 LOC. CLAUDE.md "no abstractions beyond what the task requires" applies.

### 3.1 `udcap_receiver`

```cpp
struct UdcapFrame {
    std::array<double, 24> l;      // degrees, UDCAP convention (negative = flexion)
    std::array<double, 24> r;
    int calib_left;                // 0..3
    int calib_right;
    std::chrono::steady_clock::time_point recv_ts;
};

class UdcapReceiver {
public:
    UdcapReceiver(const std::string& bind_host, uint16_t port);   // creates non-blocking socket
    ~UdcapReceiver();                                              // closes socket
    std::optional<UdcapFrame> try_recv();                          // nullopt on EAGAIN / parse err / calib!=3
private:
    int fd_;
    std::string parse_buf_;
};
```

- Non-blocking via `fcntl(fd, F_SETFL, O_NONBLOCK)`.
- On `recvfrom` EAGAIN: return `nullopt` immediately (no busy wait — main loop's sleep_until handles pacing).
- Drains socket on each call: keeps only **latest** packet (drop older buffered ones). Matches Python prototype.
- JSON shape from `example.json`: `{"Parameter": [...24 doubles per hand...], "CalibrationStatus": int, …}`. Validate length == 24 per hand; mismatch → drop frame + `LOG_WARN`.

Test points (covered by `--mock` + manual `--receiver-only`):
- `--mock` mode: receiver is **bypassed**; `main` reads `example.json` from disk directly. (Mock = no UDP socket. Avoids needing a "fake UDP sender" fixture.)
- `--receiver-only`: prints raw 48 doubles + calib status. Visual comparison against UDCAP HandDriver window.

### 3.2 `joint_mapper`

```cpp
struct JointConfig {
    std::vector<int>    sources;   // UDCAP indices
    std::vector<double> weights;   // same length as sources, sum ~ 1.0
    int                 sign;      // +1 or -1
    double              offset;    // degrees
    double              clamp_min; // degrees
    double              clamp_max; // degrees
};

class JointMapper {
public:
    explicit JointMapper(const std::string& yaml_path);
    std::array<double, 12> map_left (const std::array<double, 24>& src) const;
    std::array<double, 12> map_right(const std::array<double, 24>& src) const;
private:
    std::array<JointConfig, 12> left_;
    std::array<JointConfig, 12> right_;
    static double apply_one(const JointConfig& jc, const std::array<double, 24>& src);
};
```

- Joint order **must** match Python `JOINT_ORDER` (ADR-022): `thumb_bend, thumb_rota1, thumb_rota2, index_bend, index_joint1, index_joint2, mid_joint1, mid_joint2, ring_joint1, ring_joint2, pinky_joint1, pinky_joint2`. Encoded as a `static constexpr` array in `joint_mapper.cpp`.
- `apply_one`:
  ```
  acc = sum(weights[i] * src[sources[i]])  for i in 0..N
  v   = sign * acc + offset                # degrees
  v   = clamp(v, clamp_min, clamp_max)     # ADR-020: clamp in degree domain
  return v * (M_PI / 180.0)                # ADR-021: convert once, at the boundary
  ```
- yaml-cpp parses `mapping.left.<joint_name>.{sources,weights,sign,offset,clamp}`. Missing key → throw with file:joint context. Don't silently default — ADR-020 was specifically about *making* errors visible in the degree domain.

Test points:
- **Snapshot equivalence** (this is the M5b acceptance test, §7.6): `test_mapper_snapshot` loads `mapper_baseline.json` (12 doubles × 2 hands, written by Python on dev Mac) and asserts ‖C++ - Python‖∞ < 1e-6 rad.
- Tolerance 1e-6 rad ≈ 5.7e-5 deg; well below the joint encoder resolution. Set in the test header as `kEpsilonRad = 1e-6`.

### 3.3 `xhand_driver`

```cpp
class XHandDriver {
public:
    XHandDriver(const std::string& port, int baud);   // /dev/ttyACM0, 3000000
    void open();                                       // calls open_serial + list_hands_id + asserts CalibrationStatus==3
    bool has_left() const;
    bool has_right() const;
    void send_left (const std::array<double, 12>& rad, const PIDConfig& pid);
    void send_right(const std::array<double, 12>& rad, const PIDConfig& pid);
    void shutdown();                                   // send_command(mode=0) for all + close_device
private:
    xhand_control::XHandControl ctl_;
    std::optional<int> hand_id_left_;
    std::optional<int> hand_id_right_;
};

struct PIDConfig { float kp, ki, kd, tor_max; int mode; };   // defaults from config.yaml
```

- `open()`: enumerates via `list_hands_id()` → maps each id to L/R via `get_hand_type()` (case-insensitive on first char, accepts `L/l/R/r` per `data_type.hpp::DeviceInfo_t::ev_hand`). Stores ids. Throws `std::runtime_error` if `list_hands_id()` is empty.
- ~~`open()` "asserts CalibrationStatus==3"~~ — **REMOVED 2026-05-18 (plan deviation, user-approved before code landed).** XHand SDK does not expose a hand-level `CalibrationStatus`; the enum of that name in `data_type.hpp` is a per-finger calibration-error code, not a state value, and `HandState_t` has no such field. The "CalibrationStatus == 3" rule in CLAUDE.md / SPEC §5 refers to UDCAP-side calibration and is enforced inside `UdcapReceiver::try_recv` instead. This split matches M3 Python (`xhand_driver.py:50-63` does no calib check) and M5a `test_serial` behavior. CLAUDE.md "Do NOT" entry rephrased simultaneously to disambiguate.
- If only one hand is on the bus, `send_left` / `send_right` against the missing side **throws `std::runtime_error`** — matches Python M3 behavior (`xhand_driver.py:88-90`). Initial plan suggested silent no-op; rejected in favor of failure visibility (consistent with ADR-017's "log not crash on transient errors, but surface persistent misconfiguration").
- `send_*` builds a `HandCommand_t{12 × FingerCommand_t{id, kp, ki, kd, position, tor_max, mode}}` (struct in `data_type.hpp`). Per-joint id = 0..11 in order.
- PID is injected via `XHandPID` at construction time (mirrors Python's pre-built command template, stored once on the object); not a per-`send_*` argument. Values loaded from `config.yaml` `xhand.default_*` — **not** vendor PID (vendor=225 was M5a's choice per ADR-026; project=100 takes over here).
- **send error policy**: ADR-017 — log not crash. (Consecutive-failure threshold and watchdog-on-send-failure deferred to M6 — see plan §8.)

Test points (M5b):
- **Mock mode bypasses XHand entirely** — `--mock` does not open serial. So M5b validates the *interface*, not the wire. Vendor send is exercised in M5c.

### 3.4 `safety`

```cpp
class Watchdog {
public:
    explicit Watchdog(std::chrono::milliseconds timeout_ms);  // 200ms from config
    void update(std::chrono::steady_clock::time_point t);      // on each valid frame
    bool is_stale(std::chrono::steady_clock::time_point now) const;
private:
    std::chrono::steady_clock::time_point last_ok_;
    std::chrono::milliseconds timeout_;
};

void install_signal_handlers(std::atomic<bool>& shutdown_flag);  // SIGINT, SIGTERM
void clamp_in_place(std::array<double, 12>& rad,
                    const std::array<std::pair<double,double>, 12>& limits_rad);
```

- `install_signal_handlers`: registers handlers that set `shutdown_flag = true`. Main loop polls each tick. (No async-signal-unsafe work in the handler.)
- `clamp_in_place`: ADR-021's two-layer defense — the mapper already clamps in degrees, this is a second clamp in radians as fail-safe (e.g., guards a hand-rolled future config edit that loosens the mapper clamp).

### 3.5 `main.cpp`

Skeleton (illustrative — exact LOC during implementation):

```cpp
int main(int argc, char** argv) {
    auto args = cli::parse(argc, argv);                       // --config, --mock, --receiver-only, --hand, --duration
    auto cfg  = Config::load(args.config_path);
    std::atomic<bool> shutdown{false};
    install_signal_handlers(shutdown);

    JointMapper mapper(args.config_path);
    Watchdog wdog(std::chrono::milliseconds(cfg.udcap.timeout_ms));

    std::optional<UdcapReceiver> rx;
    std::optional<XHandDriver>   hand;

    if (!args.mock)         rx.emplace(cfg.udcap.host, cfg.udcap.port);
    if (!args.mock && !args.receiver_only)
                            hand.emplace(cfg.xhand.serial_port, cfg.xhand.baud_rate),  hand->open();

    auto example = args.mock ? Loader::load_example_json("example.json") : Loader{};
    auto next    = std::chrono::steady_clock::now();
    auto period  = std::chrono::microseconds(1'000'000 / cfg.xhand.update_rate_hz);

    while (!shutdown && (!args.duration || elapsed() < args.duration)) {
        UdcapFrame frame = args.mock ? example.next_frame()
                                     : rx->try_recv().value_or(last_frame);
        if (args.mock || (frame.calib_left == 3 && frame.calib_right == 3))
            wdog.update(frame.recv_ts);

        auto L = mapper.map_left(frame.l);
        auto R = mapper.map_right(frame.r);
        safety::clamp_in_place(L, cfg.joint_limits_left_rad);
        safety::clamp_in_place(R, cfg.joint_limits_right_rad);

        if (args.receiver_only || args.mock)  print_frame(frame, L, R);
        if (hand) { hand->send_left(L, cfg.pid); hand->send_right(R, cfg.pid); }

        next += period;  std::this_thread::sleep_until(next);
    }
    if (hand) hand->shutdown();
    return 0;
}
```

(The above is a sketch, not literal source. Real `main.cpp` will be ~120–160 LOC.)

---

## 4. CLI surface

| Flag                       | Meaning                                                           | Exercised in M5b? |
| -------------------------- | ----------------------------------------------------------------- | ----------------- |
| `--config <path>`          | YAML config path. Default `./config.yaml`.                        | ✅ all sub-tests |
| `--mock`                   | No UDP, no serial. Loop `example.json`. Print to stdout.          | ✅ §6.5 / §7.5   |
| `--receiver-only`          | UDP receive + parse + print. No XHand open.                       | ✅ §6.6 / §7.6   |
| `--duration <sec>`         | Auto-exit after N seconds. 0 = run until signal.                  | ✅               |
| `--hand <left\|right\|both>` | Default `both`. Limits which hand the driver attempts to open.  | M5c (compiles in M5b, not exercised) |
| `--port <path>`            | Override `xhand.serial_port` from config. For preset action mode. | M5c              |
| `--actions <list>`         | Comma-separated preset names (fist, palm, v, ok). M3 replacement. | M5c              |
| `--help`                   | Print usage and exit 0.                                           | ✅               |

Flag parser is hand-rolled: ~60 LOC in `src/cli.cpp`. No external dep (cxxopts not added — CLAUDE.md "do not add abstractions").

---

## 5. Build setup (`CMakeLists.txt`)

Key constraints lifted from CLAUDE.md + vendor `tests/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.10)
project(udex_to_xhand LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)  set(CMAKE_BUILD_TYPE Release)  endif()

find_package(CURL          REQUIRED)
find_package(nlohmann_json 3 REQUIRED)
find_package(yaml-cpp      REQUIRED)
find_package(xhand_control REQUIRED
             HINTS ${CMAKE_SOURCE_DIR}/xhand_control_sdk/share)

# vendor .so lives outside system path → bake RPATH so the binary finds it at runtime
set(CMAKE_INSTALL_RPATH ${CMAKE_SOURCE_DIR}/xhand_control_sdk/lib)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

add_executable(udex_to_xhand
    src/main.cpp
    src/cli.cpp
    src/udcap_receiver.cpp
    src/joint_mapper.cpp
    src/xhand_driver.cpp
    src/safety.cpp)

target_include_directories(udex_to_xhand PRIVATE src
    ${CMAKE_SOURCE_DIR}/xhand_control_sdk/include)
target_link_libraries(udex_to_xhand PRIVATE
    xhand_control CURL::libcurl nlohmann_json::nlohmann_json yaml-cpp)
target_link_options(udex_to_xhand PRIVATE -Wl,--disable-new-dtags)   # match vendor sample
target_compile_options(udex_to_xhand PRIVATE -Wall -Wextra -Wpedantic)

option(BUILD_TESTS "Build snapshot equivalence test" OFF)
if(BUILD_TESTS)
    add_executable(test_mapper_snapshot
        tests/test_mapper_snapshot.cpp
        src/joint_mapper.cpp)
    target_include_directories(test_mapper_snapshot PRIVATE src)
    target_link_libraries(test_mapper_snapshot PRIVATE
        nlohmann_json::nlohmann_json yaml-cpp)
endif()
```

Notes:
- `find_package(xhand_control HINTS ...)` mirrors vendor `tests/CMakeLists.txt`. Verified portable in M5a (ADR-024 / 025).
- `-Wl,--disable-new-dtags`: critical. Without it, GNU ld emits `DT_RUNPATH` instead of `DT_RPATH`, and `DT_RUNPATH` doesn't propagate to transitive libs → vendor .so's transitive symbols may fail to resolve. Vendor sample uses the same flag; matching it removes one whole class of "works on M5a `test_serial` but not our binary" failure modes.
- `test_mapper_snapshot` deliberately does **not** link `xhand_control` — it tests pure mapper math, so it's portable to Mac if a reviewer wants to run it locally (would need yaml-cpp + nlohmann_json via brew).

---

## 6. 测试策略 — 具体命令 + 预期

**Tag legend**: `[LOCAL]` = run on the dev Mac (this box). `[PC2]` = run on G1 PC2 (ssh in or open shell directly).

### 6.1 [LOCAL] One-time prep — generate Python baseline

Done **once** before any C++ run. Output is committed.

```bash
cd /Users/kangzixi/Desktop/4-2/xhand/udex_to_xhand
python3 scripts/dump_mapper_baseline.py \
    --example example.json \
    --config  config.yaml \
    --out     tests/fixtures/mapper_baseline.json
cat tests/fixtures/mapper_baseline.json
```

**Expected output** (shape — values come from real Python run):

```json
{
  "source": {
    "example_json_sha256": "<hex>",
    "config_yaml_sha256":  "<hex>",
    "python_version":      "3.x.y",
    "generated_at":        "2026-05-18T..."
  },
  "left":  [-0.0123, 0.0,   ... 12 floats in radians ...],
  "right": [ 0.0,    0.045, ... 12 floats ...]
}
```

The two SHA-256 fields freeze "this baseline corresponds to *that* version of `example.json` + `config.yaml`". C++ test asserts both hashes match before comparing values — protects against silent input drift.

### 6.2 [LOCAL] Code review locally — compile-check the parts that don't need vendor .so

Optional but recommended. Mac can't link `libxhand_control.so` (aarch64-only ELF), but it *can* compile-check headers + run the snapshot test, since neither needs the .so:

```bash
# Requires: brew install cmake yaml-cpp nlohmann-json
mkdir -p build-local && cd build-local
cmake -DBUILD_TESTS=ON .. 2>&1 | tee /tmp/cmake-local.log
# Expected: configures OK as long as xhand_control isn't required for test target.
# If find_package(xhand_control) fails because the .so isn't loadable on darwin,
# either (a) skip this step, or (b) split the cmake into two projects:
#   - main binary in src/
#   - test in tests/
# For M5b we accept (a). Local checks are best-effort.
```

If `find_package(xhand_control)` errors on Mac, that's expected — proceed to PC2. Don't spend time fighting it.

### 6.3 [LOCAL → PC2] Sync source

```bash
# On dev Mac:
git add CMakeLists.txt src/ tests/ scripts/dump_mapper_baseline.py \
        tests/fixtures/mapper_baseline.json \
        docs/plans/20260518-m5b-cpp-rewrite-plan.md
git commit -m "M5b: C++ rewrite of UDCAP→XHand runtime (WIP)"
git push origin main           # or feature branch; user's choice
```

```bash
# On PC2:
cd ~/udex_to_xhand            # or whatever the PC2 checkout path is
git pull --ff-only
```

### 6.4 [PC2] Build

```bash
# System deps (already installed in M5a; verify):
sudo apt install -y cmake g++ libcurl4-openssl-dev libssl-dev \
                    nlohmann-json3-dev libyaml-cpp-dev

# Build:
cd ~/udex_to_xhand
mkdir -p build && cd build
cmake -DBUILD_TESTS=ON ..        2>&1 | tee ../docs/logs/m5b-cmake-2026-05-18.log
make -j$(nproc)                  2>&1 | tee ../docs/logs/m5b-make-2026-05-18.log

# Expected outputs:
ls -lh ./udex_to_xhand ./test_mapper_snapshot
file ./udex_to_xhand
# Expected: ELF 64-bit LSB executable, ARM aarch64, dynamically linked
ldd ./udex_to_xhand | grep xhand
# Expected: libxhand_control.so => /…/xhand_control_sdk/lib/libxhand_control.so (RPATH-resolved)
```

**Acceptance**: both binaries exist, `file` reports aarch64, `ldd` resolves the vendor .so via the baked RPATH. If RPATH didn't take, `ldd` will print "not found" — fix by re-checking the `-Wl,--disable-new-dtags` + `CMAKE_INSTALL_RPATH` block before continuing.

### 6.5 [PC2] `--mock` smoke (M5b acceptance item 1 — no hardware needed)

```bash
cd ~/udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --mock --duration 3 \
    2>&1 | tee ../docs/logs/m5b-mock-run-2026-05-18.log
```

**Expected** (~300 lines, one per tick):

```
[INFO] config loaded: ../config.yaml
[INFO] mode: MOCK (no UDP, no XHand)
[INFO] loaded example.json (24+24 doubles)
[tick   1]  L: [-0.012,  0.000,  0.087, ... 12 floats] R: [ 0.034, ..., 12 floats]
[tick   2]  L: [-0.012,  0.000,  0.087, ...]           R: [ 0.034, ...]
...
[tick 300]  L: ...                                       R: ...
[INFO] exited after 3.0s, 300 ticks, 0 errors
```

Each printed value rounded to 3 decimals (radians). The 12-vector per hand must match the Python M0 stub's last-known output structure (12 numbers, joint order per ADR-022).

### 6.6 [PC2] Snapshot equivalence test (M5b acceptance item 2 — proves algorithm parity)

```bash
cd ~/udex_to_xhand/build
./test_mapper_snapshot \
    --fixture ../tests/fixtures/mapper_baseline.json \
    --example ../example.json \
    --config  ../config.yaml \
    2>&1 | tee ../docs/logs/m5b-snapshot-test-2026-05-18.log
```

**Expected**:

```
[INFO] fixture SHA-256 match: example.json + config.yaml unchanged
[INFO] running JointMapper on example.json
[INFO] L joints max |Δ| = 4.3e-08 rad  (tol 1.0e-06) → PASS
[INFO] R joints max |Δ| = 1.7e-08 rad  (tol 1.0e-06) → PASS
[INFO] all 24 joints within tolerance
exit 0
```

Failure mode example (for reviewer awareness):

```
[ERROR] L joint 4 (index_joint1) diff = 2.4e-03 rad — exceeds tol 1.0e-06
[ERROR] expected (python)  =  0.087266
[ERROR] actual   (cpp)     =  0.084866
[ERROR] inputs: src[6] = -5.0  weights=[1.0]  sign=-1  offset=0  clamp=[0,110]
exit 1
```

Pass criterion: exit 0 and "all 24 joints within tolerance".

### 6.7 [PC2] `--receiver-only` smoke (M5b acceptance item 3 — exercises UDP path without XHand risk)

**Pre-req**: Windows UDCAP HandDriver running and sending to PC2's IP on port 9000. This is the same network setup that M5c will use; doing a 10-second probe here de-risks M5c.

```bash
cd ~/udex_to_xhand/build
./udex_to_xhand --config ../config.yaml --receiver-only --duration 10 \
    2>&1 | tee ../docs/logs/m5b-receiver-2026-05-18.log
```

**Expected**:

```
[INFO] config loaded
[INFO] mode: RECEIVER_ONLY (no XHand)
[INFO] UDP bound 0.0.0.0:9000
[INFO] waiting for first packet…
[INFO] first packet from 192.168.x.y after 0.04s
[recv  1] L: l0=-47.6 l1=-60.0 ... l23=-1.0 | R: r0=-9.5 ...  | calib L=3 R=3 | fps=89.2
[recv  2] ...
...
[INFO] exited after 10.0s, ~890 packets, 0 parse errors
```

If Windows side isn't sending: `waiting for first packet…` plus a warning every second; binary still exits cleanly after `--duration`. That's fine — it proves the receiver opens the socket and times out gracefully, even if it didn't get a chance to parse a real packet. **In that case §7 DoD item 3 is recorded as "deferred to M5c"**, not as a failure of M5b.

### 6.8 [PC2] Static-analysis pass (low cost, high signal)

```bash
cd ~/udex_to_xhand
# Quick: just make sure -Wall -Wextra -Wpedantic stayed clean
grep -E '(warning|error):' docs/logs/m5b-make-2026-05-18.log || echo "no diagnostics"
# Expected: "no diagnostics"
```

Treat any new warning as a blocker for M5b commit. CLAUDE.md doesn't mandate clang-tidy; this is a deliberately narrow check.

---

## 7. Definition of Done (M5b)

For M5b to be marked ✅ in roadmap:

1. ✅ `CMakeLists.txt` + `src/{main,cli,udcap_receiver,joint_mapper,xhand_driver,safety}.{cpp,hpp}` + `src/logging.hpp` written and present in repo.
2. ✅ §6.4 build produces `udex_to_xhand` ELF aarch64 on PC2 with vendor .so resolved via RPATH.
3. ✅ §6.5 `--mock` run prints 12+12 joint floats per tick at ~100 Hz, 300 ticks over 3 seconds, 0 errors.
4. ✅ §6.6 snapshot test exits 0; all 24 joints within 1e-6 rad of Python baseline.
5. ⚠️ §6.7 `--receiver-only` run: full pass if UDCAP HandDriver is reachable from PC2; partial pass (graceful timeout) acceptable if not — record which.
6. ✅ §6.8 no `-Wall -Wextra -Wpedantic` warnings in `make` output.
7. ✅ `docs/logs/m5b-cmake-*.log`, `m5b-make-*.log`, `m5b-mock-run-*.log`, `m5b-snapshot-test-*.log`, `m5b-receiver-*.log` committed.
8. ✅ `docs/decisions/028-pure-cpp-rewrite-not-binding.md` written (Context / Decision / Consequences / Alternatives).
9. ✅ `CLAUDE.md` "Migration status" + `SPEC.md` §8 + §12 Q10 updated.
10. ✅ `docs/plans/00-roadmap.md` §M5b marked ✅ with one-line result summary; ADR-028 added to §M5 ADRs line.
11. ✅ Single commit on a single branch (or two — code + docs — operator's call; recommend one).

Items 3, 4, 6 are M5b's *non-negotiable* algorithmic acceptance. Item 5 is the only one that can be deferred without blocking M5b (because UDCAP HandDriver reachability is M5c scope by roadmap — this just gives us a free early signal).

---

## 8. What this milestone deliberately does **not** cover

| Topic                                      | Where it goes                                                              |
| ------------------------------------------ | -------------------------------------------------------------------------- |
| Real XHand hardware run (`send_command` to motors) | M5c — re-validate M3/M4 acceptance scripts                          |
| `--actions fist,palm,v,ok` (M3 replacement) | M5c                                                                       |
| Watchdog tested under fault injection      | M6 (kill UDCAP → verify hold-last-position)                                |
| Graceful SIGTERM tested                    | M6                                                                         |
| Clamp tested by jamming a glove past limits | M6                                                                         |
| Dual-hand `send_left` + `send_right`       | M7                                                                         |
| Right-hand sign verification               | M7 (SPEC §3.1 / Risk 5)                                                    |
| Latency profiling vs Python M4 baseline    | M5c (compares end-to-end timings, after hardware is in the loop)           |
| PID retuning                               | M8                                                                         |
| Smoothing filter for UDP jitter            | M8                                                                         |
| Removal of `*.py` files from repo root     | Post-M5c (deletion proposal in its own commit)                             |

---

## 9. Risks specific to M5b

| #   | Risk                                                                                          | Likelihood | Impact | Mitigation                                                                                                                                                |
| --- | --------------------------------------------------------------------------------------------- | ---------- | ------ | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| R1  | RPATH not baked / `ldd` fails to resolve `libxhand_control.so` at runtime                     | MEDIUM     | HIGH   | §5 uses `CMAKE_INSTALL_RPATH` + `CMAKE_BUILD_WITH_INSTALL_RPATH` + `-Wl,--disable-new-dtags` (matches vendor sample). §6.4 explicitly checks `ldd` output. |
| R2  | yaml-cpp / nlohmann_json minor-version diffs between Mac and PC2 produce subtly different parse | LOW        | MEDIUM | Both apt packages on Ubuntu (PC2) are pinned by distro; on Mac, brew may be newer — but Mac never runs the binary. Risk is contained.                     |
| R3  | C++ floating-point semantics differ from Python (e.g., `0.6 * 5.0 + 0.4 * 4.0`)               | LOW        | MEDIUM | Tolerance 1e-6 rad. If we see systematic offsets at 1e-7, investigate IEEE-754 vs Python `math.fsum` use in Python prototype.                              |
| R4  | UDCAP HandDriver not reachable from PC2 at M5b time (Windows side not configured)             | HIGH       | LOW    | §6.7 already accepts graceful timeout. Doesn't block §7 DoD items 1-4 + 6.                                                                                |
| R5  | "Mock mode bypasses XHand" feels too easy — reviewer might object it doesn't test real send  | MEDIUM     | LOW    | M5b's scope is *algorithmic parity + build*. Real send is M5a-proven (vendor sample worked) + M5c-validated (our binary). The split is deliberate.        |
| R6  | Snapshot fixture committed without source SHAs → silent drift                                  | LOW        | MEDIUM | §6.1 fixture format embeds SHA-256 of both source files. §6.6 test asserts SHAs match before comparing.                                                   |
| R7  | Hand-rolled CLI parser has bugs (e.g., `--duration3` parsed as duration=3)                    | LOW        | LOW    | Cover with a 5-line unit test in `cli.cpp` (assert on argc/argv tuples). Not worth adding cxxopts.                                                         |

---

## 10. Non-obvious decisions to capture as ADRs (after implementation, not before)

Only the **first** is pre-flagged in the roadmap. Others are candidates — write only if implementation makes the rationale stick.

| Candidate                                                                                                                                | Pre-flagged? | Will write if…                                                                              |
| ---------------------------------------------------------------------------------------------------------------------------------------- | ------------ | ------------------------------------------------------------------------------------------- |
| **ADR-028** Pure C++ rewrite (no pybind11 / ctypes binding)                                                                              | ✅ roadmap §M5 | Always written. See §0 for rationale.                                                       |
| ADR-029 Snapshot equivalence via committed Python-generated JSON fixture (vs running Python on PC2 every test, vs hardcoded numeric fixture) | candidate    | …if a reviewer pushes back on "why commit a JSON instead of running Python". §6.1 hashes are the load-bearing detail. |
| ADR-030 Hand-rolled CLI parser (no cxxopts / no Boost.Program_options)                                                                   | candidate    | …if the parser grows past ~80 LOC, in which case "minimal hand-roll" stops being defensible.                          |
| ADR-031 `--mock` deliberately bypasses both UDP socket and XHand serial (not just XHand)                                                  | candidate    | …only if a reviewer or future me wonders why mock isn't "fake UDP source + real mapper + real send". The answer: M0 stub was structured the same way; preserves drop-in replacement semantics. |

ADRs 029/030/031 are "write only if the design survives review challenges". M5a's ADR-024/025/026/027 were written the same way — *after* the decisions had been made and the runs had succeeded. Don't pre-author ADRs for choices that may not stick.

---

## 11. 6 Execution Record (filled at end of M5b)

> Populate after the §6 commands run. Mirrors M5a plan §6 structure.

### 11.1 Environment snapshot

- PC2 hostname: ___
- `lsb_release -a` → ___
- `cmake --version` → ___
- `g++ --version` → ___
- `dpkg -s nlohmann-json3-dev | grep Version` → ___
- `dpkg -s libyaml-cpp-dev | grep Version` → ___
- vendor SDK version (per M5a): 1.4.3

### 11.2 Build (§6.4)

- `cmake ..` exit code: ___
- `make -j$(nproc)` exit code: ___
- `file ./udex_to_xhand` → ___
- `ldd ./udex_to_xhand | grep xhand` → ___

### 11.3 Mock run (§6.5)

- ticks observed: ___ (expected ~300)
- duration: ___ s (expected ~3.0)
- joint count per line: L=12 R=12 (Y/N)
- errors in log: ___

### 11.4 Snapshot test (§6.6)

- fixture SHA match: ___
- L max |Δ| (rad): ___ (tol 1e-6)
- R max |Δ| (rad): ___ (tol 1e-6)
- exit code: ___

### 11.5 Receiver-only (§6.7)

- Windows UDCAP reachable from PC2: Y / N
- packets received in 10s: ___
- parse errors: ___
- (if N): timeout behavior graceful: Y / N

### 11.6 Static checks (§6.8)

- `grep warning docs/logs/m5b-make-*.log`: ___ (expected 0)

### 11.7 Anomalies / notes

- (free-form, like M5a §6)

---

## 12. Commit shape (advisory)

DoD §11 single-commit recommendation lays out roughly:

```
M5b ✅: Python prototype → C++17 `udex_to_xhand` binary on G1 PC2

- src/{main,cli,udcap_receiver,joint_mapper,xhand_driver,safety}.{cpp,hpp}: new
- CMakeLists.txt: top-level, finds vendor xhand_control + yaml-cpp + nlohmann_json
- tests/test_mapper_snapshot.cpp + tests/fixtures/mapper_baseline.json: parity test
- scripts/dump_mapper_baseline.py: one-shot Python fixture generator
- docs/logs/m5b-*-2026-05-18.log: build + run evidence
- docs/decisions/028-pure-cpp-rewrite-not-binding.md: ADR
- docs/plans/00-roadmap.md: M5b ✅, ADR-028 added
- docs/plans/20260518-m5b-cpp-rewrite-plan.md: this plan, §11 filled
- CLAUDE.md + SPEC.md: migration status updated to "C++ authoritative"

Python prototype files retained at repo root (deletion in post-M5c commit).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

(Operator may prefer two commits — code + docs/logs — that's fine. Single is simpler to revert if M5c uncovers a regression.)
