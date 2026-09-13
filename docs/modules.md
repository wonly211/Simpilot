# Modules

## Core Configuration And Persistence

**Purpose**：读取和保存设置、菜单、缓存及日志，提供原子文件替换。
**Key Files**：`src/core/app_settings.cpp`、`config_file.cpp`、`atomic_file.cpp`、`logger.cpp`。
**Key Types**：`AppSettings`、`AppSettingsStore`、`AtomicFileReplacement`、`Logger`；配置文本由 `read_configuration_text` / `write_configuration_text` 自由函数处理。
**Dependencies**：文件系统、注册表、UTF-8 编解码。
**Used By**：`TrayApplication`、`SettingsWindow`、菜单解析与测试。
**Important Behaviors**：无效设置回退默认值；保存使用同目录临时文件；日志保留 90 天。
**Tests**：`simpilot_core_tests`。
**Known Issues**：部分 `noexcept` 路径只返回默认值或布尔值，诊断粒度有限。

## Menu Model, Parser And Writer

**Purpose**：表示、解析、校验和序列化两份快捷菜单。
**Key Files**：`include/simpilot/menu_model.hpp`、`src/core/menu_parser.cpp`、`menu_writer.cpp`。
**Key Types**：`MenuDocument`、`MenuElement`、`MenuCategory`、`MenuEntry`、`MenuSeparator`。
**Dependencies**：配置读取、命令解析、UTF-8。
**Used By**：`TrayApplication`、`SettingsWindow`、`MenuEditorWindow`。
**Important Behaviors**：树形模型；保存前整体验证；解析失败不替换当前运行菜单。
**Tests**：`simpilot_core_tests`。
**Known Issues**：完整编辑和运行流程没有端到端测试。

## Command And Program Resolution

**Purpose**：解析命令、展开变量并定位未提供完整路径的程序。
**Key Files**：`src/core/command.cpp`、`variable_expander.cpp`、`program_resolver.cpp`、`program_cache.cpp`。
**Key Types**：`ParsedCommand`、`VariableExpander`、`ProgramResolver`、`ProgramResolutionCache`、`IProgramSearch`。
**Dependencies**：Windows 搜索目录、`PATH`、文件系统、Everything 可选接口。
**Used By**：菜单重载和程序候选对话框。
**Important Behaviors**：先常规路径，再缓存和 Everything；多候选由回调选择；失效缓存自动删除。
**Tests**：`simpilot_core_tests`、`simpilot_program_selection_dialog_test`。
**Known Issues**：路径字符串比较对短路径、符号链接和权限边界不完全稳健。

## Everything Integration

**Purpose**：查询默认 Everything 实例、启动客户端、检查或修复服务。
**Key Files**：`include/simpilot/everything.hpp`、`src/core/everything.cpp`。
**Key Types**：`EverythingSearch`、`EverythingManager`。
**Dependencies**：`Everything64.dll`、SCM、进程和窗口 API、`ShellExecuteExW`。
**Used By**：程序解析、托盘维护命令和 Everything 功能热键。
**Important Behaviors**：组件缺失时降级；只保证默认实例；服务安装可能请求 UAC。
**Tests**：核心测试验证 DLL 导出及候选排序。
**Known Issues**：查询和服务维护可能阻塞 UI；真实 IPC、服务和 UAC 未自动覆盖。

## Localization

**Purpose**：加载内置三语言资源和可选 `Language.lng`。
**Key Files**：`src/core/localization.cpp`、`tools/language_pack_builder.cpp`、`Languages/*.json`。
**Key Types**：`Localization`、`UiLanguage`。
**Dependencies**：XPRESS Huffman 压缩、JSON、Windows 资源。
**Used By**：所有用户界面。
**Important Behaviors**：内置语言打包进 RC；外部包损坏时不阻止启动；缺失键显示占位文本。
**Tests**：`simpilot_localization_resource_tests`、`simpilot_core_tests`。
**Known Issues**：外部翻译的视觉适配仍需人工检查。

## Keyboard And Hotkeys

