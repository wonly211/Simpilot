# Win+A～Win+Z 系统热键复核与屏蔽方案

更新日期：2026-07-30  
适用版本：Simpilot 0.5.1 及后续版本

## 1. 结论

Simpilot 不再通过注册表、组策略或 Explorer 的兼容设置禁用 `Win+A`～`Win+Z`。全部字母组合统一使用进程内的 `WH_KEYBOARD_LL` 低级键盘钩子，在 Simpilot 运行期间拦截；保存后立即生效，无需重启 Explorer，退出 Simpilot 后自动恢复。

该决定直接参考 Microsoft PowerToys Keyboard Manager 的录制与快捷键重映射实现。PowerToys 的 Keyboard Manager 也不是通过重启 Explorer 来完成“禁用快捷键”，而是保持一个运行中的低级键盘钩子，把目标快捷键映射到内部禁用值并吞掉对应键盘事件。

## 2. PowerToys 源码对照

本次复核使用 PowerToys 提交：

```text
d72fa2ea6ea6b6f02af0a1aaeb7b85db975016d8
```

主要参考文件：

- `src/common/interop/KeyboardHook.cpp`
- `src/settings-ui/Settings.UI.Library/HotkeySettingsControlHook.cs`
- `src/modules/keyboardmanager/KeyboardManagerEditorUI/Helpers/KeyboardHookHelper.cs`
- `src/modules/keyboardmanager/KeyboardManagerEditorLibrary/KeyboardManagerState.cpp`
- `src/modules/keyboardmanager/KeyboardManagerEngineLibrary/KeyboardEventHandlers.cpp`
- `src/modules/keyboardmanager/common/Helpers.cpp`
- `src/modules/keyboardmanager/common/KeyboardManagerConstants.h`

### 2.1 录制快捷键

PowerToys 在进入录制状态时安装 `WH_KEYBOARD_LL`，录制期间同时处理并抑制以下四类消息：

- `WM_KEYDOWN`
- `WM_KEYUP`
- `WM_SYSKEYDOWN`
- `WM_SYSKEYUP`

`KeyboardManagerState::DetectShortcutUIBackend` 根据每次按下和释放维护当前按键集合，然后返回 `Suppress`。公共 `KeyboardHook::HookProc` 收到该决定后返回非零值，事件不会继续到达反馈中心、Game Bar、Explorer 或前台应用。因此 `Win+F`、`Win+G` 等组合在第一次按下时即可被录制，而不会先执行系统动作。

Simpilot 0.5.1 在同一个 `Simpilot.exe` 中设置专用键盘线程。该线程建立自己的消息循环并在程序生命周期内只安装一个钩子；用户点击热键框时只切换钩子的录制优先状态。录制期间四类消息全部吞掉，状态完全由钩子收到的事件维护，不在回调中调用 `GetAsyncKeyState`。按键全部释放后再把录制结果投递给界面线程。源码复用范围和最小适配边界见 [PowerToys Keyboard Manager 录制架构复用说明](powertoys-keyboard-recorder.md)。

### 2.2 禁用快捷键

PowerToys 使用内部值 `VK_DISABLED = 0x100` 表示“禁用”目标。快捷键匹配后，Keyboard Manager 的引擎会：

1. 标记该快捷键已经触发；
2. 对目标动作键的按下、自动重复和释放返回非零值；
3. 必要时抑制原修饰键的释放或恢复键盘逻辑状态；
4. 注入虚拟键 `0xFF` 的按下和释放事件，防止单独释放 Win 键后误开开始菜单；
5. 保持钩子常驻，因此配置生效不依赖 Explorer，也不需要注销或重启系统。

完整 PowerToys 引擎还处理左右修饰键、额外按键、按键释放顺序、快捷键转快捷键和应用范围映射。Simpilot 当前只处理固定的 `Win+字母` 禁用场景，因此移植其中与 `Shortcut -> VK_DISABLED` 直接相关的状态流程：事件驱动的左右 Win 状态、目标键按下/重复/释放全程吞掉、首次命中时注入 `0xFF` dummy key、注入原 Win 修饰键 KeyUp、吞掉对应的物理 Win KeyUp，以及在后续未禁用按键到来前恢复仍被物理按住的 Win 状态。

