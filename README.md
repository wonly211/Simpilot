<p align="center">
  <img src="assets/simpilot-icon.png" width="128" height="128" alt="简驭 | Simpilot 图标">
</p>

<h1 align="center">简驭 | Simpilot</h1>

<p align="center">
  <strong>简体中文</strong> |
  <a href="README.zh-TW.md">繁體中文</a> |
  <a href="README.en-US.md">English</a>
</p>

<p align="center">
  把常用入口和高频操作，收进一个托盘图标与一组全局热键。
</p>

<p align="center">
  <a href="https://github.com/wonly211/Simpilot/releases/latest"><img src="https://img.shields.io/github/v/release/wonly211/Simpilot?label=Release" alt="最新版本"></a>
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4" alt="Windows 10 和 11">
  <img src="https://img.shields.io/badge/Architecture-x64-5C6BC0" alt="x64">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/wonly211/Simpilot" alt="许可证"></a>
</p>

简驭是一款面向 Windows 的轻量级快捷启动器与全局热键管理器。无需打开层层文件夹，也不必在任务栏反复寻找窗口：通过托盘菜单或熟悉的按键组合，即可启动应用、打开文件夹与文件、访问网址，或唤出 Everything 搜索。

它适合希望减少鼠标往返、整理零散入口，并让常用操作始终触手可及的办公用户、开发者、内容创作者和效率工具爱好者。

## 为什么选择简驭

| 能力 | 你能获得什么 |
|---|---|
| 分层快捷启动菜单 | 把应用、文件夹、文件和网址按自己的工作方式分类整理 |
| 全局热键 | 从任何普通桌面应用中直接触发常用操作 |
| Windows 快捷键接管 | 在简驭运行期间屏蔽选定的 `Win+字母`，并优先执行自定义动作 |
| Everything 集成 | 一键打开或恢复搜索窗口，并帮助定位没有填写完整路径的程序 |
| 菜单图标与主题 | 自动提取清晰图标，也可人工指定；支持跟随系统、浅色和深色主题 |
| 便携与本地化 | 完整解压即可运行，配置保存在本地；内建简体中文、繁體中文和 English，并可用 `Language.lng` 扩展其他语言 |

简驭使用原生 C++20 与 Win32 构建，核心功能不需要账户或云端服务，也不会为了屏蔽快捷键而修改 Windows 系统策略。关闭简驭后，由它注册的热键和实时快捷键屏蔽会自动解除。

## 三步开始

