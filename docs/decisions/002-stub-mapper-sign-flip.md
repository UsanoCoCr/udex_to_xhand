# ADR-002: Stub Mapper Applies Sign Flip Instead of Pure Passthrough

## Context

M0 的 `JointMapper` 是 stub，不做真实的加权映射。问题是 stub 是否应该完全透传（`udcap[0:12]` 直接输出），还是做最小处理（sign flip + deg→rad）。

## Decision

Stub 做 **sign flip (`*-1`) + deg→rad 转换**，而非纯透传。

```python
return [(-v) * math.pi / 180.0 for v in raw_12]
```

## Consequences

- **正面**: 输出值是合理的 XHand 弧度值（正数=屈曲），便于目视验证 main.py 输出
- **正面**: deg→rad 转换边界在 mapper 中确立——与 SPEC.md §4.2 一致，M4 替换时保持同一位置
- **负面**: 与"stub 应尽量简单"原则有轻微冲突

## Alternatives

- **纯透传**: `return udcap_24[:12]`——更简单但输出是度数且方向反，容易在后续集成时引入 bug（忘记加 sign flip）
- **完整 config-driven mapping**: 过早，M2 实验验证前 mapping 参数是假设值
