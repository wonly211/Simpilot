# Project Map

## Repository Map

```text
Simpilot
├─ include/simpilot/       核心接口和数据模型
├─ src/
│  ├─ core/                配置、菜单、解析、缓存、Everything、本地化、日志
│  └─ app/                 托盘应用、键盘层和原生 Win32 UI
├─ tests/                  CTest 测试程序
├─ Languages/              内置语言 JSON
├─ resources/              RC、图标、manifest 和版本资源模板
├─ third_party/            Everything、nlohmann/json
├─ tools/                  语言包、Wiki 发布和 CI 工具
├─ docs/                   用户、工程和历史设计文档
├─ agents/                 面向 Agent 的修改与验证知识
└─ .github/                固定 CI workflow 与发布基线
```

## Build Target Map

```text
simpilot_core (static library)
  ├─ configuration and persistence
  ├─ menu model/parser/writer
  ├─ command and variable expansion
  ├─ program resolution and Everything
  └─ localization, cache, logging, watcher

simpilot_keyboard (static library)
  ├─ KeyboardManager
  ├─ KeyboardCaptureState
  ├─ KeyboardMappingEditorModel and physical-key catalog
  ├─ KeyboardMappingEngine
  └─ depends on simpilot_core

simpilot (WIN32 executable)
  ├─ tray host and message loop
  ├─ launch menu and icon/theme support
  ├─ settings and modal dialogs
  └─ depends on simpilot_keyboard and simpilot_core
```

## Dependency Map

```text
Win32 UI / TrayApplication
        |             \
        v              v
KeyboardManager ----> simpilot_core
        |
        ├─ KeyboardCaptureState
        ├─ KeyboardMappingEditorModel
        └─ KeyboardMappingEngine

simpilot_core ---> Windows APIs / Everything SDK / nlohmann-json
```

应用层直接使用核心层类型，也直接协调 Windows Shell、通知区、SCM、注册表和进程窗口。项目不是严格的端口适配器架构；Windows 平台集成同时存在于 core 和 app，但构建依赖没有循环。

## Runtime Flow

```text
wWinMain
-> DPI setup and single-instance mutex
-> TrayApplication construction
-> hidden tray host window
-> notification icon
-> keyboard thread and health timer
-> startup registration synchronization
-> Everything SDK and client/service startup
-> primary and optional secondary menu reload
-> global hotkey registration
-> configuration watcher
-> UI message loop
```

## Critical Flows

### 显示快捷菜单

```text
tray click / built-in hotkey / second-instance message
-> TrayApplication::show_launch_menu
-> LaunchMenuRenderer::append
-> MenuIconCache::icon_for
-> TrackPopupMenu
-> command_entries_
-> TrayApplication::execute
-> ShellExecuteW
```

### 解析无完整路径程序

```text
MenuParser
-> ProgramResolver::resolve
-> Windows directories
-> PATH
-> ProgramResolutionCache
-> EverythingSearch::find_exact_file_name
-> ProgramCandidateSelector
-> resolved_value / is_available
```

### 保存设置和菜单

```text
SettingsWindow
-> draft AppSettings and MenuDocument
-> validation
-> MenuWriter / AppSettingsStore
-> AtomicFileReplacement
-> prebuild and replace keyboard mappings
-> update runtime hotkeys, icons and language
-> compensating rollback on failure
```

### 键盘输入

```text
WH_KEYBOARD_LL
-> KeyboardManager hook callback
-> KeyboardCaptureState (recording)
-> KeyboardMappingEngine (prefix, match, replay or target injection)
-> forced registration / Win-letter blocker
-> PostMessage to UI for completed capture or hotkey
-> TrayApplication command handling
```

映射使用 `vkCode + scanCode + extended` 区分物理按键。快捷键前缀只在命中候选规则
时进入最多 16 个事件的缓冲；250 ms 定时器在键盘线程中处理超时。目标注入使用
独立标记，回放使用另一标记，二者都不会递归进入映射引擎。

### 配置文件变化

```text
ConfigWatcher thread
-> ReadDirectoryChangesW
-> state and content-hash comparison
-> PostMessage to tray host
-> reload_menu on UI thread
-> retain previous document on parse failure
```

## Navigation By Task

| 任务 | 首先查看 |
|---|---|
| 菜单语法或序列化 | `include/simpilot/menu_model.hpp`、`src/core/menu_parser.cpp`、`menu_writer.cpp` |
| 程序定位 | `src/core/program_resolver.cpp`、`everything.cpp`、`program_cache.cpp` |
| 设置持久化 | `src/core/app_settings.cpp`、`src/app/settings_window.cpp` |
| 托盘或启动 | `src/app/main.cpp`、`tray_application.cpp` |
| 热键、录制与键盘映射 | `src/app/keyboard_manager.cpp`、`src/app/keyboard_capture_state.*`、`src/app/keyboard_mapping_engine.*`、`src/core/hotkey.cpp` |
| 菜单图标或主题 | `src/app/menu_icon_cache.cpp`、`launch_menu_renderer.cpp`、`menu_theme.cpp` |
| 本地化 | `src/core/localization.cpp`、`Languages/`、`tools/language_pack_builder.cpp` |
| 构建、测试、打包 | `CMakeLists.txt`、`CMakePresets.json`、`tools/ci.ps1` |