1. 前往 [Releases](https://github.com/wonly211/Simpilot/releases/latest) 下载最新的 `Simpilot-*-win-x64.zip`。
2. 将压缩包完整解压到当前用户可写的固定目录，并保持 `Everything/` 目录结构不变。
3. 运行 `Simpilot.exe`。左键单击托盘图标或按反引号键打开快捷启动菜单；右键单击托盘图标进入设置与维护。

简驭是托盘应用，启动后不会显示普通主窗口。若没有看到图标，请检查任务栏的隐藏图标区域。

> 当前发布包尚未进行数字签名。若 Windows SmartScreen 首次运行时提示未知发布者，请先确认文件来自本仓库的正式 Release，并使用随包 `.sha256` 文件核对下载内容。

## 典型使用方式

- 用一个分层菜单集中管理工作软件、项目目录、常用文档和网站。
- 为会议、截图、终端、编辑器或资料目录设置顺手的全局热键。
- 将 `Win+S`、`Win+G` 等组合交给自己的操作，并在简驭退出后自动恢复系统行为。
- 使用同一热键恢复已经运行的应用窗口，避免重复启动。
- 在主菜单之外建立第二菜单，将工作与个人入口分开。
- 直接唤出 Everything；当同名程序存在多个版本时，自行选择真正需要的路径。

## 功能概览

- 左键托盘图标只显示快捷启动菜单，右键只显示设置、语言、Everything 维护、关于和退出。
- 可视化编辑主菜单与第二菜单，支持分类、分隔线、排序、层级和菜单访问键。
- 自定义热键可打开应用、文件夹或文件；应用动作支持参数、工作目录、管理员权限、重复启动策略和窗口状态。
- 内置快捷启动菜单、第二菜单、设置窗口和 Everything 搜索四类功能热键，可分别录制、清除、启用或暂停。
- 支持实时屏蔽受支持的 `Win+A` 至 `Win+Z`，其中 `Win+L` 因 Windows 安全限制不提供覆盖。
- 自动监听菜单配置变化；读取失败时保留上一份有效菜单。
- Windows 资源管理器重启后自动恢复托盘图标。
- 自动生成透明的 128x128 图标缓存，并可从 ICO、EXE 或 DLL 中人工选择图标。
- 使用 Everything 默认实例与官方 SDK；Everything 不可用时不影响简驭本身启动，并可在菜单编辑器中查看或重新选择无路径程序的解析结果。
- 支持 Per-Monitor V2 DPI，菜单在不同缩放比例的显示器上保持清晰。
- 日志统一保存在 `Log/Simpilot.log`，启动时清理超过 90 天的记录。

## 用户手册

安装、菜单编辑、热键录制、Windows 快捷键屏蔽、Everything、备份迁移和故障排查，请阅读：

**[《简驭 | Simpilot 用户手册》](docs/用户手册.md)**

也可访问 **[Simpilot Wiki](https://github.com/wonly211/Simpilot/wiki)**，按任务查阅快速开始、菜单、热键、Everything、语言包和故障排查。

遇到问题时，可先查看手册的故障排查与常见问题章节，再到 [GitHub Issues](https://github.com/wonly211/Simpilot/issues) 反馈。提交问题前请检查日志，避免公开个人目录名或文件名。

## 系统要求

- Windows 10 或 Windows 11
- x64 处理器与操作系统
- 一个当前用户可写的程序目录

简驭采用便携方式发布，不提供安装程序。运行时配置、缓存和日志均保存在程序目录下，便于备份和迁移。

<details>
<summary><strong>从源代码构建</strong></summary>

需要 Visual Studio 2022“使用 C++ 的桌面开发”工作负载和 CMake 3.24 或更高版本。

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
cmake --build --preset package-release
```

发布包生成于：

```text
build/vs2022-x64/Simpilot-<version>-win-x64.zip
```

</details>

<details>
<summary><strong>运行时目录</strong></summary>

```text
Config/
  Simpilot.ini
  Simpilot2.ini
  Setting.ini
Cache/
  program-cache.tsv
  RunIcon/
Log/
  Simpilot.log
```

`Simpilot.ini` 与可选的 `Simpilot2.ini` 是快捷启动菜单的数据源；`Setting.ini` 保存界面语言和应用设置。配置统一使用 UTF-8 原子保存，普通用户无需手工修改。

</details>

<details>
<summary><strong>开发与设计文档</strong></summary>

- [多语言资源设计](docs/多语言资源与发布.md)
- [快捷启动菜单编辑器](docs/快捷启动菜单编辑器.md)
- [自定义全局热键](docs/自定义全局热键.md)
- [人工指定菜单图标](docs/menu-custom-icons.md)
- [菜单图标与主题](docs/菜单图标与主题.md)
- [Windows 快捷键复核与屏蔽方案](docs/Windows快捷键屏蔽复核.md)
- [PowerToys Keyboard Manager 录制架构复用说明](docs/PowerToys键盘录制架构.md)

</details>

## 开源许可与第三方组件

简驭依据 [GNU General Public License v3.0](LICENSE) 开源。

发布包包含 Everything 运行组件，并复用 Microsoft PowerToys Keyboard Manager 的部分源码；完整许可与来源说明见 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)。Everything 组件位于独立的 `Everything/` 目录，不会嵌入 `Simpilot.exe`，也不会在运行时下载。