## 3. Simpilot 运行时设计

### 3.1 配置

系统热键页的选择保存在：

```ini
[WindowsHotkeys]
DisabledWindowsHotkeys=AFG
```

值只包含大写字母，读取时兼容小写、重复字母和任意顺序。`L` 会被忽略，Simpilot 不提供 `Win+L` 屏蔽。没有该配置时默认不屏蔽任何系统热键。

Simpilot 不读取或写入以下设置：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced\DisabledHotkeys
HKCU\Software\Microsoft\GameBar\UseNexusForGameBarEnabled
HKCU\Software\Microsoft\Windows\CurrentVersion\Policies\System\DisableLockWorkstation
```

这可以避免覆盖用户已有设置，也避免把“禁用一个快捷键”扩大为“关闭 Game Bar”或“关闭系统锁定功能”。

### 3.2 生命周期

1. Simpilot 启动并读取 `Config\Setting.ini`。
2. `Simpilot.exe` 的专用键盘线程建立独立消息循环并安装唯一的常驻 `WH_KEYBOARD_LL`。有效屏蔽集合由本页选择、确认强制覆盖的固定热键和自定义 `Win+字母` 热键合并生成。
3. 设置页打开后仍使用同一钩子。录制开始时，钩子先执行 PowerToys 来源的录制决策并吞掉全部输入，不再继续执行运行时屏蔽或全局热键分支。设置窗口与添加窗口自身不安装钩子。
4. 用户保存后，新字母掩码立即写入常驻钩子；不修改 Windows 设置，不显示 Explorer 重启对话框。
5. 用户取消时，原有掩码保持不变。
6. Simpilot 退出时调用 `UnhookWindowsHookEx` 并结束钩子线程，所有运行时屏蔽自动解除。

### 3.3 匹配规则

- 左 Win 和右 Win 都可触发屏蔽。
- 只有修饰键严格为 Win 且已选择的 `A`～`Z` 会被吞掉；`Shift+Win+字母`、`Ctrl+Win+字母` 和 `Alt+Win+字母` 必须传递给 Windows 的全局热键机制。
- 首次动作键按下、长按产生的重复 KeyDown 和对应 KeyUp 都被吞掉。
- 未选择的相邻字母继续正常传递。
- 钩子回调不访问注册表、不写日志、不等待锁，也不执行界面操作。
- `0xFF` dummy key 只用于标记 Win 已参与组合，避免释放 Win 时打开开始菜单。

## 4. Win+A～Win+Z 功能复核

下表描述常见 Windows 11 行为；具体功能可能随 Windows 版本、硬件和已安装组件变化。Simpilot 的屏蔽方法不依赖功能宿主，因此 A～Z 均使用同一运行时钩子。

| 组合 | 常见系统行为 | Simpilot 方案 |
|---|---|---|
| Win+A | 快速设置 | 运行时钩子 |
| Win+B | 聚焦通知区域 | 运行时钩子 |
| Win+C | Copilot 或搜索 | 运行时钩子 |
| Win+D | 显示或隐藏桌面 | 运行时钩子 |
| Win+E | 文件资源管理器 | 运行时钩子 |
| Win+F | 反馈中心 | 运行时钩子 |
| Win+G | Xbox Game Bar | 运行时钩子，不关闭 Game Bar |
| Win+H | 语音输入 | 运行时钩子 |
| Win+I | Windows 设置 | 运行时钩子 |
| Win+J | Recall（受支持设备） | 运行时钩子 |
| Win+K | 投放 | 运行时钩子 |
| Win+M | 最小化所有窗口 | 运行时钩子 |
| Win+N | 通知中心和日历 | 运行时钩子 |
| Win+O | 锁定屏幕方向 | 运行时钩子 |
| Win+P | 投影模式 | 运行时钩子 |
| Win+Q | 搜索（兼容行为） | 运行时钩子 |
| Win+R | 运行 | 运行时钩子 |
| Win+S | 搜索 | 运行时钩子 |
| Win+T | 切换任务栏应用 | 运行时钩子 |
| Win+U | 辅助功能设置 | 运行时钩子 |
| Win+V | 剪贴板历史记录 | 运行时钩子 |
| Win+W | 小组件 | 运行时钩子 |
| Win+X | 快捷链接菜单 | 运行时钩子 |
| Win+Y | 混合现实输入切换 | 运行时钩子 |
| Win+Z | 贴靠布局 | 运行时钩子 |

## 5. 已否决的方案

### 5.1 Explorer DisabledHotkeys

`DisabledHotkeys` 只对部分由 Explorer/Shell 管理的组合有效。实测 `Win+Q`、`Win+V` 可生效，而 `Win+A`、`Win+F` 可能不生效；修改后还可能需要重启 Explorer。它不是微软承诺覆盖 A～Z 的统一快捷键禁用接口，因此不再用于 Simpilot。

### 5.2 Win+G 和 Win+L 专用注册表

Game Bar 快捷键设置和 `DisableLockWorkstation` 改变的是系统功能配置，影响范围超过单一键盘组合。它们还会在 Simpilot 退出后继续生效，不符合“运行期间临时屏蔽”的预期，因此不再使用。

### 5.3 Windows Keyboard Filter

Keyboard Filter 适用于特定 Enterprise、Education 和 IoT 场景，需要系统功能、管理员权限并通常需要重启。它不是普通 Home/Pro 桌面应用的合适依赖。对于当前需求，PowerToys 已证明用户态低级键盘钩子足够。

## 6. 安全边界

- 普通桌面钩子不能拦截安全注意序列，例如 `Ctrl+Alt+Del`。
- 钩子只覆盖 Simpilot 所在的当前交互式桌面和权限边界；登录桌面、安全桌面、其他用户会话和部分远程桌面场景不在保证范围内。
- 如果 Windows 因 `LowLevelHooksTimeout` 移除超时钩子，系统不会通知应用。因此回调必须保持极短；后续版本可增加独立健康检查和自动重装机制。
- 低完整性进程不能控制更高完整性安全桌面。Simpilot 不通过提权扩大键盘拦截范围。

## 7. 验证清单

自动测试负责验证配置文件往返保存；系统行为需要在隔离测试桌面人工验证：

1. 录制 `Win+F`、`Win+G`、`Win+L`，确认第一次按下即显示在输入框，系统功能不启动。
2. 分别打开 F、G 对应的屏蔽开关并保存，确认无需重启 Explorer 且第一次按下即被屏蔽；页面不提供 Win+L。
3. 长按动作键，确认自动重复不会穿透。
4. 分别使用左 Win、右 Win，确认行为一致。
5. 分别按“先松字母、后松 Win”和“先松 Win、后松字母”，确认没有残留按下状态，也不弹出开始菜单。
6. 在已屏蔽组合后继续按未屏蔽字母，确认未屏蔽组合正常工作。
7. 关闭开关并保存，确认立即恢复；退出 Simpilot 后再次确认全部恢复。
8. 确认保存过程不写上述三个注册表位置，且不会出现 Explorer 重启提示。
9. 确认诊断日志只写入 `Log\Simpilot.log`。

建议覆盖 Windows 11 当前稳定版、Windows 10 22H2，以及 Home/Pro 的标准用户和管理员启动场景。

## 8. 资料

- [PowerToys Keyboard Manager 源码](https://github.com/microsoft/PowerToys/tree/main/src/modules/keyboardmanager)
- [Windows 键盘快捷键总表](https://support.microsoft.com/en-us/windows/keyboard-shortcuts-in-windows-dcc61a57-8ff0-cffe-9796-cb9706c75eec)
- [LowLevelKeyboardProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)
- [SetWindowsHookExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw)
- [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
- [Windows Keyboard Filter](https://learn.microsoft.com/en-us/windows/configuration/keyboard-filter/)
