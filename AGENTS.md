# Simpilot 仓库工作规则

## Project Context

Simpilot 是面向 Windows 10/11 x64 的便携式托盘快捷启动器和全局热键管理器。项目使用 C++20、原生 Win32、CMake 与 Visual Studio 2022；运行时配置、缓存和日志位于程序目录，不依赖账户、云服务或包管理器。

主要代码边界：

- `simpilot_core`：配置、菜单模型、解析与保存、程序定位、本地化、Everything、缓存、日志和文件监听；
- `simpilot_keyboard`：标准及强制热键、低级键盘钩子、录制、物理键盘映射和 Windows 快捷键屏蔽；
- `simpilot`：托盘宿主、消息循环、菜单、设置窗口及其他 Win32 UI。

## Required Reading

开始任何代码任务前至少阅读：

- `docs/architecture.md`
- `docs/project-map.md`
- `docs/build-and-run.md`
- `docs/testing.md`
- `docs/project-state.md`

修改具体子系统前，再阅读 `docs/modules.md` 和对应的 `agents/` 文档。涉及发布或 CI 时必须阅读 `agents/build-and-release.md`。

## Source of Truth

事实来源优先级为：

```text
Source Code
Tests
Build Configuration
CI
Documentation
Assumptions
```

文档与代码、测试或构建配置冲突时，必须调查并修正文档或实现，不能静默依赖过时说明。无法从仓库确认的历史原因必须标记为未知，不得补写推测性的设计理由。

## Change Discipline

- 只修改完成当前任务所需的文件，避免无关重构、批量重命名和全仓格式化；
- 不为修复局部问题大面积重写现有 Win32 生命周期或消息处理；
- 修改公共接口、依赖、配置格式、持久化数据或发布结构前，必须说明兼容性影响；
- 不删除、跳过或放宽测试、警告、校验和错误处理来制造通过结果；
- 不吞掉本应报告的错误，不把环境问题描述为代码缺陷；
- 不覆盖或回退用户已有的未提交修改；
- 不编造产品需求、历史决策或平台保证。

## Development Workflow

每项代码任务遵循：

```text
Understand -> Locate -> Plan -> Implement -> Static Checks
           -> Build -> Tests -> Review Diff -> Report
```

先确认线程所有权、消息边界和持久化影响，再修改相关实现。Windows UI、键盘钩子、配置监听和 Everything 的特殊约束见 `agents/windows-runtime.md`。

## Verification

- Core、配置、解析或存储修改：构建并运行 `simpilot_core_tests`，随后运行完整 CTest；
- 键盘或热键修改：运行热键状态测试、键盘线程生命周期测试和完整 CTest，并记录仍需人工键盘验证的部分；
- UI 修改：运行相关窗口或菜单测试及完整 CTest，并完成交互式冒烟验证；
- 构建、打包或 CI 修改：完整运行 `tools/ci.ps1`；
- 无法执行的检查必须明确说明原因、影响和剩余风险。

不能仅凭代码审阅或“看起来正确”宣告完成。

## Documentation Rule

修改以下内容时必须同步更新相应工程文档：

- 架构、模块边界、线程或关键运行流程；
- 公共接口、配置项、数据格式或存储位置；
- 构建、测试、依赖、打包或 CI；
- 已知问题、验证基线或重要故障排查方法。

用户行为变化还必须同步检查中英文 README、用户手册和 Wiki 源文件。

## CI 工作流

1. 仓库的唯一正式 CI 入口是 `.github/workflows/ci.yml`，本地与 GitHub Actions 必须共同调用 `tools/ci.ps1`。不得在 YAML 中复制另一套构建或发布校验逻辑。

2. GitHub Actions 的 `uses:` 必须固定到完整的 40 位提交 SHA，并在同一行注释对应的发布标签。禁止使用 `@main`、`@master`、`@v4` 等可移动引用。

3. CI 固定使用 `windows-2022`、Visual Studio 2022、x64、Release 和 `ci-vs2022-x64` preset。修改 runner、生成器、架构、配置或 preset 时，必须作为独立的 CI 基础设施变更审查。

4. `tools/ci.ps1` 每次必须删除并重新创建专用的 `build/ci-vs2022-x64`，随后依次完成：

   - 验证 CMake 与 Visual Studio Release 配置；
   - Release 编译；
   - 全部自动测试；
   - Release 打包；
   - 版本资源、ZIP 白名单和 SHA-256 校验；
   - 与 `.github/ci/release-baseline.json` 中上一正式版本的 EXE、ZIP 大小比较。

5. `.github/ci/release-baseline.json` 必须记录最近一个已发布正式版本的精确产物数据。正常发布后应更新基线并清空 `approvedGrowth`。超过 5% 的合理增长只能通过填写有明确原因和上限的 `approvedGrowth` 放行，不得直接提高全局阈值或删除体积检查。

6. 修改 `.github/workflows/ci.yml`、`tools/ci.ps1`、`CMakePresets.json` 或发布基线后，必须在 Windows 环境完整运行一次 `tools/ci.ps1`，并保持工作区无意外跟踪文件变化。

## Completion Criteria

只有在实现完整、适用的静态检查和构建通过、相关及完整测试通过、必要的人工验证完成、文档同步且最终差异已审阅后，才能宣告任务完成。
