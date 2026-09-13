# Project State

状态日期：2026-09-13
仓库起点：`main` / `32101e8` / `0.18.5`

## Current Status

| 能力 | 状态 | 证据或限制 |
|---|---|---|
| Dependency restore | N/A | 没有包管理器；依赖由仓库和 Windows SDK 提供 |
| CMake configure | PASS | VS 2022 x64、SDK `10.0.26100.0`、MSVC `19.44.35228.0` |
| Debug build | PASS with environment caveat | `C:\Temp\Simpilot-takeover-debug-final` 全新构建通过；SynologyDrive 目录稳定复现 PDB `C1090 / error 3` |
| Debug tests | PASS | 9/9，0 失败，0 跳过，1.10 秒 |
| Release build | PASS | 固定 CI 全新重建 `build/ci-vs2022-x64` |
| Release tests | PASS | 9/9，0 失败，0 跳过，1.01 秒 |
| Release configuration | PASS | `/O2 /Ob2 /DNDEBUG`、`/INCREMENTAL:NO`、`MaxSpeed`、`LinkIncremental=false` |
| Package | PASS | 版本资源、ZIP 白名单和自身 `.sha256` 已验证 |
| Local fixed CI | PASS | `pwsh -NoProfile -File .\tools\ci.ps1` |
| Vendor source removal | PASS | 第三方键盘管理器目录已从文件系统、构建输入和生成工程移除；CI 有残留门禁 |
| Interactive package smoke | PARTIAL | 从最终 ZIP 解压后可启动，便携 `Config/`、`Log/`、键盘线程、菜单加载、配置监听和 Everything 就绪均已确认；当前自动化桌面会话中托盘图标注册返回 `2147500037`，菜单、设置和正常退出未验证 |
| GitHub Actions run | NOT VERIFIED | workflow 尚未提交/推送，无法验证远端 runner 与上传产物 |
| Required branch check | NOT CONFIGURED | 首次远端成功后仍需仓库管理员把 `Windows x64 Release` 设为必需检查 |

## Verified Baseline

```text
Version: 0.18.5
Platform: Windows x64
Generator: Visual Studio 17 2022
MSVC: 19.44.35228.0
Windows SDK: 10.0.26100.0

Debug build: PASS in fresh non-synchronized directory
Debug tests: 9/9 PASS

Release build: PASS
Release tests: 9/9 PASS
Package: PASS

Official v0.18.5 Simpilot.exe baseline: 1,019,904 bytes
Current CI Simpilot.exe:                 1,156,608 bytes
EXE delta:                                 136,704 bytes (13.4036%)

Official v0.18.5 ZIP baseline:           2,474,630 bytes
Current CI ZIP:                          2,534,502 bytes
ZIP delta:                                  59,872 bytes (2.4194%)
```

当前固定 CI 生成的 ZIP SHA-256：

```text
C0B75B410095FF7C1D0F0C169F03206276A8E191C097C4375048EE9C2F61E2D0
```

EXE 因新增独立物理键映射引擎、录制状态机、运行时保护、设置 UI、语言资源和测试支持而
超过全局 5% 阈值。`.github/ci/release-baseline.json` 使用有理由的 `approvedGrowth`，上限
固定为 EXE `1,170,000` 字节、ZIP `2,550,000` 字节；全局阈值没有提高。正式发布新版本
后应把真实产物写为新基线并清空该批准记录。

ZIP 哈希只描述本次构建。CPack ZIP 的时间戳或压缩元数据可能变化，因此不同构建不要求
哈希相同；内容白名单、自身校验和和体积门禁必须通过。

## Keyboard Mapping State

- 键盘录制、热键处理和物理键盘映射均为 Simpilot 独立实现；仓库不再包含外部键盘管理器源码；
- 保留既有 `KeyboardManager` 热键/线程行为，并新增映射录制和同步规则替换接口；
- `[KeyboardMappings]` 是向后兼容的可选设置节；旧配置没有该节时规则为空，既有行为不变；
- 源支持单键、快捷键和两个同时按住动作键的单级 chord；目标支持单键或标准快捷键；
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
- GitHub 仓库规则集、远端首次 Actions 结果及维护者是否有权限设置必需检查；
- 没有正式性能基线、完整安全威胁模型或自动化 Windows 桌面兼容矩阵；
- 发布包启动和便携目录已验证；真实按键、托盘菜单、设置交互、正常退出及退出后的按键恢复仍需在可用的交互式桌面会话中验证。

## Takeover Phase Reports

### Phase 4 — Agent Governance

**Completed**：根 `AGENTS.md` 仅保留原文件 CI 工作流约束，并加入经审计确认的通用治理规则；创建四份 `agents/` 知识文件。
**Evidence**：`AGENTS.md`、`agents/architecture.md`、`agents/build-and-release.md`、`agents/testing.md`、`agents/windows-runtime.md`。
**Findings**：规则明确了源码优先级、线程所有权、固定 CI 入口和真实键盘/UI 验证边界。
**Problems**：原发布约束和事故记录已按所有者要求删除。
**Unknowns**：未来是否需要独立安全规则文件，需在形成实际安全模型后决定。
**Next**：在后续架构、构建或运行时变化中维护这些规则。

### Phase 5 — CI

**Completed**：建立唯一 GitHub Actions workflow、独立 CMake presets、统一 PowerShell 入口、供应源码残留检查和结构化产物基线；本地完整运行通过。
**Evidence**：`.github/workflows/ci.yml`、`.github/ci/release-baseline.json`、`tools/ci.ps1`、`CMakePresets.json`；9/9 测试与上述产物摘要。
**Findings**：当前 EXE 超过历史基线 5%，已使用有理由且有上限的批准记录；ZIP 未超过 5%。
**Problems**：远端 workflow、产物下载和分支必需检查尚不能在未提交状态验证。
**Unknowns**：GitHub runner 首次执行结果与仓库管理权限。
**Next**：推送后观察 `Windows x64 Release`，复核上传产物并配置分支规则。

### Phase 6 — Final Validation

**Completed**：Release、Debug 非同步构建、9 项 CTest、固定 CI、版本资源、ZIP 内容、SHA-256、体积门禁和供应源码残留门禁均通过。
**Evidence**：本文件 Verified Baseline 与 `build/ci-vs2022-x64`；同步目录 Debug 的 `C1090` 已由非同步全新构建排除为代码缺陷。
**Findings**：没有依赖升级、版本变更或发布包目录结构变化；新增的可选映射配置保持旧用户行为。
**Problems**：发布包启动已验证，但当前自动化桌面会话中托盘图标注册失败，无法验证菜单、设置和正常退出；远端 CI 和主分支必需检查尚未验证。
**Unknowns**：真实物理键、权限边界、远端 runner 和桌面兼容矩阵的结果。
**Next**：在可用的交互式桌面会话中完成剩余发布包冒烟；提交推送后完成远端验证和分支保护。
