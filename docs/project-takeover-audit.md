# Project Takeover Audit

审计日期：2026-09-12
审计基线：`main`，提交 `32101e8`，项目版本 `0.18.5`

## 结论标记

- **已确认事实**：可由源码、构建配置、测试或实际命令直接验证；
- **高可信推断**：由多个代码证据共同支持，但仓库没有直接声明；
- **未确认事项**：当前仓库证据不足，需要项目所有者或真实环境确认。

## 项目概况

**已确认事实**

Simpilot 是 Windows 10/11 x64 的便携式托盘快捷启动器和全局热键管理器。它使用 C++20、Unicode Win32、CMake 及 Visual Studio 2022，生成单个主程序 `Simpilot.exe`，并随包携带 Everything 运行组件。

项目不使用 vcpkg、Conan、NuGet 或联网依赖恢复。主要第三方代码和二进制位于 `third_party/`：

- Everything `1.4.1.1032` 客户端、SDK DLL、语言文件和配置；
- nlohmann/json 单头文件库。

主程序入口是 `src/app/main.cpp` 中的 `wWinMain`。构建入口是 `CMakeLists.txt` 与 `CMakePresets.json`，测试由 CTest 注册，发布包由 CPack 生成 ZIP。

## 仓库结构

| 路径 | 职责 | 结论 |
|---|---|---|
| `include/simpilot/` | 核心模型、配置、解析、Everything、缓存、日志等接口 | 已确认事实 |
| `src/core/` | 与完整托盘 UI 分离的核心实现 | 已确认事实 |
| `src/app/` | 托盘协调器、键盘线程及全部原生 Win32 UI | 已确认事实 |
| `tests/` | 审计基线时 7 个 CTest 可执行测试的源码；接管实施后新增键盘录制和映射专项测试 | 已确认事实 |
| `resources/` | 图标、RC、manifest 与版本资源模板 | 已确认事实 |
| `Languages/` | 三种内置语言的 JSON 源文件 | 已确认事实 |
| `third_party/` | 仓库内置第三方源码及 Everything 发布文件 | 已确认事实 |
| `tools/` | 语言包构建器、Wiki 发布及固定 CI 脚本 | 已确认事实 |
| `docs/` | 用户手册、Wiki 源、历史设计记录和工程文档 | 已确认事实 |
| `.github/` | GitHub Actions 工作流和发布体积基线 | 本地固定 CI 与 `v0.18.6` 标签 run `34732966160` 已验证；`main` 已要求 `Windows x64 Release` |

## 构建、测试与打包

**已确认事实**

- CMake 最低版本为 `3.24`；生成器为 `Visual Studio 17 2022`，平台为 `x64`；
- `CMAKE_CONFIGURATION_TYPES` 固定为 `Debug;Release`；
- 所有自有 C++ 目标使用 `/W4 /permissive- /utf-8 /EHsc`；
- Release 可信配置应生成 `/O2 /Ob2 /DNDEBUG` 和 `/INCREMENTAL:NO`；
- CPack ZIP 白名单为 `Simpilot.exe`、Everything 四文件、`LICENSE`、`THIRD-PARTY-NOTICES.txt`；
- 2026-09-12 的独立干净 Release 构建、审计基线 7/7 测试和打包已经成功；键盘映射独立实现后，2026-09-13 的固定 CI 为 9/9；
- 可信重建结果为 EXE `1,019,904` 字节、ZIP `2,474,631` 字节；正式 `v0.18.5` ZIP 基线为 `2,474,630` 字节。

新增能力后的当前 CI 产物为 EXE `1,156,608` 字节、ZIP `2,534,502` 字节；这些数字
不覆盖 Phase 2 历史基线，体积批准和当前 SHA-256 见 `docs/project-state.md`。

现有 `build/vs2022-x64` 包含多个历史版本和已移除目标产生的文件，且曾指向已经移除的 Visual Studio Build Tools 实例，因此不能作为可信基线。

## 运行时架构

**已确认事实**

主进程内存在三个主要执行线程：

1. UI 线程：托盘窗口、菜单、设置、对话框、Everything 协调及主消息循环；
2. 键盘线程：消息窗口、唯一常驻 `WH_KEYBOARD_LL`、录制、物理键盘映射和强制热键；
3. 配置监听线程：等待目录变化并向 UI 线程投递重载消息。

长生命周期状态由 `TrayApplication` 协调。配置监听线程不直接修改 UI 状态；键盘线程通过 Windows 消息与 UI 线程通信；日志内部使用互斥锁。

## 关键流程

### 启动

```text
wWinMain
-> 设置 Per-Monitor V2 DPI
-> 创建 Local\Simpilot 互斥量
-> 构造 TrayApplication
-> 创建隐藏托盘宿主窗口
-> 启动键盘线程和配置监听
-> 初始化 Everything
-> 加载并解析菜单
-> 注册热键
-> GetMessageW 主循环
```

### 菜单加载与命令执行

```text
Config/Simpilot.ini
-> read_configuration_text / MenuParser
-> MenuDocument
-> VariableExpander / ParsedCommand
-> ProgramResolver
-> Windows 路径、PATH、缓存、Everything
-> LaunchMenuRenderer
-> TrackPopupMenu
-> ShellExecuteW
```

### 设置保存

```text
SettingsWindow 草稿
-> 校验菜单、设置和键盘映射
-> 预构建并替换映射运行时
-> 原子写入菜单和 Setting.ini
-> 更新图标缓存及其他运行时设置和热键
-> 失败时执行补偿式回滚
```

### 配置刷新

