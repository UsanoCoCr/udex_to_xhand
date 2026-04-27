# ADR-004: xhand_driver.send() Is No-op; main.py Owns Printing

## Context

M0 stub 需要某种方式让用户看到 pipeline 输出。两个选择：
- A) `XHandDriver.send()` 内部 print 关节值
- B) `XHandDriver.send()` 是 no-op，`main.py` 循环中负责格式化和 print

## Decision

选择 **B: main.py 负责 print**。`XHandDriver.send()` 是完全的 no-op。

## Consequences

- **正面**: M3 替换为真实 SDK 调用时，`send()` 的接口和行为都是纯粹的"发送命令"，不需要删除 print 逻辑
- **正面**: main.py 可以控制输出格式（L + R 在同一行、tick 编号等），driver 不需要知道显示需求
- **正面**: 未来加 logging 时，日志逻辑集中在 main.py 而非分散在各模块
- **负面**: `xhand_driver.py --mock --action fist` 独立运行时需要自己的 `__main__` print 逻辑（已实现）

## Alternatives

- **Driver 内部 print**: 更直观但 M3 时必须删除，且 driver 耦合了 I/O 职责
- **回调/事件模式**: 过度设计，M0 不需要
