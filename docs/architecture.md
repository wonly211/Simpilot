# Architecture

## System Context

Simpilot 为 Windows 桌面用户提供托盘快捷启动菜单、全局热键、指定 Windows 快捷键屏蔽和 Everything 集成。程序在当前交互式用户会话中运行，以便携目录保存状态，不提供服务器、账户或云端同步。

外部交互对象包括：

- Windows Shell、通知区、窗口管理、全局热键和低级键盘钩子；
- 当前用户注册表启动项；
- Everything 默认实例、Windows 服务和随包客户端；
- 用户配置要启动的应用、文件、目录和网址；
- 本地配置、缓存、图标和日志文件。

## Major Components

### Core

`simpilot_core` 负责可测试的数据与基础设施逻辑：

- `MenuDocument`、`MenuCategory`、`MenuEntry` 等菜单模型；
- UTF-8 配置读取、菜单解析、校验和规范化写回；
- 命令拆分、变量展开和程序路径解析；
- Everything SDK 封装、客户端及服务协调；
- `AppSettings`、程序缓存、日志、原子文件替换和配置监听；
- 内置及外部语言包加载。

核心层仍使用部分 Windows API，因此“core”表示与完整托盘 UI 分离，而非跨平台领域层。

### Keyboard

`simpilot_keyboard` 由 `KeyboardManager`、`KeyboardCaptureState`、
`KeyboardMappingEditorModel` 和 `KeyboardMappingEngine` 组成。编辑模型在 UI 线程把
录制结果或结构化下拉选择规范化为物理键规则；其余组件管理专用线程、消息窗口、唯一
`WH_KEYBOARD_LL`、标准/强制热键注册、热键录制、物理键盘映射及 Win+字母屏蔽。
映射规则在 UI 线程校验并复制到键盘线程；键盘线程只使用预构建规则表维护前缀、
待回放事件和活动目标按键，不在低级钩子回调中访问文件、等待或写日志。

### Application And UI

`simpilot` 是单一 Win32 可执行程序。`TrayApplication` 持有核心服务和长期状态，处理托盘消息、菜单展示、命令执行、设置、Everything 维护和退出。全部窗口使用手写 HWND、Common Controls 与 GDI 布局。

## Dependency Direction

正常构建依赖为：

```text
simpilot -> simpilot_keyboard -> simpilot_core
simpilot ---------------------> simpilot_core
```

应用层负责组合，不允许核心层依赖 `TrayApplication` 或具体窗口。核心层通过 `IProgramSearch`、候选选择器和诊断回调接入外部行为。

## Startup Architecture

1. `wWinMain` 设置 Per-Monitor V2 DPI，解析自身路径并创建 `Local\Simpilot` 互斥量；
2. 已存在实例时尝试向首实例发送自定义消息，然后退出；
3. `TrayApplication` 根据 EXE 目录构造配置、缓存、日志和图标路径，并加载设置和缓存；
4. `run()` 创建隐藏顶层窗口、注册 `TaskbarCreated` 并添加托盘图标；
5. 启动键盘线程、健康定时器和鼠标定位器，按设置同步当前用户启动项；
6. 动态加载 Everything SDK，尝试复用默认实例、启动服务或随包客户端；
7. 解析主菜单及可选第二菜单；解析失败时保留上一份有效模型；
8. 注册功能及自定义热键，启动配置监听线程；
9. UI 线程进入 `GetMessageW` 循环。

## Data Flow

菜单数据从 UTF-8 文本进入 `MenuParser`，形成多态 `MenuDocument` 树。程序项经过变量展开、命令解析、路径搜索、缓存和 Everything 候选选择，得到可执行目标。菜单展示时，`LaunchMenuRenderer` 将模型递归转换为 `HMENU`；用户选择后由 `command_entries_` 映射回条目并交给 Shell 执行。

设置窗口在内存中维护设置和菜单草稿。保存时先校验，再通过 `AtomicFileReplacement` 写入配置；运行时状态、热键和图标随后更新。跨文件失败通过补偿式备份和恢复处理，不是数据库事务。

键盘映射从 `Setting.ini` 的 `[KeyboardMappings]` 节加载为
`KeyboardMappingRule`。设置窗口保存前执行整表校验，`KeyboardManager::update_mappings`
在键盘线程预构建不可变查找表后替换运行时规则；前缀事件在 250 ms 内等待完整匹配，
超时或失配时按原始物理事件回放。命中时抑制完整源序列并通过带独立标记的
`SendInput` 发送目标，目标注入和回放均不会再次进入映射引擎。

录制与运行时映射使用两个独立状态机。`KeyboardCaptureState` 接收
`WM_KEYDOWN`、`WM_SYSKEYDOWN`、`WM_KEYUP` 和 `WM_SYSKEYUP` 四类低级钩子消息，
在会话期间抑制消息，等所有已按下的键释放后发布一次结果；裸 `Esc` 取消 legacy
录制，`Backspace` 可作为普通物理键录制。映射编辑器可使用物理键和单级 chord 录制，
也可通过左右修饰键及动作键下拉框编辑同一份结构化草稿；裸 `Esc` 仍可作为源键，取消由对话框命令完成。结果通过会话号 mailbox 传递，迟到
结果不会覆盖新会话。

