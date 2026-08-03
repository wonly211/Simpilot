# 简驭 | Simpilot

简驭 | Simpilot 是一款 Windows C++20 快速启动工具，使用原生 Win32 托盘菜单执行命令和打开网址。

## 当前功能

- 读取 `Simpilot.ini` 和可选的 `Simpilot2.ini` 菜单配置。
- 支持层级分类、分隔线、菜单访问键、管理员标记、应用、文件夹、文件和网址条目。
- `Simpilot.ini` 和 `Simpilot2.ini` 统一使用 UTF-8，支持可选的 UTF-8 BOM。
- 支持标准 Windows 环境变量、`%SimpilotConfigDir%`、绝对路径、相对路径、Windows 系统目录和 PATH 程序解析。
- 通过官方 `Everything64.dll` SDK 查询无路径程序；发现多个有效候选时，按文件版本、修改时间和完整路径排序，并通过带图标的候选窗口让用户选择。
- 优先复用已运行的 Everything 默认实例，并识别客户端和默认服务的来源路径。
- 检测并启动已安装的 Everything 默认服务；托盘菜单可通过 UAC 安装或修复为随包服务。
- 随包启动 `Everything/Everything.exe`；服务或数据库不可用时自动退化到系统目录和 PATH。
- 将 Everything 查询结果持久化到程序解析缓存，缓存目标失效后自动清除。
- 实时监听 `Simpilot.ini` 和 `Simpilot2.ini`，修改后自动重载；读取失败时保留上一份有效菜单。
- 快捷启动菜单自动显示透明背景的应用、网址和分类图标；图标从 Windows 高分辨率系统图像列表提取，缺少 128×128 资源时自动缩放，并按解析后的目标路径以 128×128 ICO 缓存到 `Cache/RunIcon/`。
- 快捷启动菜单使用 24px 图标和 15px 文字，并根据显示器 DPI 等比例放大。
- 弹出菜单主题支持“跟随 Windows”“浅色”和“深色”，保存后下次打开菜单立即生效。
- 记录启动、Everything 状态、菜单解析和自动重载诊断日志；启动时自动清理超过 90 天的记录。
- 托盘菜单提供原生设置窗口，可设置主菜单、第二菜单和打开设置的全局热键。
- 托盘右键可打开可视化快捷启动菜单编辑器，在主菜单和第二菜单之间切换，并添加、编辑、删除和排序启动项、分类与分隔线。编辑器支持内嵌修改内容、调整层级、配置菜单访问键和管理员运行，并在外部配置发生变化时阻止无提示覆盖。
- 设置窗口提供“全局热键”页，可添加、编辑、删除及逐项启用或禁用自定义全局热键。“操作类型”支持“打开应用”“打开文件夹”和“打开文件”；应用动作还可配置参数、工作目录、运行权限、重复启动策略和启动后窗口状态。
- 已启用的内置或自定义 `Win+A`～`Win+Z`（不含 `Win+L`）会自动打开并锁定“Windows 快捷键屏蔽”页的对应开关，然后屏蔽 Windows 操作并执行简驭动作；禁用或删除后自动解除该联动。
- 设置窗口提供“Windows 快捷键屏蔽”页，可分别屏蔽受支持的 `Win+A`～`Win+Z`；所有已选组合在简驭 | Simpilot 运行期间由常驻低级键盘钩子实时拦截，保存后立即生效，无需重启 Windows 资源管理器。
- `Simpilot.exe` 内的专用键盘线程维护独立消息循环和唯一的低级键盘钩子。正常状态处理 Windows 热键屏蔽及全局热键；点击热键输入框后切换到 PowerToys 来源的录制状态并优先吞掉全部键盘事件，包括 `Win+R`、`Win+G` 等 Windows 组合。按 `Esc` 取消并保留原值；固定热键通过输入框右侧的“清除”按钮移除，`Backspace` 不再具有特殊清除行为。
- 热键与其他 Simpilot 功能或 Windows 已注册热键冲突时要求确认；确认覆盖系统冲突后使用键盘拦截方式尝试强制接管。
- 第二菜单热键直接弹出 `Simpilot2.ini`，无需先经过主菜单的“菜单 2”子菜单。
- 左键点击托盘图标只显示快捷启动菜单；右键点击只显示设置、配置维护、Everything、语言和退出等管理入口。
- 支持当前用户登录 Windows 后自动启动，不需要管理员权限。
- 内建简体中文、繁体中文和英文界面，默认使用简体中文。可在托盘菜单或“设置 > 常规 > 外观”中即时切换，无需重启。
- 使用 Per-Monitor V2 DPI 感知，保证不同缩放比例显示器上的菜单文字清晰。

## 构建

需要 Visual Studio 2022“使用 C++ 的桌面开发”工作负载和 CMake：

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
cmake --build --preset package-release
```

发布包生成于：

```text
build/vs2022-x64/Simpilot-0.16.0-win-x64.zip
```

发布目录：

```text
Simpilot.exe
THIRD-PARTY-NOTICES.txt
Everything/
  Everything.exe
  Everything64.dll
  Everything.lng
  Everything.ini
Languages/
  en-US.json
  zh-CN.json
  zh-TW.json
```

Everything 组件不会嵌入 `Simpilot.exe`，也不会在运行时下载或释放。

## 运行时文件

运行期间会在程序目录下按需创建：

```text
Config/
  Simpilot.ini
