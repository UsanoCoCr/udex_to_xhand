# ADR-006: CalibrationStatus — Report but Don't Filter in receive()

## Context

SPEC.md 和 CLAUDE.md 要求 `CalibrationStatus != 3` 时跳帧。问题是这个过滤应该发生在哪里：

- A) `receive()` 内部：CalibrationStatus != 3 → 返回 None
- B) `receive()` 返回原始数据（含 calib 状态），由调用方按需过滤

## Decision

选择 **B: receive() 返回 CalibrationStatus 字段但不做过滤**。CalibrationStatus 作为返回 dict 的 `calib_left` / `calib_right` 字段传递给调用方。

## Consequences

- **正面**: 支持单手模式（`--hand left`）——如果只有左手校准完成，右手未校准不应阻止左手数据
- **正面**: standalone 模式可以显示 CalibStatus 值用于调试，即使未校准也能看到原始数据
- **正面**: 过滤策略由控制循环（main.py）决定，receiver 保持纯粹的"接收 + 解析"职责
- **负面**: M1 阶段未校准数据会被 main.py 处理（因为 main.py 还没加 CalibStatus 检查），需要在 M4/M5 补上

## Alternatives

- **receive() 内部过滤**: 更安全但粒度太粗——整帧丢弃意味着单手校准完成时也无法使用
- **Per-hand 过滤在 receive() 内**: 可行但让 receiver 知道"哪只手被选中"，耦合了 CLI 参数
