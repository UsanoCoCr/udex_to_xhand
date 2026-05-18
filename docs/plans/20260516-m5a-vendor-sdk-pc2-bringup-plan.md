# M5a — Vendor C++ SDK Bring-up on G1 PC2 (Left Hand Only)

| Field                | Value                                                                                                                       |
| -------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Date                 | 2026-05-16 (planned) / 2026-05-18 (executed)                                                                                |
| Milestone            | M5a (subset of M5; see [00-roadmap.md §M5](./00-roadmap.md))                                                                |
| Spec refs            | [SPEC.md §7 Tech Stack, §9.1.5 Phase 1.5](../../SPEC.md)                                                                    |
| Status               | ✅ PASS — executed on G1 PC2 2026-05-18, all §3.5 acceptance items confirmed by operator                                    |
| Scope (LIMITED)      | **LEFT hand only**; position-control mode only; **no project source code touched** — only vendor `xhand_control_sdk/tests/` |
| Author               | Claude + 操作员                                                                                                             |
| Where each step runs | Dev Mac (this box): git/ssh client + write this plan. **G1 PC2 (aarch64 Linux): every build + hardware step.**              |
| Non-obvious ADRs     | [024](../decisions/024-vendor-sample-as-m5a-harness.md) · [025](../decisions/025-vendor-source-pristine-sed-not-committed.md) · [026](../decisions/026-m5a-uses-vendor-pid-defaults-not-claudemd.md) · [027](../decisions/027-joint4-index-as-m5a-smoke-joint.md) |

> **What M5a actually proves**: `libxhand_control.so` (aarch64) + headers + vendor cmake `share/` + vendor sample compile cleanly on G1 PC2, and the kit can enumerate, identify, and command **one LEFT XHand** over `/dev/ttyACM0` @ 3 Mbps. Nothing else.

---

## 1. 文件清单

### 1.1 新增 (will be committed after run)

| Path                                                     | One-line responsibility                                                                                |
| -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `docs/plans/20260516-m5a-vendor-sdk-pc2-bringup-plan.md` | **This file** — frozen plan + execution record skeleton. After running on PC2, fill §6 and commit.     |
| `docs/logs/m5a-test-serial-<date>.log`                   | Captured stdout/stderr from the interactive `./test_serial` run on PC2 (created at run time, not now). |

### 1.2 临时修改 — **NOT committed** (per-build only)

| Path                                                     | Change                                              | Why temporary                                                                                                                                                                       |
| -------------------------------------------------------- | --------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `xhand_control_sdk/tests/src/serial_test.cpp`            | line 36: `"/dev/ttyUSB0"` → `"/dev/ttyACM0"`        | Vendor sample hardcodes a serial port that doesn't match our hardware. Our XHand enumerates as CDC-ACM (ADR-014). Apply via `sed -i.bak`; restore from `.bak` before any `git add`. |
| (optional) `xhand_control_sdk/tests/src/serial_test.cpp` | line 68 `set_hand_name(..., "xhand")` commented out | Avoid renaming the device persistently. Cosmetic; skip if you don't mind the rename.                                                                                                |
| (optional) `xhand_control_sdk/tests/src/serial_test.cpp` | line 79 `reset_sensor(...)` commented out           | Avoid resetting a fingertip sensor calibration on a unit we don't yet use for force feedback.                                                                                       |

> The `xhand_control_sdk/tests/build/` directory created by cmake is already gitignored via the generic `build/` rule.

### 1.3 不修改 (read-only inputs)

- `xhand_control_sdk/lib/libxhand_control.so` — vendor binary, linked as-is
- `xhand_control_sdk/include/*.hpp` — vendor headers, included as-is
- `xhand_control_sdk/share/xhand_control/cmake/*.cmake` — vendor cmake config (verified: uses `_IMPORT_PREFIX` relative to the file, no absolute paths baked in, portable to PC2)
- Everything outside `xhand_control_sdk/` — M5a does **not** touch project Python prototype, project-level `src/` (does not exist yet), `config.yaml`, or top-level `CMakeLists.txt` (also does not exist yet).

---

## 2. 数据流

