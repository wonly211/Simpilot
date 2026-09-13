# Architecture Decision Records

本目录只记录从现在开始可以被项目证据支持的重要决策。不要为已有代码补写无法确认的历史理由；这类信息应在 `docs/architecture.md` 或 `docs/project-state.md` 标记为 `Historical rationale unknown`。

## When To Create An ADR

以下变更通常需要 ADR：

- 改变主要模块或依赖方向；
- 引入新的第三方依赖、运行时或发布方式；
- 改变配置、持久化或兼容性策略；
- 改变线程、进程、IPC 或 Windows 集成模型；
- 替换构建、测试或 CI 的基本工具链；
- 接受具有长期影响的安全、性能或维护权衡。

局部 bug 修复、内部重命名和没有架构影响的 UI 调整通常不需要 ADR。

## Naming

文件名使用：

```text
ADR-001-short-title.md
ADR-002-next-decision.md
```

编号连续且不复用。标题使用简短英文文件名，正文可以使用中文。

## Status

- `Proposed`：讨论中，尚未实施；
- `Accepted`：已经批准并作为当前规则；
- `Superseded`：被后续 ADR 替代，并链接新记录；
- `Rejected`：经过讨论但未采用。

## Template

```markdown
# ADR-XXX Title

## Status

Proposed

## Context

描述已确认事实、约束和需要解决的问题。无法确认的内容明确标记。

## Decision

说明选择的方案和适用边界。

## Alternatives

列出实际考虑过的方案及证据支持的取舍。

## Consequences

记录正面影响、成本、兼容性、迁移、测试和回退要求。
```

## Review Rule

ADR 必须与实现和文档在同一变更中审查。决策变化时新增 ADR 并将旧记录标为 `Superseded`，不要静默重写已经接受的历史记录。
