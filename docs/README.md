# Simpilot Documentation

本目录同时保存工程文档、用户文档、Wiki 源文件和可追溯的功能设计记录。新开发者或 Agent 应从工程文档开始；最终用户应从对应语言的用户手册或 Wiki 开始。

## Engineering Documentation

| 文档 | 用途 |
|---|---|
| [Project Takeover Audit](project-takeover-audit.md) | 2026-09-12 的仓库审计、证据、风险和未知事项 |
| [Project Map](project-map.md) | 目录、构建目标、依赖和关键调用链 |
| [Architecture](architecture.md) | 系统上下文、组件、启动、状态、并发和平台集成 |
| [Modules](modules.md) | 按功能定位模块、文件、类型、测试和已知问题 |
| [Build And Run](build-and-run.md) | 环境、Debug/Release、固定 CI、打包和运行 |
| [Testing](testing.md) | 测试、修改到测试的映射和覆盖缺口 |
| [Configuration](configuration.md) | 设置、环境变量、注册表、Everything 和 CI 配置 |
| [Data And Storage](data-and-storage.md) | 菜单、设置、缓存、图标、日志和损坏处理 |
| [Troubleshooting](troubleshooting.md) | 已实际确认的开发和 CI 故障 |
| [Project State](project-state.md) | 当前基线、技术债、Unknowns 和接管阶段报告 |
| [Architecture Decisions](decisions/README.md) | 新 ADR 的使用范围、编号和模板 |

代码任务还应阅读仓库根 `AGENTS.md` 以及 `agents/` 下与任务相关的知识文件。

## User Documentation

[简体中文文档中心](zh-CN/文档中心.md) | [English Documentation](en-US/README.md)

- [简体中文用户手册](zh-CN/用户手册.md)
- [English User Manual](en-US/User-Manual.md)
- [Simpilot Wiki](https://github.com/wonly211/Simpilot/wiki)

## Development Records

`zh-CN/development/` 和 `en-US/development/` 保存特定功能的设计演进、实现边界和人工验证要求。它们可能描述某个历史版本，不能替代当前源码、测试、构建配置和本目录中的工程状态文档。

已有的重要记录包括：

- 快捷启动菜单编辑器；
- 自定义全局热键；
- Windows 快捷键屏蔽复核；
- 键盘录制架构；
- 菜单图标、主题和人工图标；
- 设置界面设计规范；
- Wiki 内容维护计划。

## Wiki Sources

`docs/wiki/` 是面向用户的 GitHub Wiki 源文件。GitHub Wiki 不支持目录型页面 URL，因此发布脚本会把中英文源页面展平到单独的 Wiki 工作树。

键盘映射的用户入口为 `wiki/zh-CN/键盘映射.md` 和
`wiki/en-US/Keyboard-Mappings.md`；实现与线程边界以工程文档和源码为准。

```powershell
.\tools\publish-wiki.ps1 -Target .\build\wiki-publish
```

检查生成内容和链接后，再在独立 Wiki 仓库提交。不要直接修改生成副本并期待仓库源文件自动同步。

## Documentation Authority

发生冲突时按以下顺序判断：

```text
Source Code
Tests
Build Configuration
CI
Engineering Documentation
User Documentation and Historical Design Records
Assumptions
```

发现冲突必须调查和修正。无法确认的历史原因应明确写为未知。

## Phase 3 Report

### Completed

重建审计、地图、架构、模块、构建、测试、配置、存储、排障、状态和 ADR 文档入口，并保留原有中英文用户资料。

### Evidence

所有工程说明均引用当前源码目录、构建目标、CTest 名称、配置键或 2026-09-12 的实际命令结果。

### Findings

用户文档覆盖较完整，主要缺口是接管级架构、模块导航、验证矩阵和当前状态快照。

### Problems

部分历史设计文档仍使用旧版本作为“当前”描述，现已在索引中明确其权威边界。

### Unknowns

无法恢复的原始需求和历史理由继续保留在 `project-state.md` 的 Documentation Gaps。

### Next

在功能、构建或运行时边界变化时持续维护文档；远端 CI 与人工桌面验证状态见
`project-state.md`。