Cache/
  program-cache.tsv
  RunIcon/
Log/
  Simpilot.log
```

保存设置后创建 `Config/Simpilot.settings.ini`；首次产生 Everything 程序解析结果后创建 `Cache/program-cache.tsv`。

首次显示快捷启动菜单中的目标时，会在 `Cache/RunIcon/` 生成透明的 128×128 `.ico` 文件。设置窗口的“菜单图标”页可为每个完整启动操作人工指定 ICO，或从 EXE、DLL 中选择图标；人工图标与自动缓存统一保存在该目录。

可选的第二菜单为 `Config/Simpilot2.ini`。手动选择界面语言后还会创建：

```text
Config/language.txt
```

内容为 `zh-CN`、`zh-TW` 或 `en-US`。缺少该文件或内容无效时使用默认的简体中文。

三种内建语言统一使用 `Languages/*.json` 资源。当前语言缺少某个文本或资源文件无法读取时，逐项回退到英文；英文仍缺少该文本时显示明确的缺译标记，不会静默显示空白。资源格式、键集合要求和新增语言流程见 [多语言资源设计](docs/localization.md)。

所有日志统一保存在 `Log/`，只写入 `Simpilot.log`，不读取或迁移其他目录中的日志。每次 Simpilot 启动时，日志中超过 90 天的记录会被安全清理。`program-cache.tsv` 仅保存程序名与已解析路径；Everything 返回多个候选时，用户确认的路径也保存在此处，目标文件被删除后对应缓存会自动失效。

`Simpilot.settings.ini` 保存开机启动、弹出菜单主题、固定全局热键、自定义全局热键和 Windows 热键屏蔽设置。热键只使用 `*Code=修饰键,虚拟键` 数值字段，不读取热键字符串。自定义热键必须明确包含 `Enabled`。主菜单首次默认使用反引号键；其他固定热键默认留空。确认强制覆盖的热键会使用低级键盘拦截，但 Windows 安全桌面组合（例如 `Ctrl+Alt+Del`）无法被普通应用覆盖。

弹出菜单主题保存在 `[General]` 段的 `MenuTheme`：`0` 跟随 Windows、`1` 浅色、`2` 深色。主题只作用于 Simpilot 的快捷启动菜单和托盘右键菜单，不改变 Windows 系统主题。菜单图标与主题的实现边界见 [菜单图标与主题设计](docs/menu-icons-and-themes.md)。

`Simpilot.ini` 和 `Simpilot2.ini` 仍是快捷启动菜单的唯一数据源。可视化编辑器以 UTF-8 原子保存配置，保留层级、分隔线、菜单访问键和管理员标记；完整编辑规则见 [可视化快捷启动菜单编辑器](docs/menu-editor.md)。人工图标按配置中的完整启动操作区分，具体规则见 [人工指定菜单图标](docs/menu-custom-icons.md)。

快捷启动菜单、第二菜单、设置窗口和 Everything 搜索统一作为不可删除的简驭功能热键，每项均可独立录制、清除、启用或暂停；关闭开关不会丢失按键。Everything 搜索默认使用 `Win+S` 且不启用，其配置不保存 EXE 路径，运行时直接调用 Everything 管理器。自定义热键保存在 `[CustomGlobalHotkeys]` 段，每项包含启用状态、按键、操作类型和目标路径。`Action=0` 打开应用，`Action=1` 打开文件夹，`Action=2` 使用 Windows 默认关联程序打开文件。只有应用动作使用参数、工作目录、管理员身份、已运行处理和窗口可见性；文件夹与文件动作不会扫描或复用进程。完整格式与运行时设计见 [自定义全局热键设计](docs/custom-global-hotkeys.md)。

Windows 快捷键屏蔽状态以 `DisabledWindowsHotkeys=AFG...` 的形式保存在 `Simpilot.settings.ini`。简驭 | Simpilot 不修改 Explorer 的 `DisabledHotkeys`、Game Bar 设置或 Windows 锁定策略，也不会要求重启 Explorer。Windows 快捷键屏蔽页不提供 `Win+L`。`Simpilot.exe` 内只有一个专用键盘线程和一个 `WH_KEYBOARD_LL`：非录制状态处理已选系统组合及自定义全局热键，录制状态优先执行 PowerToys 来源的 `DetectShortcutUIBackend` 并吞掉全部键盘事件。设置窗口和添加窗口只接收录制结果，不安装钩子。完整源码复用边界见 [PowerToys Keyboard Manager 录制架构复用说明](docs/powertoys-keyboard-recorder.md)，系统热键行为见 [Win+A～Win+Z 系统热键复核与屏蔽方案](docs/windows-hotkey-audit.md)。

开机启动项写入当前用户注册表：

```text
HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run\Simpilot
```

## 项目结构

- `include/simpilot`：可测试的公共接口。
- `src/core`：解析器、编码、变量、Everything、程序解析和多语言资源。
- `src/app`：Win32 托盘、原生菜单和执行入口。
- `tests`：核心兼容性测试。
- `docs`：功能审计、Windows 版本差异和开发决策记录。
- `third_party/Everything`：随发布包分发的 Everything 运行组件。
- `third_party/PowerToys`：按 MIT 许可复用的 Keyboard Manager 源码及许可。
- `Languages`：内建的 JSON 界面语言资源。
