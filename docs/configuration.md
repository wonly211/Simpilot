# Configuration

## Configuration Model

Simpilot 是便携应用。所有路径以 `Simpilot.exe` 所在目录为根，不使用安装目录、Roaming AppData 或系统级配置。普通设置应通过 UI 修改；菜单文件也可以使用 UTF-8 文本编辑器维护。

## Files And Sources

| 配置 | 来源 | 默认/行为 | 使用位置 | 敏感 | 必需 |
|---|---|---|---|---|---|
| `Config/Simpilot.ini` | 用户或菜单编辑器 | 缺失时创建默认菜单 | `TrayApplication::reload_menu` | 否 | 是 |
| `Config/Simpilot2.ini` | 用户或菜单编辑器 | 缺失表示没有第二菜单 | `TrayApplication::reload_menu` | 否 | 否 |
| `Config/Setting.ini` | 设置窗口 | 缺失或不可解析时使用 `AppSettings` 默认值 | `AppSettingsStore` | 可能含本机路径 | 否 |
| `Language.lng` | 用户提供 | 缺失或损坏时仅使用三种内置语言 | `Localization` | 否 | 否 |
| `Everything/Everything.ini` | 随发布包 | 随包固定配置 | Everything 客户端 | 否 | Everything 功能需要 |
| `.github/ci/release-baseline.json` | 维护者 | 最近正式版产物基线 | `tools/ci.ps1` | 否 | CI 必需 |

## Setting.ini Keys

`Setting.ini` 为 UTF-8、INI 风格键值文件。键名读取时不区分大小写。

### General

| 键 | 默认值 | 含义 |
|---|---|---|
| `Language` | `zh-CN` | `zh-CN`、`zh-TW`、`en-US` 或外部语言代码 |
| `StartWithWindows` | `0` | 是否写入当前用户启动项 |
| `MouseShakeLocatorEnabled` | `0` | 是否启用摇鼠标定位器 |
| `MenuTheme` | `0` | `0` 跟随系统、`1` 浅色、`2` 深色 |

### Built-In Hotkeys

每个功能使用 `<Name>Code`、`<Name>Force` 和 `<Name>Enabled`：

- `MainMenu`：默认反引号，启用；
- `SecondMenu`：默认无按键；
- `OpenSettings`：默认无按键；
- `EverythingSearch`：默认 `Win+S`，关闭。

`Code` 格式为 `<modifiers>,<virtual-key>` 的十进制值。`Force` 表示使用低级钩子接管；受支持的 Win+字母会在加载时强制启用该模式。Win+L 会被拒绝。

### Windows Hotkeys

`DisabledWindowsHotkeys` 保存要屏蔽的字母集合。仅接受 A-Z，L 会被忽略。

### Custom Global Hotkeys

`CustomGlobalHotKeyCount` 最大读取 128。每项使用编号前缀并包含：

```text
Code
Force
Action
Program
Arguments
WorkingDirectory
RunAsAdministrator
ExistingProcessAction
Visibility
Enabled
```

`Action` 为应用、文件夹或文件；路径和参数可能包含个人目录等隐私信息，不应直接粘贴到公开 Issue。

### Keyboard Mappings

`[KeyboardMappings]` 是独立的物理键映射配置节。旧配置没有此节时，
`keyboard_mappings_enabled` 默认为启用但规则列表为空，其他设置行为不变。

| 键 | 含义 |
|---|---|
| `KeyboardMappingsEnabled` | 总开关，`0` 或 `1` |
| `KeyboardMappingCount` | 后续编号规则数量，最多 128 |
| `KeyboardMappingNEnabled` | 第 `N` 条规则是否启用 |
| `KeyboardMappingNProcess` | 可选的 `.exe` 基名；空值表示全局 |
| `KeyboardMappingNExactMatch` | `1` 精确匹配进程基名；`0` 使用不区分大小写的基名开头匹配 |
| `KeyboardMappingNSourceModifiers` | 源修饰键，按 `vk:scan:extended` 保存，以 `|` 分隔 |
| `KeyboardMappingNSourceAction` | 源第一个动作键，格式为 `vk:scan:extended` |
| `KeyboardMappingNSourceChord` | 源第二个动作键；无 chord 时为 `0` |
| `KeyboardMappingNTargetModifiers` | 目标修饰键，按 `vk:scan:extended` 保存，以 `|` 分隔 |
| `KeyboardMappingNTargetAction` | 目标动作键，格式为 `vk:scan:extended` |

`vk`、`scan` 和 `extended` 使用十进制值，`extended` 为 `0` 或 `1`。左右
Ctrl、Alt、Shift、Win 通过物理键值保留；旧的 `HotKeyGesture` 配置格式不迁移到此节。
源规则支持单键、含一至四个修饰键的快捷键，或最多三个修饰键加两个同时按住的动作键
的单级 chord。目标只支持单键或含最多四个修饰键的快捷键，不支持目标 chord。

保存时拒绝越界按键、重复源、歧义前缀、循环映射、非法目标、`Win+L` 和安全组合。
读取配置时无效的新规则会逐条跳过，其他规则和旧设置继续加载；
`AppSettingsStore::load` 的可选 `DiagnosticSink` 会报告被跳过的规则，正式运行时由
`TrayApplication` 接入 `Log/Simpilot.log`。进程值只保存
不含路径的大小写不敏感 `.exe` 基名；输入不带后缀时保存器会补上 `.exe`。

## Environment Variables

菜单目标支持 `%NAME%` 形式的当前进程环境变量，并提供项目变量 `%SIMPILOTCONFIGDIR%`。不存在的变量保持原样。程序路径解析还会读取当前进程 `PATH`。

项目没有要求用户设置的专用环境变量。

## Registry

启用开机启动时写入：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
Value name: Simpilot
```

值为带引号的当前 `Simpilot.exe` 路径。关闭开机启动时删除该值。项目不修改系统策略来屏蔽快捷键。

## External Components

- `Everything64.dll`：查询 Everything 默认实例数据库；
- `Everything.exe`：打开搜索窗口、启动客户端及安装/修复默认服务；
- Everything Windows 服务：名称固定为 `Everything`，不是主程序启动前提；
- Windows Shell、SCM、注册表、Common Controls、GDI 和用户程序：均为本地系统集成。

项目没有自有网络端点、监听端口、证书、API Key、密码或 token。

## Build And CI Configuration

- `CMakeLists.txt` 是版本、目标、编译选项、安装白名单和 CPack 的事实来源；
- `CMakePresets.json` 定义日常与 CI 构建目录；
- `.github/workflows/ci.yml` 只编排 `tools/ci.ps1`；
- `.github/ci/release-baseline.json` 的 `approvedGrowth` 只应用于经审查的体积增长，不是全局绕过开关。