**Purpose**：注册功能及自定义热键、录制按键、执行物理键盘映射并屏蔽受支持的 Win+字母。
**Key Files**：`src/app/keyboard_manager.cpp`、`src/app/keyboard_capture_state.*`、`src/app/keyboard_mapping_editor_model.*`、`src/app/keyboard_mapping_engine.*`、`src/core/keyboard_mapping.cpp`、`src/core/hotkey.cpp`。
**Key Types**：`KeyboardManager`、`KeyboardCaptureState`、`KeyboardMappingEditorModel`、`KeyboardMappingEngine`、`HotKeyGesture`、`HotKeyBinding`、`PhysicalKey`、`KeyboardTrigger`、`KeyboardOutput`、`KeyboardMappingRule`。
**Dependencies**：`RegisterHotKey`、`WH_KEYBOARD_LL`、`SendInput`、线程消息。
**Used By**：`TrayApplication`、设置及自定义热键窗口、键盘映射编辑对话框。
**Important Behaviors**：单一常驻低级钩子；录制优先于映射和运行时动作；编辑模型让录制和下拉选择生成同一结构化规则；映射按 `vkCode + scanCode + extended` 区分物理键；快捷键前缀最多缓冲 16 个事件并由键盘线程定时器处理 250 ms 超时；目标注入和超时回放带独立标记；Win+L 及安全组合不可映射。
**Tests**：`simpilot_keyboard_capture_state_tests`、`simpilot_keyboard_mapping_tests`、`simpilot_windows_hotkey_blocker_tests`、`simpilot_keyboard_thread_lifecycle_test`、核心设置测试。
**Known Issues**：健康检查不能检测静默钩子丢失；挂死线程停止可能无限等待；UIPI 或权限可能导致 `SendInput` 回放失败；真实输入仍需人工验证。

## Tray Application

**Purpose**：组合所有服务并拥有主消息循环和长期运行状态。
**Key Files**：`src/app/main.cpp`、`tray_application.hpp`、`tray_application.cpp`。
**Key Types**：`TrayApplication`。
**Dependencies**：全部核心、键盘和 UI 组件以及 Windows Shell。
**Used By**：`wWinMain`。
**Important Behaviors**：单实例、托盘恢复、菜单重载延迟、热键分发、Everything 初始化和退出清理。
**Tests**：由下层及部分 UI 测试间接覆盖。
**Known Issues**：第二实例窗口查找失配；没有完整端到端测试；协调器体积较大。

## Launch Menu, Theme And Icons

**Purpose**：把菜单模型呈现为原生菜单，管理主题、DPI、图标提取和缓存。
**Key Files**：`launch_menu_renderer.cpp`、`menu_theme.cpp`、`menu_icon_cache.cpp`。
**Key Types**：`LaunchMenuRenderer`、`MenuThemeController`、`MenuIconCache`。
**Dependencies**：HMENU、GDI、Shell 图像列表、uxtheme。
**Used By**：`TrayApplication` 和菜单编辑设置。
**Important Behaviors**：owner-draw、128x128 ICO 磁盘缓存、人工图标覆盖、多显示器定位。
**Tests**：`simpilot_menu_presentation_tests`。
**Known Issues**：内存图标命中后不会总是重新核对源文件；暗色菜单依赖未公开接口。

## Settings And Dialogs

**Purpose**：编辑常规设置、功能热键、自定义热键、键盘映射、菜单和人工图标。
**Key Files**：`settings_window.cpp`、`menu_editor_window.cpp`、`custom_hotkey_dialog.cpp`、`keyboard_mapping_dialog.cpp`、`program_selection_dialog.cpp`、`about_window.cpp`。
**Key Types**：`SettingsWindow`、`MenuEditorWindow`、`CustomHotKeyDialog`、`KeyboardMappingDialog`、`ProgramSelectionDialog`、`AboutWindow`。
**Dependencies**：核心模型、键盘管理器、Common Controls、Shell 和 GDI。
**Used By**：`TrayApplication`。
**Important Behaviors**：模态嵌套消息循环、设置草稿、键盘映射录制与结构化下拉回填、跨资源保存和回滚、即时语言切换。
**Tests**：程序选择、关于窗口、菜单呈现、键盘映射状态/引擎及核心设置测试。
**Known Issues**：大型手写 UI 文件、复杂补偿事务、多 DPI 和完整交互覆盖不足。

## Build, Packaging And CI

**Purpose**：生成程序、测试、语言资源和固定发布 ZIP。
**Key Files**：`CMakeLists.txt`、`CMakePresets.json`、`tools/ci.ps1`、`.github/workflows/ci.yml`。
**Key Types**：CMake targets/presets、PowerShell CI functions。
**Dependencies**：VS 2022、Windows SDK、CMake、CPack、GitHub Actions。
**Used By**：开发、Pull Request 和主分支验证。
**Important Behaviors**：CI 使用全新专用目录，检查 Release flags、测试、包内容、版本、哈希与体积。
**Tests**：本地 `tools/ci.ps1` 和 GitHub Actions。
**Known Issues**：没有独立 clang-format、clang-tidy 或静态分析器配置；主分支规则需要远端权限设置。
