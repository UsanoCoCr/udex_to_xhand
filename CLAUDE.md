# CLAUDE.md

## What is this

UDCAP gloves → XHand dexterous hand real-time teleoperation.
Windows PC runs UDCAP (HandDriver), sends hand joint data as UDP/JSON to a Linux PC,
which maps 24 UDCAP DOF → 12 XHand DOF per hand and sends position commands via RS485.
Dual-hand (left + right). Internal tool. Acceptance = pick up a cup.
Full spec: SPEC.md.

## Architecture

```
Windows (UDCAP HandDriver)
  → UDP JSON, port 9000, 60-120Hz
Linux (this project, Python 3.10+)
  → udcap_receiver.py  : non-blocking UDP recv, parse JSON
  → joint_mapper.py    : 24→12 mapping per hand, config-driven
  → xhand_driver.py    : XHand Python SDK wrapper, RS485
  → safety.py          : watchdog, clamp, graceful shutdown
  → main.py            : control loop ~100Hz, CLI entry point
  → config.yaml        : all tunable params (mapping, PID, ports)
```

## Key data flow

- UDCAP: `l0`-`l23` (left) / `r0`-`r23` (right), degrees, negative = flexion
- XHand: 12 joints per hand, radians, positive = flexion
- Mapping sign flip + range rescale defined per-joint in config.yaml
- UDCAP param→joint mapping is in SPEC.md §3.1 but UNVERIFIED — treat as hypothesis

## XHand joints (authoritative: xhand_control_ros2.hpp:68-72)

```
0: thumb_bend    3: index_bend    6: mid_1     8: ring_1    10: pinky_1
1: thumb_rota1   4: index_1       7: mid_2     9: ring_2    11: pinky_2
2: thumb_rota2   5: index_2
```

## Commands

```bash
# Install XHand SDK (on Linux, in conda env)
conda activate xhand
pip install xhand_controller-*-cp310-cp310-*.whl

# Run teleoperation
python main.py --config config.yaml

# Test UDP receiver only (no XHand needed)
python udcap_receiver.py --port 9000

# Test XHand only (no UDCAP needed)
python xhand_driver.py --action fist
```

## Code conventions

- Python 3.10+, stdlib + pyyaml + xhand_controller only
- No ROS2 dependency — use Python SDK directly
- All mapping params in config.yaml, never hardcoded
- Units: degrees at UDCAP boundary, radians at XHand boundary, convert once in joint_mapper
- Position commands in radians: `degrees * math.pi / 180`
- XHand send_command takes HandCommand_t with 12 FingerCommand_t (id, kp, ki, kd, position, tor_max, mode)
- Default PID: kp=100, ki=0, kd=0, tor_max=300, mode=3

## Workflow

1. Read SPEC.md before starting any work
2. Plan before code — discuss approach in conversation first
3. One module at a time, test each before integrating
4. Phase order: UDP receiver → XHand driver → mapper → safety → main loop
5. Joint mapping values are experimental — expect iteration

## Constraints — do NOT

- Do NOT add wrist control (l21-l23 excluded)
- Do NOT read XHand sensors / implement force feedback
- Do NOT add GUI or visualization
- Do NOT add motion recording/playback
- Do NOT use ROS2 (even though ROS2 SDK exists in repo, it's reference only)
- Do NOT add arm control
- Do NOT hardcode joint mapping — everything through config.yaml
- Do NOT send commands to XHand before verifying hand IDs and CalibrationStatus==3
- Do NOT send commands without clamping to joint limits first

## Safety (non-negotiable)

- Watchdog: hold last position if no UDP for >200ms
- Clamp all joint positions to per-joint [min, max] before send_command
- Ctrl+C → set mode=0 (passive) → close_device
- Skip frame on JSON parse error — never send stale/corrupt data
- Startup: open device → verify IDs → set mode=3 → wait for first valid packet
