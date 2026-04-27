# Verified UDCAP Parameter Mapping

**Date**: 2026-04-27
**Source**: [UDCAP JSON SDK 关节角度使用手册](https://udexreal.gitbook.io/udexreal-docs/docs-cn/c++-python-sdk/json-c++python-sdk-guan-jie-jiao-du-shi-yong-shou-ce) (official documentation)
**Previous source**: `scripts/udcap_param_identify.py` experimental results (superseded — noisy due to cross-talk)

---

## Correction Summary

SPEC.md §3.1 had two major errors:

1. **Parameter ordering was wrong**: SPEC assumed proximal→distal (MCP→PIP→DIP). Actual ordering is **distal→proximal** (DIP→PIP→MCP Pitch→MCP Yaw) per finger.
2. **l21-l23 were not wrist**: l20/l21/l22 are MCP Roll angles (thumb/index/pinky). l23 is a gesture recognition flag (always -1). **No wrist parameters exist in the JSON SDK**.

---

## Official Parameter Mapping (l0-l27 / r0-r27)

Left and right hands share identical structure. Right hand uses `r` prefix.

### Thumb (l0-l3, l20)

| Index | Joint | Axis | Description |
|-------|-------|------|-------------|
| l0 | Thumb IP (3rd joint) | Pitch | 拇指第3关节俯仰角 (DIP) |
| l1 | Thumb MCP (2nd joint) | Pitch | 拇指第2关节俯仰角 (PIP) |
| l2 | Thumb CMC (1st joint) | Pitch | 拇指第1关节俯仰角 (MCP) |
| l3 | Thumb CMC (1st joint) | Yaw | 拇指第1关节偏航角 (abduction) |
| l20 | Thumb CMC (1st joint) | Roll | 拇指第1关节旋转角 (opposition) |

### Index (l4-l7, l21)

| Index | Joint | Axis | Description |
|-------|-------|------|-------------|
| l4 | Index DIP (3rd joint) | Pitch | 食指第3关节俯仰角 |
| l5 | Index PIP (2nd joint) | Pitch | 食指第2关节俯仰角 |
| l6 | Index MCP (1st joint) | Pitch | 食指第1关节俯仰角 |
| l7 | Index MCP (1st joint) | Yaw | 食指第1关节偏航角 (abduction) |
| l21 | Index MCP (1st joint) | Roll | 食指第1关节旋转角 |

### Middle (l8-l11)

| Index | Joint | Axis | Description |
|-------|-------|------|-------------|
| l8 | Middle DIP (3rd joint) | Pitch | 中指第3关节俯仰角 |
| l9 | Middle PIP (2nd joint) | Pitch | 中指第2关节俯仰角 |
| l10 | Middle MCP (1st joint) | Pitch | 中指第1关节俯仰角 |
| l11 | Middle MCP (1st joint) | Yaw | 中指第1关节偏航角 |

### Ring (l12-l15)

| Index | Joint | Axis | Description |
|-------|-------|------|-------------|
| l12 | Ring DIP (3rd joint) | Pitch | 无名指第3关节俯仰角 |
| l13 | Ring PIP (2nd joint) | Pitch | 无名指第2关节俯仰角 |
| l14 | Ring MCP (1st joint) | Pitch | 无名指第1关节俯仰角 |
| l15 | Ring MCP (1st joint) | Yaw | 无名指第1关节偏航角 |

### Pinky (l16-l19, l22)

| Index | Joint | Axis | Description |
|-------|-------|------|-------------|
| l16 | Pinky DIP (3rd joint) | Pitch | 小指第3关节俯仰角 |
| l17 | Pinky PIP (2nd joint) | Pitch | 小指第2关节俯仰角 |
| l18 | Pinky MCP (1st joint) | Pitch | 小指第1关节俯仰角 |
| l19 | Pinky MCP (1st joint) | Yaw | 小指第1关节偏航角 |
| l22 | Pinky MCP (1st joint) | Roll | 小指第1关节旋转角 |

### Non-Joint Parameters

| Index | Type | Description |
|-------|------|-------------|
| L_CalibrationStatus | Status | -1=uncalibrated, 0=fist, 1=fingers together, 2=spread, 3=complete |
| l23 | Gesture | 手势识别保留位 (always -1) |
| l24 | IMU | Quaternion W |
| l25 | IMU | Quaternion X |
| l26 | IMU | Quaternion Y |
| l27 | IMU | Quaternion Z |

---

## UDCAP → XHand Mapping (corrected source indices)

| XHand Joint | ID | UDCAP Sources | Mapping |
|-------------|----|--------------|---------|
| thumb_bend | J0 | l0 (DIP) + l1 (PIP) + l2 (MCP Pitch) | Weighted sum of 3 flexion joints |
| thumb_rota1 | J1 | l3 (MCP Yaw) | Direct — thumb abduction |
| thumb_rota2 | J2 | l20 (MCP Roll) | Direct — thumb opposition |
| index_bend | J3 | l7 (MCP Yaw) | Direct — lateral spread |
| index_joint1 | J4 | l6 (MCP Pitch) | Direct — proximal flexion |
| index_joint2 | J5 | l5 (PIP) + l4 (DIP) | Weighted sum — coupled distal flexion |
| mid_joint1 | J6 | l10 (MCP Pitch) | Direct — proximal flexion |
| mid_joint2 | J7 | l9 (PIP) + l8 (DIP) | Weighted sum — coupled distal flexion |
| ring_joint1 | J8 | l14 (MCP Pitch) | Direct — proximal flexion |
| ring_joint2 | J9 | l13 (PIP) + l12 (DIP) | Weighted sum — coupled distal flexion |
| pinky_joint1 | J10 | l18 (MCP Pitch) | Direct — proximal flexion |
| pinky_joint2 | J11 | l17 (PIP) + l16 (DIP) | Weighted sum — coupled distal flexion |

### Discarded UDCAP Parameters (no XHand DOF)

| Index | Joint | Reason |
|-------|-------|--------|
| l11 | Middle MCP Yaw | XHand has no middle finger spread DOF |
| l15 | Ring MCP Yaw | XHand has no ring finger spread DOF |
| l19 | Pinky MCP Yaw | XHand has no pinky finger spread DOF |
| l21 | Index MCP Roll | XHand has no index roll DOF |
| l22 | Pinky MCP Roll | XHand has no pinky roll DOF |
| l23 | Gesture flag | Not a joint — always -1 |

---

## Comparison: SPEC.md Hypothesis vs Official Docs

| Aspect | SPEC.md §3.1 (hypothesis) | Official Documentation |
|--------|---------------------------|----------------------|
| Thumb params | l0-l4 (5 params, CM Pitch/Yaw/Roll, MP, IP) | l0-l3 + l20 (4 contiguous + 1 roll at end) |
| Intra-finger order | Proximal→Distal (MCP→PIP→DIP) | **Distal→Proximal (DIP→PIP→MCP)** |
| l4 | Thumb IP Pitch | **Index DIP Pitch** |
| l20 | Pinky DIP Pitch | **Thumb MCP Roll** |
| l21-l23 | Wrist (Pitch, Yaw, Roll) | **l21=Index Roll, l22=Pinky Roll, l23=Gesture** |
| Wrist data | l21-l23 (excluded) | **Does not exist in JSON SDK** |
| Total joint params | 24 (l0-l23) | **23 (l0-l22), l23 is gesture flag** |