```text
ReadDirectoryChangesW
-> 文件大小、时间和内容哈希比较
-> PostMessage(WM_APP + 2)
-> UI 线程重新解析
-> 成功后整体替换旧 MenuDocument
```

## 配置、存储和外部集成

**已确认事实**

- 运行时数据全部位于 `Simpilot.exe` 同级目录；
- `Config/Simpilot.ini` 与可选的 `Simpilot2.ini` 保存菜单；
- `Config/Setting.ini` 保存语言、主题、启动项、功能热键、快捷键屏蔽、自定义热键及可选 `[KeyboardMappings]`；
- `Cache/program-cache.tsv` 保存无完整路径程序的解析结果；
- `Cache/RunIcon/` 保存自动提取和人工指定的 128x128 ICO；
- `Log/Simpilot.log` 使用 UTF-8 时间戳行，启动时删除 90 天以前的记录；
- `Language.lng` 是可选外部语言包；
- 唯一注册表写入是当前用户 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 的 `Simpilot` 启动项；
- Everything 通过动态加载 `Everything64.dll`、默认实例 IPC、SCM 和可选提权服务安装与系统交互；
- 项目没有自有网络协议、账户、远端 API、端口或 secret 配置。

## 测试现状

**已确认事实**

审计基线的 CTest 注册 7 个测试：核心逻辑、本地化资源、Windows 快捷键屏蔽、键盘线程生命周期、菜单呈现、程序候选窗口和关于窗口。2026-09-12 的 Release 测试结果为 7/7 通过，无跳过。接管实施后增加 `simpilot_keyboard_capture_state_tests` 和 `simpilot_keyboard_mapping_tests`，当前注册总数为 9；2026-09-13 的 Release 与 Debug 均为 9/9 通过，无失败或跳过。

**已确认缺口**

- 没有完整 `TrayApplication` 端到端测试；
- 没有第二实例交接测试；
- 没有真实键盘输入、钩子静默丢失或线程挂死注入测试；
- 没有真实 Everything IPC、服务修复和 UAC 测试；
- UI 测试不能覆盖全部多显示器、DPI、托盘和嵌套消息循环行为。

## 已知问题与风险

1. **高：第二实例交接失配。** `main.cpp` 在 `HWND_MESSAGE` 范围查找首实例，但 `TrayApplication` 创建的是隐藏顶层窗口。第二实例会退出，但可能无法通知首实例显示菜单。
2. **高：Everything UI 阻塞。** 服务修复和部分查询从 UI 线程同步执行，最坏路径可长时间阻塞消息循环。
3. **高：键盘线程停止可能挂起。** 控制消息超时后的停止路径最终使用无限等待，真实线程挂死可能阻止退出。
4. **中：钩子健康检查存在盲区。** 当前检查线程句柄，不能确认 Windows 是否静默移除了低级钩子。
5. **中：设置提交复杂。** 菜单、设置、图标和运行时状态跨多个资源域，依赖手工补偿式事务。
6. **中：Win32 UI 维护热点。** `settings_window.cpp`、`menu_editor_window.cpp` 和 `tray_application.cpp` 较大，并包含手工 HWND/GDI 生命周期。
7. **中：平台兼容风险。** 暗色菜单依赖未公开的 `uxtheme.dll` 序号入口；文件夹选择的 COM 初始化前置条件尚未完整验证。

本次接管只记录上述问题，不修改业务实现。

## 文档审计

**已确认事实**

现有 README、用户手册和 Wiki 对最终用户行为覆盖较完整；`docs/zh-CN/development/` 保存多个功能的设计演进和人工验收规则。

部分开发文档仍以 `0.18.0` 为“当前版本”，应视为适用范围或历史记录，不能作为当前版本号来源。`CMakeLists.txt` 才是构建版本事实来源。已有设计文档中明确记录的理由可以保留；源码无法证明且无文档来源的理由必须标为未知。

## 当前未提交工程变更审查

| 变更 | 审计结论 |
|---|---|
| `.github/workflows/ci.yml` | 符合固定 GitHub Actions 入口方向，需本地和远端验证 |
| `tools/ci.ps1` | 覆盖干净配置、构建、测试、打包、元数据、内容、哈希和体积门禁，应保留 |
| `.github/ci/release-baseline.json` | 为 5% 体积门禁提供结构化基线，应保留 |
| `CMakePresets.json` | CI 使用独立构建目录，避免历史缓存，应保留 |
| `CMakeLists.txt` 与 `about_window_test.cpp` | 让测试版本从 `PROJECT_VERSION` 派生，不改变产品行为，应保留 |
| `AGENTS.md` | 按所有者要求只保留原 CI 约束，其他部分由本次审计重建 |

## Unknowns

- 原始产品需求和完整路线图不存在；
- 多数早期架构选择的历史原因无法从当前仓库确定；
- 数字签名策略与未来安装方式未确认；
- 所有支持的 Windows 版本、SKU、权限级别和远程桌面组合尚未形成完整测试矩阵；

## Phase 1 Report

### Completed

完成源码、构建、测试、发布、文档、配置、存储和 Windows 运行时审计。

### Evidence

证据来自 `CMakeLists.txt`、`CMakePresets.json`、`include/simpilot/`、`src/core/`、`src/app/`、`tests/`、现有文档及 2026-09-12 的干净构建记录。

### Findings

项目具有清晰的三目标构建边界和便携存储模型，但复杂度集中于 Win32 UI、键盘线程和 Everything 同步操作。

### Problems

发现历史构建目录污染、运行时风险和端到端覆盖不足；本阶段没有修改业务代码。

### Unknowns

历史设计理由、签名策略和完整平台测试矩阵仍未确认。

### Next

建立可复现的 Debug、Release、测试和打包基线。