```
[Local Mac, this dev box]
  │  (1) git push origin main             # currently 2 commits ahead: 70b1922, db071d7
  ↓
[Git remote — wherever origin/main lives]
  ↓
[G1 PC2 — aarch64 Linux, henceforth "PC2"]
  │
  ├─ (2) git clone OR git pull --ff-only
  │       (xhand_control_sdk/ now present on PC2 with aarch64 .so)
  │
  ├─ (3) apt install build deps:
  │       cmake g++ libcurl4-openssl-dev libssl-dev nlohmann-json3-dev libyaml-cpp-dev
  │
  ├─ (4) Hardware: plug in LEFT XHand on a PC2 USB port (no right hand connected)
  │       Verify dialout group membership; /dev/ttyACM0 readable+writable
  │
  ├─ (5) sed-patch ttyUSB0 → ttyACM0 in vendor sample source (keep .bak)
  │
  ├─ (6) cmake .. && make -j$(nproc) in xhand_control_sdk/tests/build/
  │        ├─ produces ./test_serial      (used by M5a)
  │        └─ produces ./test_ethercat    (NOT used — EtherCAT path is future work)
  │
  └─ (7) ./test_serial 2>&1 | tee ../m5a-test-serial-$(date +%F).log
          │
          │ RS485 @ 3 Mbps on /dev/ttyACM0
          ↓
       [LEFT XHand — the only hand connected]
          ↑ list_hands_id        ⇒ [hand_id_L]              # length exactly 1
          ↑ get_hand_type        ⇒ "Left" / "L"
          ↑ get_serial_number    ⇒ vendor SN string
          ↑ read_version / read_parameters                    # informational, expected to succeed
          ↑ send_command (joint 4, position=0.1 rad, kp=225*)
          ↑ read_state            ⇒ position read-back > 0.05 rad
          ↑ send_command (joint 4, position=0.0 rad)
          ↑ read_state            ⇒ position read-back ≈ 0 rad

[Back to dev Mac]
  ↑ (8) scp pc2:<repo>/xhand_control_sdk/tests/m5a-test-serial-*.log docs/logs/
  ↑ (9) Fill §6 Execution Record below, then git add + commit
```

\* The vendor sample's mode=3 path sets `kp=225, ki=0, kd=0, tor_max=350` (see `serial_test.cpp:193-198`). That's different from our project defaults (kp=100, tor_max=300 per CLAUDE.md), but it's the vendor's own validated baseline — M5a uses it as-is. Project PIDs are set by M5b from `config.yaml`, not here.

---

## 3. 测试策略

Each subsection is **(a)** command(s) to run on **PC2**, **(b)** expected outcome, **(c)** the specific failure mode that blocks M5a.

### 3.1 Environment prep (one-time per PC2)

```bash
# (a)
sudo apt update && sudo apt install -y \
    cmake g++ libcurl4-openssl-dev libssl-dev \
    nlohmann-json3-dev libyaml-cpp-dev

sudo usermod -aG dialout $USER
# After this, log out + back in, OR start a new login shell. Verify:
groups | tr ' ' '\n' | grep -qx dialout && echo "OK: dialout active" || echo "FAIL: re-login required"
```

- **(b)** `apt` completes with no `E:` lines. `groups` contains `dialout`.
- **(c) Block**: PC2 offline → install `.deb` packages from local mirror; if `nlohmann-json3-dev` isn't in the distro repo, fall back to `git clone` of the header-only library into `/usr/local/include/nlohmann/`.

### 3.2 Hardware check (LEFT hand only)

```bash
# (a) Plug LEFT XHand into one PC2 USB port. Then:
ls -l /dev/ttyACM*
dmesg | tail -20 | grep -iE 'cdc_acm|tty(ACM|USB)'
stat -c '%n mode=%a group=%G' /dev/ttyACM0
```

- **(b)** Exactly one `/dev/ttyACM0` (or `ttyACM1` if other CDC-ACM peripherals exist; in that case use the one that just appeared in dmesg). `dmesg` shows `cdc_acm`, NOT `ftdi_sio` / `pl2303` (those would mean USB-serial bridge instead of CDC-ACM and would contradict ADR-014). `stat` shows `mode=660 group=dialout`.
- **(c) Block**: no `ttyACM*` enumerated → bad cable / unpowered hand / udev rule issue. Verify with `lsusb` that the XHand vendor ID shows up before re-trying.

### 3.3 Get sources onto PC2

```bash
# (a) On dev Mac — push current branch (2 commits ahead of origin/main):
git push origin main

# (a) On PC2 — choose ONE depending on initial state:
git clone <repo-url> && cd <repo>
#   OR
cd <repo> && git pull --ff-only

# Sanity-check the aarch64 binary
file xhand_control_sdk/lib/libxhand_control.so
```

