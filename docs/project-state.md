# Project State

状态日期：2026-09-13
仓库起点：`main` / `32101e8` / `0.18.5`

## Current Status

| 能力 | 状态 | 证据或限制 |
|---|---|---|
| Dependency restore | N/A | 没有包管理器；依赖由仓库和 Windows SDK 提供 |
| CMake configure | PASS | VS 2022 x64、SDK `10.0.26100.0`、MSVC `19.44.35228.0` |
| Debug build | PASS with environment caveat | `C:\Temp\Simpilot-takeover-debug-final` 全新构建通过；SynologyDrive 目录稳定复现 PDB `C1090 / error 3` |
| Debug tests | PASS | 9/9，0 失败，0 跳过，1.17 秒 |
| Release build | PASS | 固定 CI 全新重建 `build/ci-vs2022-x64` |
| Release tests | PASS | 9/9，0 失败，0 跳过，1.44 秒 |
| Release configuration | PASS | `/O2 /Ob2 /DNDEBUG`、`/INCREMENTAL:NO`、`MaxSpeed`、`LinkIncremental=false` |
| Package | PASS | 版本资源、ZIP 白名单和自身 `.sha256` 已验证 |
| Local fixed CI | PASS | `pwsh -NoProfile -File .\tools\ci.ps1` |
| Vendor source removal | PASS | 第三方键盘管理器目录已从文件系统、构建输入和生成工程移除；CI 有残留门禁 |
| Interactive package smoke | PARTIAL | 公开 `v0.18.7` ZIP 的完整性、目录和版本资源已确认；此前公开 `v0.18.6` ZIP 已验证启动、便携目录、键盘线程、菜单加载、配置监听和 Everything 就绪。当前已有另一份 Simpilot 正在运行，未中断用户进程以重复启动测试；菜单、设置和正常退出仍需人工验证 |
| GitHub Actions run | PASS / external follow-up | `v0.18.7` PR run `34748960402` 与合并后 `main` run `34749258736` 成功；标签 run `34749357606` 因 GitHub Actions 官方故障仍在排队 |
| Required branch check | PASS | `main` 已要求 `Windows x64 Release`，`strict=true`；检查绑定 GitHub Actions app `15368` |
| GitHub Release | PASS | `v0.18.7` 已公开并设为 Latest；发布 ZIP 与 `.sha256` 已从 Release 重新下载复核 |

## Verified Baseline

```text
Version: 0.18.7
Platform: Windows x64
Generator: Visual Studio 17 2022
MSVC: 19.44.35228.0
Windows SDK: 10.0.26100.0

Debug build: PASS in fresh non-synchronized directory
Debug tests: 9/9 PASS

Release build: PASS
Release tests: 9/9 PASS
Package: PASS

Previous v0.18.6 Simpilot.exe:           1,156,608 bytes
Official v0.18.7 Simpilot.exe:           1,170,432 bytes
EXE delta:                                  13,824 bytes (1.1952%)

Previous v0.18.6 ZIP:                    2,534,696 bytes
Official v0.18.7 ZIP:                    2,540,392 bytes
ZIP delta:                                   5,696 bytes (0.2247%)
```

正式 `v0.18.7` ZIP SHA-256：

```text
23F7188A4C1F660692C6BF8F3BE8A77FE86CE5C686FDC2B51871B292992B6AC1
```

GitHub Actions `main` run `34749258736` 上传的 artifact 名称为
`Simpilot-e77f094a7c5e969db701b0625e42ad552ad0c1d7-win-x64`。下载后确认其中仅有
`Simpilot-0.18.7-win-x64.zip` 与对应 `.sha256`；该 ZIP 随后作为正式 Release 资产公开。
从公开 Release 重新下载后，ZIP 仍为 `2,540,392` 字节，SHA-256 为：

```text
23F7188A4C1F660692C6BF8F3BE8A77FE86CE5C686FDC2B51871B292992B6AC1
```

`v0.18.7` 的结构化键盘映射编辑器使 EXE 和 ZIP 相对 `v0.18.6` 分别增长 `1.1952%`
和 `0.2247%`，无需 `approvedGrowth`。正式发布后已经把上述真实产物写为新基线，
`approvedGrowth` 保持为空；全局 5% 阈值没有提高。

