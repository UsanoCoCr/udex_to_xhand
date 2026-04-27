# ADR-001: UDCAP Parameter Lookup by Name, Not Array Index

## Context

UDCAP UDP JSON 的 `Parameter` 数组包含 68 个 `{Name, Value}` 对象。有两种方式提取 `l0`-`l23`:
- A) 按数组下标（l0 = index 1, l1 = index 2, ...）
- B) 按 `Name` 字段匹配（`"l0"`, `"l1"`, ...）

## Decision

选择 **B: 按 Name 字段匹配**。`_parse()` 先构建 `{name: value}` dict，再按 `f"l{i}"` 查找。

## Consequences

- **正面**: 即使 UDCAP 软件版本更新改变数组顺序、插入新参数，代码不受影响
- **正面**: 代码自文档化——`lookup["l5"]` 比 `params[6]["Value"]` 更清晰
- **负面**: 每帧多一次 O(68) dict 构建，但在 100Hz 循环中开销可忽略（~10μs）

## Alternatives

- **按数组下标**: 更快但脆弱。example.json 中 `L_CalibrationStatus` 在 index 0，`l0` 在 index 1——如果 UDCAP 新增字段，所有下标偏移
