# Testing

## Test Framework

项目使用 CTest 注册 9 个独立 C++ 测试可执行程序，没有额外的单元测试框架。测试通过抛出异常或非零退出码报告失败。

完整 Release 测试：

```powershell
cmake --build --preset release
ctest --preset release
```

查看失败输出：

```powershell
ctest --test-dir build/vs2022-x64 -C Release --output-on-failure
```

## Registered Tests

| CTest 名称 | 主要覆盖 | 最近验证 |
|---|---|---:|
| `simpilot_core_tests` | 菜单、命令、变量、配置、缓存、日志、监听、Everything 候选、设置、热键模型及映射配置往返 | PASS |
| `simpilot_localization_resource_tests` | 内置/外部语言包、损坏及缺失资源 | PASS，0.02s |
| `simpilot_windows_hotkey_blocker_tests` | Win+字母屏蔽纯状态机 | PASS，0.01s |
| `simpilot_keyboard_thread_lifecycle_test` | 键盘线程和钩子生命周期重复启动/停止 | PASS，0.05s |
| `simpilot_keyboard_capture_state_tests` | legacy 热键和物理映射录制状态机、消息变体、释放顺序、Esc/Backspace、单独物理修饰键源、重置 | PASS |
| `simpilot_keyboard_mapping_tests` | 编辑草稿、F1-F24/扩展键目录和键名、严格物理键匹配、单键/快捷键/chord、右 Ctrl 消歧、自动重复、250 ms 回放、安全组合旁路、进程范围优先级、注入标记、失败清理和运行时替换释放 | PASS |
| `simpilot_menu_presentation_tests` | 菜单 owner-draw、主题、定位、图标缓存和人工图标 | PASS，0.10s |
| `simpilot_program_selection_dialog_test` | 程序候选窗口创建与基本交互 | PASS，0.11s |
| `simpilot_about_window_test` | 关于窗口布局、链接、版本资源和本地化 | PASS，0.13s |

2026-09-13 的 `v0.18.7` 候选 Release 总计：9/9 通过，0 失败，0 跳过，1.19 秒；
非同步全新 Debug 总计：9/9 通过，0 失败，0 跳过，1.17 秒。同步目录 Debug 的 PDB `C1090 / error 3`
属于已记录环境问题。

同日的右 Ctrl 单独源键修复在新的非同步 Debug 目录再次获得 9/9 通过（1.15 秒），
固定 Release CI 获得 9/9 通过（1.16 秒），并通过版本、ZIP、SHA-256 和体积门禁。

## What To Run

### 修改 Core

至少运行 `simpilot_core_tests` 和完整 CTest。涉及语言资源时增加 `simpilot_localization_resource_tests`；涉及 Everything 候选窗口时增加对应对话框测试。

### 修改 UI

根据改动运行菜单呈现、程序选择或关于窗口测试，并运行完整 CTest。随后执行人工冒烟，确认窗口可打开、键盘导航、DPI 布局和退出行为。

### 修改 Windows 或键盘层

运行：

- `simpilot_windows_hotkey_blocker_tests`；
- `simpilot_keyboard_capture_state_tests`；
- `simpilot_keyboard_mapping_tests`；
- `simpilot_keyboard_thread_lifecycle_test`；
- 完整 CTest；
- 隔离桌面中的真实键盘人工验证。

人工验证至少覆盖 legacy 录制与取消、映射单键/快捷键/同时按住的单级 chord、左右修饰键、
单独右 Ctrl 到 `左 Win + 左 Shift + F23`、右 Ctrl 快速接普通动作键的消歧、录制和下拉框
回填、F1-F24、添加/编辑/应用后的配置往返、应用范围、重复输入、强制
Win+字母热键、未屏蔽相邻按键和退出后系统快捷键恢复。

### 修改配置或存储

运行 `simpilot_core_tests` 和完整 CTest，检查 UTF-8、缺失/损坏输入、原子替换、缓存失效和设置往返。如果修改设置提交过程，还需人工测试保存失败及回滚可见行为。

修改 `[KeyboardMappings]` 或映射运行时替换时，还要检查物理键字段往返、重复/歧义/循环拒绝、
无效规则隔离及诊断、进程精确/宽松匹配和更新失败后的旧运行时恢复。

### 修改构建、版本、打包或 CI

完整运行：

```powershell
.\tools\ci.ps1
```

不能只运行某个测试来替代配置、版本、ZIP 和体积检查。

### 发布前

运行固定 CI、Debug/Release 适用测试，并从解压后的 ZIP 完成人工冒烟。发布动作和 GitHub Release 远端回读不属于当前 CI 自动化。

## Known Test Gaps

- 没有完整 `TrayApplication` 和托盘生命周期端到端测试；
- 没有第二实例通知首实例的测试；
- 没有 `SendInput` 或物理键盘驱动的真实钩子测试；映射单键、快捷键和 chord 仍需人工验证；
- 没有钩子静默移除、键盘线程挂死和控制消息超时注入；
- 没有真实 Everything 默认实例 IPC、服务修复、UAC 取消和超时测试；
- 没有启动项注册、外部进程窗口恢复和不同权限进程的完整测试；
- UI 自动测试没有形成 Windows 10/11、多显示器、多 DPI、深浅主题的矩阵；
- 当前没有 clang-tidy、独立 MSVC 静态分析或格式检查任务。

这些缺口必须在相关改动报告中作为剩余风险说明，不能描述为已有覆盖。