ZIP 哈希只描述本次构建。CPack ZIP 的时间戳或压缩元数据可能变化，因此不同构建不要求
哈希相同；内容白名单、自身校验和和体积门禁必须通过。

## v0.18.7 Keyboard Mapping Editor Fix

键盘映射编辑器已经取消 `vk:scan:extended` 原始文本输入，录制结果和结构化下拉选择
共同写入 `KeyboardMappingEditorModel`。源端提供四个左右修饰键槽、主键和可选 chord
动作键，目标端提供四个修饰键槽和主键；动作键目录覆盖 `F1` 至 `F24`，并保留目录外
录制键的完整物理身份。该修改修复了旧编辑器生成 `=>`、解析器却无法读回而造成的
快捷键和 chord 保存失败。

2026-09-13 验证结果：Release 和非同步全新 Debug 均为 9/9 测试通过，PR 与合并后
`main` 的固定 GitHub Actions 流水线通过。正式 Release 资产的 `Simpilot.exe` 为
`1,170,432` 字节，ZIP 为 `2,540,392` 字节；ZIP 白名单、版本资源 `0.18.7.0` 和
旁车 SHA-256 均已通过重新下载复核。相对正式 `v0.18.6` 基线，EXE 增长 `1.1952%`，
ZIP 增长 `0.2247%`，无需 `approvedGrowth`。正式基线回写后的本地固定 CI 再次通过：
EXE 与基线相同，重建 ZIP 为 `2,540,193` 字节，比正式资产少 `199` 字节（`-0.0078%`）。

## Keyboard Mapping State

- 键盘录制、热键处理和物理键盘映射均为 Simpilot 独立实现；仓库不再包含外部键盘管理器源码；
- 保留既有 `KeyboardManager` 热键/线程行为，并新增映射录制和同步规则替换接口；
- `[KeyboardMappings]` 是向后兼容的可选设置节；旧配置没有该节时规则为空，既有行为不变；
- 源支持单键、快捷键和两个同时按住动作键的单级 chord；目标支持单键或标准快捷键；
- 映射编辑器支持录制和结构化下拉选择，左右修饰键、主键盘/数字键盘 Enter 及目录外录制键保持物理区分；
- 运行时使用 16 项固定前缀队列、250 ms 键盘线程定时器、独立 target/replay 标记和失败诊断；
- 自动测试不使用真实 `SendInput` 或物理键盘，真实桌面、UIPI 和安全桌面仍是人工边界。

## Known Problems

### High

- 第二实例使用 `FindWindowExW(HWND_MESSAGE, ...)` 查找首实例，但托盘宿主是隐藏顶层窗口；第二实例可能无法请求首实例显示菜单。
- Everything 服务维护和部分程序查询在 UI 线程同步运行，可能长时间阻塞托盘消息循环。
- 键盘线程控制超时后的停止路径最终可能无限等待，真实挂死会阻止正常退出。

### Medium

- 键盘健康检查只确认线程存活，不能检测 Windows 静默移除低级钩子。
- 键盘映射依赖低级钩子和 `SendInput`；UIPI、权限差异或安全桌面可能使目标注入或源事件回放失败。
- 设置应用跨菜单文件、`Setting.ini`、图标和运行时热键，依赖复杂的补偿式回滚。
- `settings_window.cpp`、`menu_editor_window.cpp` 和 `tray_application.cpp` 是大型手写 Win32 生命周期热点。
- 菜单暗色模式使用 `uxtheme.dll` 未公开入口；已有回退，但仍受 Windows 内部变化影响。
- 文件夹选择使用旧 Shell API，UI 线程 COM 初始化前置条件尚未完整确认。
- 进程恢复依赖路径字符串和启发式窗口选择，可能受路径别名、权限或辅助窗口影响。

### Low

- 进程内图标缓存命中后不总是重新检查源文件时间，运行期间可能显示旧图标。
- 历史设计文档中的“当前版本”文字可能仍指向 `0.18.0`，需要读者结合本状态文档判断。

## Technical Debt