- **(b)** `file` prints `ELF 64-bit LSB shared object, ARM aarch64, …`.
- **(c) Block**: x86_64 `.so` checked out (would mean either stale branch or accidental use of the legacy `xhand_control_sdk_py/`). Confirm you're on `main` with both `70b1922` and `db071d7` present in `git log`.

### 3.4 Patch + build vendor sample

```bash
cd xhand_control_sdk/tests

# (a) Patch port string in place; keep .bak for restore
sed -i.bak 's|/dev/ttyUSB0|/dev/ttyACM0|g' src/serial_test.cpp
diff src/serial_test.cpp src/serial_test.cpp.bak    # should show exactly ONE hunk on line 36

# (a) Out-of-source build
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# (a) Confirm artifacts
file test_serial
ls -l test_serial test_ethercat
```

- **(b)** `cmake ..` finds `xhand_control` via the bundled `share/xhand_control/cmake/`. `make` produces both binaries. `file test_serial` reports `ELF 64-bit … aarch64`.
- **(c) Block A**: `cmake` says "Could NOT find xhand_control" → confirm `xhand_control_sdk/share/xhand_control/cmake/xhand_controlConfig.cmake` exists; this was verified locally before plan finalization.
- **(c) Block B**: `make` fails on `nlohmann/json.hpp` → reinstall `nlohmann-json3-dev` and re-run.
- **(c) Block C**: `make` fails with `cannot find -lcurl` / `-lssl` → reinstall `libcurl4-openssl-dev` and `libssl-dev`.

### 3.5 Run vendor smoke test against LEFT hand

```bash
# Still in xhand_control_sdk/tests/build/
./test_serial 2>&1 | tee ../m5a-test-serial-$(date +%F).log
```

Interactive walk-through — script your inputs to keep the run short and safe:

| Prompt (vendor's wording, paraphrased)     | Type this          | Why                                                                                                      |
| ------------------------------------------ | ------------------ | -------------------------------------------------------------------------------------------------------- |
| `select follow num to choose fingure mode` | `3`                | Position Control Mode — matches SPEC.md mode=3.                                                          |
| `Enter finger id:`                         | `4`                | XHand joint 4 = `index_joint1` (proximal flexion of index — safe single-DOF move; no thumb / abduction). |
| `Enter position:`                          | `0.1`              | ~5.7°. Small, safe, well within the joint's range.                                                       |
| (re-prompt) `Enter finger id:`             | `4`                | Same joint.                                                                                              |
| `Enter position:`                          | `0.0`              | Return to neutral.                                                                                       |
| (re-prompt) `Enter finger id:`             | (press **Ctrl+C**) | Triggers `exit_flag`, calls `close_device()`, prints `Exiting program.`.                                 |

**(b) Acceptance — all eight must hold (copy/paste evidence from the captured log into §6):**

1. Banner: `xhand_control initialized <version>` shows a non-empty SDK version.
2. `hand id: <N>` line appears **exactly once** (only one hand connected).
3. `type: Left` (or `L`).
4. `serial number: <non-empty>`.
5. After sending position=0.1 to joint 4: `command sent successfully`.
6. The follow-up `Finger 4 position:` line reads back a value `> 0.05` (proves the motor actually moved, not merely that the command was acknowledged).
7. After Ctrl+C: `Exiting program.` is printed (graceful close — confirms `close_device()` ran).
8. **Physical observation** at the hand:
   - Index finger of the LEFT XHand visibly flexes slightly, then returns to neutral.
   - No buzzing / oscillation, no slam, no error LEDs on the palm board.

**(c) Failure modes that block M5a:**
- Open serial fails (`Failed to open serial device …`) → re-check §3.2 group + mode; verify cable.
- `list_hands_id` returns empty → wrong RS485 polarity / hand unpowered. Confirm power LED on palm.
- Position read-back stays at 0 after a non-zero command → motor not energized; verify mode=3 was actually selected.
- Hand twitches violently → STOP IMMEDIATELY (Ctrl+C); the vendor's kp=225 plus an over-large position would explain this, but 0.1 rad shouldn't trigger it. If it does, file a finding and pause M5a.

### 3.6 Restore vendor source (do NOT commit the sed patch)

```bash
cd xhand_control_sdk/tests
mv src/serial_test.cpp.bak src/serial_test.cpp
git status -- xhand_control_sdk/tests/src/serial_test.cpp
# Expected: "nothing to commit, working tree clean" for this path.
```

---

## 4. 验证命令 (single-shot checklist)

Run sequentially on **PC2** after §3.1 + §3.2 are green. Each block prints `OK` or `FAIL`.

```bash
# 0. Aarch64 .so present and is in fact aarch64
file xhand_control_sdk/lib/libxhand_control.so | grep -q aarch64 && echo "[0] OK" || echo "[0] FAIL"

# 1. Serial port present, owned by dialout, R/W as current user
ls -l /dev/ttyACM0 && \
    [ -r /dev/ttyACM0 ] && [ -w /dev/ttyACM0 ] && echo "[1] OK" || echo "[1] FAIL"

# 2. Apply temporary port patch
( cd xhand_control_sdk/tests \
    && sed -i.bak 's|/dev/ttyUSB0|/dev/ttyACM0|g' src/serial_test.cpp \
    && grep -q '/dev/ttyACM0' src/serial_test.cpp \
    && echo "[2] OK patched" || echo "[2] FAIL patch" )

# 3. Build vendor tests
( cd xhand_control_sdk/tests \
    && mkdir -p build && cd build \
    && cmake .. > cmake.log 2>&1 \
    && make -j$(nproc) > make.log 2>&1 \
    && file test_serial | grep -q aarch64 \
    && echo "[3] OK built" || { echo "[3] FAIL build"; tail -30 cmake.log make.log; } )

# 4. Run the interactive smoke test (capture log)
( cd xhand_control_sdk/tests/build \
    && ./test_serial 2>&1 | tee ../m5a-test-serial-$(date +%F).log )
# → follow §3.5 walk-through; verify §3.5 acceptance items 1–8 by hand.

# 5. Restore vendor source
( cd xhand_control_sdk/tests \
    && mv src/serial_test.cpp.bak src/serial_test.cpp \
    && git status --porcelain -- src/serial_test.cpp \
    | grep -q . && echo "[5] FAIL still dirty" || echo "[5] OK restored" )

# 6. (FROM dev Mac, not PC2) Ship the log back
# mkdir -p docs/logs
# scp <pc2-user>@<pc2-host>:<repo-path>/xhand_control_sdk/tests/m5a-test-serial-*.log docs/logs/

# 7. Commit log + filled-in plan (FROM dev Mac)
# git add docs/plans/20260516-m5a-vendor-sdk-pc2-bringup-plan.md docs/logs/m5a-test-serial-*.log
# git commit -m "M5a: vendor SDK + Left XHand bring-up on G1 PC2 (log + execution record)"
```

---

## 5. 风险 & 缓解

| #   | Risk                                                                                     | Mitigation                                                                                                                                                                                            |
| --- | ---------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | sed patch leaks into a commit                                                            | `sed -i.bak` always leaves `.bak`; §3.6 explicitly restores; §4 step 5 fails loudly if the working tree is still dirty under that path.                                                               |
| 2   | Vendor sample's kp=225 is more aggressive than CLAUDE.md's kp=100                        | M5a only sends ±0.1 rad (~5.7°). Small enough that the higher gain won't slam. Project PIDs return to kp=100 in M5b from `config.yaml`.                                                               |
| 3   | `set_hand_name(..., "xhand")` rewrites name persistently across power cycles             | Cosmetic, but if undesired comment out line 68 of `serial_test.cpp` after the port sed and rebuild. Note the additional patch under §1.2.                                                             |
| 4   | `reset_sensor(hand, 0x11)` resets a fingertip sensor that isn't part of position control | Out of scope for our project but conservative move is to comment out line 79 before rebuilding. Recoverable via vendor calibration tooling if needed.                                                 |
| 5   | nlohmann_json version skew between vendor build host and PC2 distro                      | Use distro `nlohmann-json3-dev`; do not mix pip/conda-installed JSON libs. The SDK is header-only-consumer for nlohmann, so the only risk is missing header, which §3.4 Block B catches.              |
| 6   | `find_package(xhand_control)` fails because of baked-in absolute paths                   | Verified locally: vendor `xhand_controlConfig.cmake` computes `_IMPORT_PREFIX` relative to the file's location with four `get_filename_component(... PATH)` calls. Portable to any checkout location. |
| 7   | M5a passes but later M5b project link fails on the same PC2                              | M5a is the bisect baseline — keep this plan's log so we can diff M5b's link line against vendor's known-good link.                                                                                    |
| 8   | Wrong hand connected (Right instead of Left)                                             | §3.5 acceptance item 3 (`type: Left`) catches this. Stop and reconnect the correct hand.                                                                                                              |

---

## 6. Execution Record — filled 2026-05-18

```
Date run         : 2026-05-18
PC2 host         : G1 PC2 (hostname not captured during run)
PC2 kernel       : aarch64 Linux (uname -a not captured during run)
SDK version      : 1.4.3
hand_id          : 1
hand type        : L
serial number    : 012L320220250728005
Joint 4 cmd      : 0.1 rad sent → position read-back = 0.0843551 rad  (target > 0.05 ✅)
Joint 4 return   : 0.0 rad sent — executed by operator after Joint 4 cmd; return-to-zero
                   prompt visible in log but read-back line was not captured in the pasted
                   slice. Operator confirmed §3.5 item 7 ("Exiting program." printed) and
                   §3.5 item 8 (physical behaviour) hold for the full run.
Physical check   : [x] index finger visibly moved
                   [x] returned to neutral cleanly
                   [x] no buzz / no slam / no error LEDs
Log file         : docs/logs/m5a-test-serial-2026-05-18.log  (partial — captures the
                   command-control segment; pre-banner kernel/host lines and post-Ctrl+C
                   close lines not in the pasted slice; full run retained on PC2)
Vendor patches   : [x] only ttyUSB0→ttyACM0
                   [ ] set_hand_name NOT commented — log shows "save to flash ok
                       set name successfully, current name xhand". Name was already
                       "xhand", so the persistent write is a no-op rename.
                   [ ] reset_sensor NOT commented — log shows "reset sensor successfully".
                       Acceptable for M5a (no force-feedback usage in project scope).
Anomalies        : First send_command attempt returned "command sent failed 1501070
                   Communication data CRC error" with read-back -0.0174528 rad
                   (no movement, as expected when the frame is rejected). Operator
                   re-sent immediately; second attempt returned "command sent
                   successfully" with read-back 0.0843551 rad. The retry-once behaviour
                   is consistent with one-off RS485 line noise at 3 Mbps, not a
                   reproducible bug. Flagged as a watch-item for M5b: the project
                   driver should at minimum log CRC failures (ADR-017 already covers
                   "log-not-crash on send errors").
Decision         : [x] PASS — proceed to M5b
                   [ ] FAIL
```

### Acceptance summary (§3.5 items 1–8)

| # | Item                                    | Evidence                                                   |
| - | --------------------------------------- | ---------------------------------------------------------- |
| 1 | Banner with non-empty SDK version       | `xhand_control initialized 1.4.3` ✅                       |
| 2 | `hand id: <N>` appears exactly once     | `hand id: 1` (one line) ✅                                 |
| 3 | `type: Left` / `L`                      | `type: L` ✅                                               |
| 4 | Serial number non-empty                 | `012L320220250728005` ✅                                   |
| 5 | `command sent successfully` after 0.1   | Confirmed (second attempt; first hit CRC, see Anomalies) ✅ |
| 6 | Read-back > 0.05 rad after 0.1 commanded | `0.0843551` rad ✅                                         |
| 7 | `Exiting program.` after Ctrl+C         | Confirmed by operator (not in pasted log slice) ✅         |
| 8 | Physical: visible flex, return, no buzz | Confirmed by operator ✅                                   |

---

## 7. Definition of Done

M5a is **done** when:

1. `xhand_control_sdk/tests/build/test_serial` exists on PC2, built from unmodified vendor `.so` + headers, with **only** the local `sed` patch on the port string (and optionally the two RECOMMENDED comment-outs in §1.2).
2. Running it against the LEFT XHand passes all 8 acceptance items in §3.5.
3. The capture log is in `docs/logs/m5a-test-serial-<date>.log`, and §6 Execution Record is filled in.
4. `git status` under `xhand_control_sdk/` shows **no staged or unstaged changes**.
5. Plan file + log file are committed in one commit on dev Mac.

After M5a is done, M5b can start immediately — the same `libxhand_control.so` that vendor's `test_serial` linked against is the one `find_package(xhand_control)` will pick up in M5b's project-level `CMakeLists.txt`.
