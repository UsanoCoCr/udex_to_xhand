# ADR-003: config.yaml Ships Full Mapping Schema Upfront

## Context

M0 的 stub 代码不读取 `config.yaml` 中的 mapping 配置（`sources`, `weights`, `sign`, `clamp` 等）。问题是 config.yaml 应该只包含 M0 需要的字段（udcap port, xhand serial），还是提前放入完整 schema。

## Decision

**提前放入完整 schema**。config.yaml 包含 `mapping.left` / `mapping.right` 共 24 个关节映射定义，即使 M0 stub 不使用。

## Consequences

- **正面**: Schema 提前锁定，M1-M4 开发时不需要做 config migration
- **正面**: 团队可以提前 review mapping 参数假设值（来自 SPEC.md §11），在 M2 实验验证前就发现明显错误
- **正面**: `config.yaml` 作为文档——新人读一遍就知道系统所有可调参数
- **负面**: M0 阶段 config.yaml 中 ~60% 内容未被使用，可能造成"这些参数已经生效了"的误解

## Alternatives

- **渐进式添加**: 每个 milestone 只加该 milestone 需要的 config 字段——更整洁但增加 4 次 config 变更
- **使用代码默认值 + 最小 config**: 映射参数硬编码为默认值，config 只覆盖——与 CLAUDE.md "never hardcode mapping" 约束冲突