- `TrayApplication` 集中协调过多运行时职责，但当前仍是明确所有权中心；任何拆分必须独立设计和回归。
- 原生 Win32 UI 重复管理字体、DPI、控件、布局和模态消息循环，维护成本较高。
- 映射规则表、前缀回放和目标按键生命周期集中在 `KeyboardMappingEngine`，尚无真实物理键端到端自动覆盖。
- 设置保存跨多个文件和运行时状态，没有统一提交介质。
- Everything 查询和元数据读取缺少后台任务边界。
- CI 有 Release 质量门禁，但没有 clang-format、clang-tidy、MSVC 独立静态分析或自动化桌面测试。

## Unknowns And Documentation Gaps

- 原始产品需求、完整路线图和多数早期架构选择的历史原因；
- 数字签名、安装器或未来发布渠道策略；
- 完整支持的 Windows build、SKU、权限级别和远程桌面矩阵；
- Debug PDB 失败具体由同步客户端、过滤驱动还是安全软件触发；
- 没有正式性能基线、完整安全威胁模型或自动化 Windows 桌面兼容矩阵；
- `v0.18.7` 发布包的版本、ZIP 内容与完整性已验证；因当前已有另一份 Simpilot 正在运行，未中断用户进程以重复启动。真实按键、托盘菜单、设置交互、正常退出及退出后的按键恢复仍需在可用的交互式桌面会话中验证。

## Takeover Phase Reports

### Phase 4 — Agent Governance

**Completed**：根 `AGENTS.md` 仅保留原文件 CI 工作流约束，并加入经审计确认的通用治理规则；创建四份 `agents/` 知识文件。
**Evidence**：`AGENTS.md`、`agents/architecture.md`、`agents/build-and-release.md`、`agents/testing.md`、`agents/windows-runtime.md`。
**Findings**：规则明确了源码优先级、线程所有权、固定 CI 入口和真实键盘/UI 验证边界。
**Problems**：原发布约束和事故记录已按所有者要求删除。
**Unknowns**：未来是否需要独立安全规则文件，需在形成实际安全模型后决定。
**Next**：在后续架构、构建或运行时变化中维护这些规则。

### Phase 5 — CI

**Completed**：建立唯一 GitHub Actions workflow、独立 CMake presets、统一 PowerShell 入口、供应源码残留检查和结构化产物基线；本地与 GitHub Actions 完整运行通过，`main` 已启用必需检查。
**Evidence**：`.github/workflows/ci.yml`、`.github/ci/release-baseline.json`、`tools/ci.ps1`、`CMakePresets.json`；9/9 测试、PR run `34748960402`、`main` run `34749258736`、公开 `v0.18.7` Release 产物摘要与 `main` 分支保护 API 返回值。
**Findings**：`v0.18.7` 的 EXE 与 ZIP 相对 `v0.18.6` 均未超过 5%；正式发布后已建立新基线且 `approvedGrowth` 为空。
GitHub Actions 固定到官方 `checkout v7.0.1` 与 `upload-artifact v7.0.1` 的完整提交 SHA，二者原生使用 Node 24。
**Problems**：GitHub 于 2026-09-13 报告 Actions 性能下降，`v0.18.7` 标签 run `34749357606` 已创建但尚未分配 job；同一提交的 PR 与 `main` 运行已通过。
**Unknowns**：标签 run 的最终完成时间取决于 GitHub 托管服务恢复；未来 hosted runner 镜像更新仍可能改变工具链小版本，workflow 固定的 runner 系列保持不变。
**Next**：后续正式版本发布后继续更新产物基线并清空当次 `approvedGrowth`。

### Phase 6 — Final Validation

**Completed**：Release、Debug 非同步构建、9 项 CTest、固定 CI、版本资源、ZIP 内容、SHA-256、体积门禁和供应源码残留门禁均通过。
**Evidence**：本文件 Verified Baseline 与 `build/ci-vs2022-x64`；同步目录 Debug 的 `C1090` 已由非同步全新构建排除为代码缺陷。
**Findings**：没有依赖升级、配置格式或发布包目录结构变化；计划内版本更新为 `0.18.7`，可选映射配置保持旧用户行为。
**Problems**：`v0.18.7` 发布包的静态产物验证完成，但当前已有另一份 Simpilot 正在运行，未中断用户进程以执行重复启动；此前自动化桌面会话也无法验证菜单、设置和正常退出。
**Unknowns**：真实物理键、权限边界和桌面兼容矩阵的结果。
**Next**：在可用的交互式桌面会话中完成剩余发布包冒烟。
