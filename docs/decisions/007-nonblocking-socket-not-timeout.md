# ADR-007: Non-blocking Socket (`setblocking(False)`) Instead of Timeout-based

## Context

`receive()` 在 100Hz 控制循环中被调用，必须快速返回。两种方式实现非阻塞 UDP 接收：

- A) `socket.settimeout(small_value)` — 例如 1ms 超时
- B) `socket.setblocking(False)` — 无数据时立即抛出 `BlockingIOError`

config.yaml 中有 `timeout_ms: 200`，是否应该用作 socket timeout？

## Decision

选择 **B: `setblocking(False)`**。`timeout_ms: 200` 保留给 M5 watchdog 使用，不用于 socket 配置。

## Consequences

- **正面**: `receive()` 在无数据时 **立即** 返回（~微秒级），不浪费控制循环时间
- **正面**: `timeout_ms` 语义清晰——它是 watchdog 超时（"多久没收到数据就报警"），不是 socket 超时（"等多久再放弃"）
- **正面**: 配合 drain loop（ADR-005），非阻塞模式自然支持循环读取直到缓冲区清空
- **负面**: 需要在 main.py 的 `data is None` 路径中确保 sleep 仍然执行（否则 busy-spin），M0 的代码有此 bug，M1 已修复

## Alternatives

- **`settimeout(0.001)`**: 功能等价但每次无数据调用浪费 1ms（10% 的循环预算）
- **`settimeout(0.2)`**: 把 watchdog 超时当 socket 超时用——语义混淆，且会阻塞控制循环 200ms
