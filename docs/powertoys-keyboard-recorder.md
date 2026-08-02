# PowerToys Keyboard Manager 录制逻辑复用说明

更新日期：2026-07-30  
适用版本：Simpilot 0.5.1

## 1. 最终架构

Simpilot 0.5.1 只发布并运行一个主程序 `Simpilot.exe`。程序内部创建一个专用键盘线程，该线程拥有独立的 Windows 消息循环，并在整个程序生命周期内安装唯一的 `WH_KEYBOARD_LL` 低级键盘钩子。

```text
Simpilot.exe
├─ 主界面线程
│  ├─ 托盘与菜单
│  ├─ 设置窗口
│  └─ 接收录制结果
└─ 专用键盘线程
   ├─ 独立消息循环
   ├─ 唯一 WH_KEYBOARD_LL
   ├─ 录制状态：PowerToys 来源逻辑，优先 Suppress
   └─ 正常状态：Windows 热键屏蔽与全局热键处理
```

设置窗口和添加全局热键窗口不安装钩子，也不维护修饰键、待释放按键或低级键盘事件。它们通过同步线程消息要求键盘线程开始或停止录制，通过界面线程的隐藏消息窗口接收最终结果。

## 2. 为什么不再使用独立 Recorder 进程

PowerToys 将 Runtime 与 Editor 分成不同进程，这是其产品架构选择，不是 `WH_KEYBOARD_LL` 的系统要求。Simpilot 的设置窗口规模较小，可以在单个进程内用专用线程获得所需的消息循环隔离，无需增加第二个可执行文件、进程间协议和子进程生命周期管理。

0.5.1 因此删除了：

- `Simpilot.KeyboardRecorder.exe`；
- `KeyboardRecorderClient`；
- 跨进程窗口消息协议；
- Recorder 启动、父进程监控和双 EXE 打包逻辑。

这次合并没有把钩子放回界面线程。钩子仍由专用键盘线程独占，因此设置窗口、托盘菜单、日志和 Everything 操作不会在钩子线程中执行。

## 3. PowerToys 来源

本次复用以项目保存的 PowerToys 参考源码为依据，参考提交：

```text
d72fa2ea6ea6b6f02af0a1aaeb7b85db975016d8
```

主要来源文件：

```text
src/common/hooks/LowlevelKeyboardEvent.h
src/modules/keyboardmanager/common/ModifierKey.h
src/modules/keyboardmanager/common/Shortcut.h
src/modules/keyboardmanager/common/Shortcut.cpp
src/modules/keyboardmanager/KeyboardManagerEditor/KeyboardManagerEditor.cpp
src/modules/keyboardmanager/KeyboardManagerEditorLibrary/KeyboardManagerState.h
src/modules/keyboardmanager/KeyboardManagerEditorLibrary/KeyboardManagerState.cpp
LICENSE
```

## 4. 直接复用与适配边界

以下代码可脱离 PowerToys 其余基础设施独立编译，因此直接复用：

- `LowlevelKeyboardEvent.h`：原文件；
- `ModifierKey.h`：原文件；
- `KeyboardHookDecision.h`：从 `KeyboardManagerState.h` 原样提取枚举；
- MIT 许可全文：`third_party/PowerToys/LICENSE.txt`。

以下代码移除了 Simpilot 不需要且无法独立编译的 PowerToys 依赖：

- `Shortcut.h/.cpp`：保留 `ModifierKey`、`actionKey`、`SetKey`、`ResetKey`、`IsModifier` 和 `Reset`；移除 WinUI、WinRT 字符串、和弦、布局映射及重映射执行成员；
- `KeyboardManagerState.h/.cpp`：保留 `SelectDetectedShortcut`、`ResetDetectedShortcutKey` 以及 `DetectShortcutUIBackend` 的 KeyDown、KeyUp 和 `Suppress` 决策；移除 XAML 界面对象、`LayoutMap`、`KeyDelay` 和 PowerToys 日志；
- `src/app/keyboard_manager.cpp`：录制分支采用 PowerToys Editor 的 HookProc 决策结构，即先包装 `LowlevelKeyboardEvent`，调用 `DetectShortcutUIBackend`，收到 `Suppress` 后立即返回 `1`。

PowerToys 完整的 `KeyboardManagerState.cpp` 不能原样加入 Simpilot 构建，因为它依赖 Windows App SDK、WinUI/XAML 和 PowerToys 内部基础设施。项目没有把自行设计的替代实现描述为“PowerToys 原文件原样编译”，直接复用和最小适配的边界在本节明确区分。

## 5. 单钩子状态优先级

键盘钩子的处理顺序固定为：

1. PowerToys 来源的录制状态；
2. 强制接管的自定义全局热键；
3. 用户选择屏蔽的 `Win+字母` 系统热键；
4. `CallNextHookEx`。

录制状态启用后，第一分支对 `WM_KEYDOWN`、`WM_KEYUP`、`WM_SYSKEYDOWN`、`WM_SYSKEYUP` 返回 `Suppress`，后续运行时分支不会执行。组合键全部释放后，键盘线程把修饰键和虚拟键投递给界面线程，并退出录制状态。

这种顺序保证已保存的 `Win+G` 屏蔽规则、全局热键注册和设置页录制不会互相竞争。同一时刻只有一个钩子回调入口和一个录制状态源。

## 6. 线程生命周期

1. 托盘隐藏窗口创建完成后，`KeyboardManager::start` 创建专用键盘线程；
2. 键盘线程创建消息窗口，安装低级钩子并通知主线程启动完成；
3. 配置更新、强制热键注册和录制开始/停止通过 `SendMessageTimeoutW` 同步提交给键盘线程；
4. 钩子回调只更新内存状态、投递 Windows 消息或返回抑制决定；
5. `Simpilot.exe` 退出时，键盘线程恢复必要的 Win 键逻辑状态，调用 `UnhookWindowsHookEx` 并结束消息循环。

## 7. 测试边界

自动测试不使用 `SendInput` 模拟录制：

- 核心配置与解析测试；
- Windows 热键屏蔽纯状态测试；
- 专用键盘线程启动、唯一钩子安装、状态更新和正常退出测试。

这些测试不能代替物理键盘验收。发布前仍必须在简驭功能热键和添加全局热键窗口的录制按钮中分别验证 `Win+G`、`Win+F`、`Win+R`、`Esc` 和 `Backspace`。`Tab` 聚焦按钮不能开始录制；点击按钮或使用键盘激活后才进入录制。`Esc` 应取消录制，`Backspace` 应作为普通按键被记录，不再清除已有热键；清除功能热键只使用录制按钮右侧的“清除”按钮。在真实键盘验收前，只能报告“单进程、单线程钩子架构和非模拟测试通过”，不能报告系统快捷键问题已经验证修复。

## 8. WinUI 3 可行性

WinUI 3 可以用于 Simpilot 的设置界面，推荐采用混合结构：

```text
Win32/C++
├─ 托盘图标与菜单
├─ 专用键盘线程
├─ Everything 与核心逻辑
└─ 窗口生命周期桥接

C++/WinRT + WinUI 3
└─ 设置页面与添加热键对话框
```

键盘线程不应依赖 WinUI 3。界面迁移需要引入 Windows App SDK，并重新决定 unpackaged/self-contained 或 MSIX 发布方式；这会增加构建配置、运行时组件和发布体积。因此它应作为独立批次完成，不能与低级键盘钩子重构混合验证。
