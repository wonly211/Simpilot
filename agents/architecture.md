# Architecture Knowledge

## 用途

本文件帮助 Agent 快速判断改动应落在哪个模块。完整架构与调用链见 `docs/architecture.md` 和 `docs/project-map.md`。

## 依赖边界

```text
simpilot (Win32 application)
  -> simpilot_keyboard
      -> simpilot_core

simpilot -> simpilot_core
```

- `include/simpilot/` 与 `src/core/` 是可脱离托盘 UI 测试的核心能力；
- `src/app/keyboard_manager.*`、`keyboard_capture_state.*`、`keyboard_mapping_editor_model.*` 与 `keyboard_mapping_engine.*` 组成键盘层；
- `src/app/` 其余文件是 Win32 应用与 UI；
- 第三方实现固定在 `third_party/`，不要把应用策略下沉到第三方目录。

## 定位规则

- 菜单格式、命令解析、变量展开：`menu_model`、`menu_parser`、`menu_writer`、`command`、`variable_expander`；
- 程序查找与缓存：`program_resolver`、`program_cache`、`everything`；
- 设置与持久化：`app_settings`、`atomic_file`、`settings_window`；
- 托盘、启动、运行时协调：`main.cpp`、`tray_application.*`；
- 菜单呈现与图标：`launch_menu_renderer`、`menu_icon_cache`、`menu_theme`；
- 热键、录制、物理映射与屏蔽：`keyboard_manager`、`keyboard_capture_state`、`keyboard_mapping_editor_model`、`keyboard_mapping_engine`、`keyboard_mapping_dialog`、`keyboard_mapping`、`hotkey`；
- 本地化：`localization`、`Languages/`、`language_pack_builder`。

## 修改约束

- 保持核心逻辑可在不启动完整托盘应用的情况下测试；
- 映射模型校验和配置往返留在 `simpilot_core`；映射运行时状态只由键盘线程拥有，UI 通过 `KeyboardManager` 的同步更新接口替换规则；
- 修改映射规则时保持物理身份 `vkCode + scanCode + extended`，不要退回仅按逻辑虚拟键匹配；
- 不让配置监听线程或键盘线程直接拥有或修改 UI 对象；
- 不在 `TrayApplication` 保存指向临时菜单模型的长生命周期引用；
- 新的跨模块依赖必须在 `docs/architecture.md` 记录方向和理由；
- 历史设计理由若无提交、代码或文档证据，应标记为未知。
