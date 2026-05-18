# ADR-028: Pure C++ rewrite of the runtime stack (no pybind11 / ctypes / cffi binding)

Date: 2026-05-18
Status: Accepted
Milestone: M5b

## Context

After M0–M4 the runtime stack was Python (`main.py` + `udcap_receiver.py` + `joint_mapper.py` + `xhand_driver.py` + `safety.py`). The original M5 plan (before roadmap revision 2 on 2026-05-16) assumed an aarch64 `xhand_controller` Python wheel existed. It does not.

What the vendor actually ships on aarch64 (`xhand_control_sdk/`):
- `lib/libxhand_control.so` — ELF aarch64 shared library
- `include/{xhand_control,data_type,error_manager,communication_interface,visibility_control}.hpp` — C++17 headers
- `share/xhand_control/cmake/xhand_controlConfig*.cmake` — cmake package config
- `tests/src/serial_test.cpp` — C++ sample

Zero Python entry points. M3's Python `from xhand_controller import xhand_control` cannot resolve on aarch64. The decision space at M5b start was either to rewrite the runtime in C++, or to write a Python binding over the C++ SDK. Roadmap §M5 revision 2 pre-flagged the rewrite path; this ADR records the rationale after implementation confirmed the cost estimate.

## Decision

Replace the Python runtime with a single C++17 binary `udex_to_xhand` built from `src/main.cpp` + 6 modules + top-level `CMakeLists.txt`. Do **not** introduce a pybind11 / ctypes / cffi / gRPC / IPC layer between Python and the vendor SDK. The 100 Hz control loop runs entirely in native C++ from the moment it touches the SDK.

Python is preserved as `legacy_python/` for snapshot oracle + reviewer parity diff (see ADR-031). Offline helpers in `scripts/` remain Python — they are not in the realtime path.

## Consequences

**正面**
- Native ABI: zero Python ⇄ C++ marshalling cost on the hot path. Direct calls into `xhand_control::XHandControl::send_command(hand_id, HandCommand_t)`; no struct re-packing per cycle.
- Single binary deploy on PC2: `cmake .. && make` → ELF aarch64 → run. M5b §6.4 (2026-05-18) confirmed clean build with `g++ 11.4.0` / `nlohmann_json 3.10.5` / `OpenSSL 3.0.2` / `CURL 7.81.0` — no warnings under `-Wall -Wextra -Wpedantic`.
- Reference (`xhand_control_ros2.hpp`) is already C++; language baseline stays unified across vendor reference, our runtime, and any future ROS2 reintegration if pursued.
- M5b §6.6 snapshot test: ‖cpp - python‖∞ max diff = **0.0e+00 rad (L)** / **1.4e-17 rad (R)** against the Python M4 baseline, tolerance 1.0e-06. The rewrite preserved algorithmic parity bit-for-bit on the left hand and within IEEE-754 round-off on the right. Strong evidence that ADRs 020-022's mapping math survived the rewrite intact.
- CLAUDE.md "Do NOT add a pybind11 / ctypes / cffi layer" is now an enforced invariant, not aspirational.

**负面 / 风险**
- LOC went up: M5b C++ runtime is ~1100 lines vs Python prototype ~250. Verbosity is the standard cost of C++ explicit memory + type layout.
- Lost the rapid REPL iteration of Python in the control loop. Mitigated: `legacy_python/` retained for ad-hoc analysis + `scripts/` for offline UDCAP work.
- Coupling to one specific vendor SDK version: `find_package(xhand_control HINTS xhand_control_sdk/share)` pulls in vendor's cmake config which may shift between releases. ADR-024 already established the in-tree SDK as a frozen baseline; same applies here.

## Alternatives

1. **pybind11 layer over `libxhand_control.so`** — Rejected. Adds Python ⇄ C++ marshalling per `send_command` call (12 × `FingerCommand_t` packed struct, mixed `uint16_t`/`int16_t`/`float`). At 100 Hz × 2 hands = 2400 marshalling cycles/sec; even a few µs each is wasteful. Vendor's `__attribute__((packed))` structs are exactly the layout pybind11 stumbles on. Plus net-new code we'd own and version against the vendor.
2. **ctypes / cffi shim** — Rejected. Even more fragile than pybind11 — every struct change in `data_type.hpp` requires a parallel Python `Structure` definition. Vendor doesn't ship one, so we'd reverse-engineer and maintain it ourselves.
3. **gRPC / Unix-socket bridge** (Python ⇄ C++ daemon) — Rejected. Out-of-process at 100 Hz adds context-switch + serialization cost on every cycle. Introduces an unsupervised second process. Counters M5's "single binary" simplification.
4. **Keep Python on a separate x86_64 dev box, RPC to PC2** — Rejected. Reintroduces the network as critical path and requires the dev box to stay running for teleoperation. ADR-023 pivoted to PC2 specifically to remove that external dependency.
5. **Use vendor's ROS2 wrapper (`xhand_control_ros2/`)** — Rejected. ROS2 on PC2 is a heavier dependency (rclcpp + colcon + DDS runtime + topic plumbing). CLAUDE.md "Do NOT use ROS2" is explicit. The ROS2 wrapper is reference-only.
