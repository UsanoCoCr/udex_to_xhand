# ADR-008: Fix main.py Busy-spin When No UDP Data Available

## Context

M0 的 main.py 控制循环：

```python
data = receiver.receive()
if data is None:
    continue        # ← 跳过了后面的 sleep
```

Mock 模式下 `receive()` 总是返回数据，所以 `continue` 永远不会执行——bug 隐藏。Real UDP 模式下（M1），大多数 tick 无数据可读，`continue` 跳过 `time.sleep()`，导致 CPU 100% 空转。

## Decision

重构循环：将数据处理放在 `if data is not None:` 块内，`sleep` 逻辑始终执行。

```python
data = receiver.receive()
if data is not None:
    # map, clamp, send, print
    ...

# sleep 始终执行，无论是否有数据
elapsed = time.monotonic() - t_loop
sleep_time = interval - elapsed
if sleep_time > 0:
    time.sleep(sleep_time)
```

## Consequences

- **正面**: Real UDP 模式下 CPU 占用从 100% 降到 ~1%（sleep 释放时间片）
- **正面**: 控制循环始终保持 ~100Hz 节奏，无论有无数据
- **正面**: Mock 模式行为不变（`receive()` 总是返回数据，`if` 块总是执行）
- **负面**: 无数据时 tick 计数仍递增但不打印——tick 编号可能有跳跃。可接受，tick 只是调试用

## Alternatives

- **在 `continue` 前加 `time.sleep(interval)`**: 可行但重复了 sleep 逻辑，容易在维护时遗漏一处
- **保持 `continue` 但在 `receive()` 内加 sleep**: 职责混乱——receiver 不应关心控制循环节奏
