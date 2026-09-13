# Testing Knowledge

## 测试入口

完整测试：

```powershell
ctest --preset release
```

构建、打包或 CI 相关修改使用：

```powershell
.\tools\ci.ps1
```

## 修改到测试的映射

| 修改范围 | 至少执行 |
|---|---|
| 菜单、配置、缓存、日志、Everything 解析、本地化 | `simpilot_core_tests`、相关专项测试、完整 CTest |
| Windows 快捷键屏蔽 | `simpilot_windows_hotkey_blocker_tests`、完整 CTest、人工真实按键验证 |
| 键盘录制状态机 | `simpilot_keyboard_capture_state_tests`、`simpilot_keyboard_thread_lifecycle_test`、完整 CTest、人工真实按键验证 |
| 键盘映射编辑、模型或运行时 | `simpilot_core_tests`、`simpilot_keyboard_mapping_tests`、`simpilot_keyboard_thread_lifecycle_test`、完整 CTest、人工验证录制/下拉回填与真实按键 |
| 菜单、主题、图标 | `simpilot_menu_presentation_tests`、完整 CTest、人工菜单验证 |
| 程序候选窗口 | `simpilot_program_selection_dialog_test`、完整 CTest |
| 关于窗口、版本资源 | `simpilot_about_window_test`、完整 CTest |
| 构建、版本、打包、CI | 完整 `tools/ci.ps1` |

## 重要边界

- 当前没有完整的 `TrayApplication` 端到端测试；
- 自动测试没有通过 `SendInput` 验证真实键盘输入；
- 映射测试使用可注入输入接收器和时钟，不覆盖真实低级钩子、UIPI、权限差异或安全桌面；
- Everything 测试验证 SDK 导出和核心解析，不覆盖真实服务安装、IPC 超时和 UAC；
- UI 测试覆盖部分窗口创建和布局，不能替代多显示器、DPI、菜单交互和托盘生命周期冒烟；
- 不得将未执行的人工验证描述为已通过。

详细测试清单与当前基线见 `docs/testing.md`。