`KeyboardMappingEngine` 只在键盘线程维护按下集合、前缀队列和活动目标组合。只有
配置规则的前缀才进入最多 16 项队列；完整命中后抑制源序列，按目标修饰键正序按下、
动作键按下，源动作键自动重复时重复发送目标动作 key-down，最后一个源键释放后按
动作键再修饰键的反序释放。超时、失配或队列溢出
按原始物理事件回放。目标注入标记和回放标记均在入口处短路，避免递归；`SendInput`
部分失败时清理活动目标并尽力回放源事件。其他进程注入的 `LLKHF_INJECTED` 输入
跳过映射；已经传递的无关修饰键不会与源或目标拼成隐式 `Win+L` 或安全注意序列。
引擎只发布诊断标志，由 UI 健康检查定时器写日志。

## State Management

- `TrayApplication`：托盘宿主、菜单模型、命令映射、设置、服务对象和运行状态；
- `KeyboardManager`：UI 侧线程句柄和键盘线程私有状态；
- `KeyboardCaptureState`：键盘线程上的 legacy 热键及物理映射录制候选；
- `KeyboardMappingEngine`：键盘线程上的规则表、前缀缓冲、活动映射和目标按键生命周期；
- `SettingsWindow`：单次模态编辑会话的草稿与回滚材料；
- `ProgramResolutionCache`：进程内映射及 TSV 持久化；
- `MenuIconCache`：进程内 HICON 和磁盘 ICO；
- `ConfigWatcher`：被监视文件的大小、时间和内容哈希快照。

## Error Handling

- 配置解析、文件操作和 Windows API 失败通常转为日志、不可用标记、用户消息或默认设置；
- 菜单重载失败保留上一份有效菜单，避免用部分状态替换现有模型；
- Everything 不可用时降级，不能阻止主程序启动；
- 日志写入失败被有意忽略，因为诊断设施不能阻止启动或打开菜单；
- 核心持久化使用同目录临时文件和替换操作降低部分写入风险；
- 一些 `noexcept` 基础设施选择回退默认值，因此调用者无法区分所有损坏原因，详细信息依赖日志或测试。

## Concurrency

### UI Thread

拥有所有可见和隐藏窗口、菜单对象及大部分运行时模型。Everything 服务维护和程序候选处理也在该线程执行。

### Keyboard Thread

通过 `CreateThread` 启动，拥有消息窗口、消息循环和低级钩子。UI 使用 `SendMessageTimeoutW` 更新状态，键盘线程使用 `PostMessage` 通知 UI。

低级钩子中的顺序固定为：录制捕获、键盘映射、现有强制热键/Win 覆盖、Windows
快捷键屏蔽、`CallNextHookEx`。映射前缀最多缓冲 16 个事件；定时器在键盘线程消息
循环中调用 `on_timer`，不会在钩子回调内睡眠。录制结果通过固定大小 mailbox 发布，
以 release/acquire 会话号同步到 UI 线程，避免在钩子回调中加锁或分配内存。

同一定时器刷新当前前台进程基名。查询失败时映射引擎清空当前应用范围，仅全局规则
可匹配；设置对话框的“使用前台应用”读取线程最近观察到的外部前台进程，避免把
Simpilot 自己的模态窗口保存为作用域。

### Configuration Thread

使用 `std::thread` 和目录通知等待配置变化，只投递值消息。停止由事件唤醒并 join。

活动菜单期间配置刷新会被推迟，因为命令映射持有指向当前菜单模型的指针。设置窗口和对话框包含嵌套消息循环，因此必须考虑重入和 `WM_QUIT` 转交。

## Platform Integration

- DPI：Per-Monitor V2；
- 执行级别：`asInvoker`，`uiAccess=false`；
- 托盘：`Shell_NotifyIconW` 与 `TaskbarCreated`；
- 热键：`RegisterHotKey` 与 `WH_KEYBOARD_LL`；
- 键盘映射：物理 `vkCode + scanCode + extended`，目标通过 `SendInput` 注入并使用独立标记；
- 启动项：HKCU `Run`；
- 程序执行：`ShellExecuteW` / `ShellExecuteExW`；
- Everything：动态 DLL、进程枚举、SCM、可选 `runas`；
- 配置监听：`ReadDirectoryChangesW`；
- UI：Common Controls、GDI、Shell 图标和原生菜单；
- 主题：Windows 版本检查后调用 `uxtheme.dll` 菜单入口，不可用时回退。

## Architectural Risks

当前已确认的风险和严重度见 `docs/project-state.md`。任何修复都应作为独立行为变更，补充针对性测试，而不是与文档、CI 或无关 UI 调整混合提交。

## Historical Rationale

仓库中的专项设计文档可以证明部分功能的选择过程。未被源码、提交或文档明确记录的早期架构原因无法从当前仓库确定。
