# ADR-005: Drain-to-Latest Instead of One Packet per receive() Call

## Context

`receive()` is called once per control loop iteration (~100Hz). UDCAP sends at 60-120Hz. Two strategies for reading from the non-blocking UDP socket:

- A) Read one packet per call — simple, but if the loop stalls briefly (GC, OS scheduling), stale packets accumulate in the kernel buffer and the system processes outdated poses
- B) Drain loop — read all available packets, keep only the last one, parse only that

## Decision

选择 **B: Drain-to-latest**。`receive()` 内部循环调用 `recvfrom()` 直到 `BlockingIOError`，仅保留最后一个包。

```python
latest_raw = None
while True:
    try:
        data, addr = self._sock.recvfrom(65535)
        latest_raw = data
    except BlockingIOError:
        break
```

## Consequences

- **正面**: 遥操始终使用最新手势数据，不会因控制循环偶尔延迟而处理过时数据
- **正面**: 仅对最后一个包做 `json.loads` + `_parse()`，丢弃的中间包零 CPU 开销
- **正面**: 内核缓冲区不会无限增长（每次 receive() 都清空）
- **负面**: 无法统计丢弃了多少包（可在 M5 加诊断计数器）

## Alternatives

- **One-per-call**: 更简单但遥操场景下会引入不必要的延迟。如果控制循环卡顿 50ms，后续 5 帧都是旧数据
- **独立接收线程 + 共享变量**: 过度设计。Python GIL 下 UDP recvfrom 已经足够快，drain loop 在单线程内完成
