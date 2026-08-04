<p align="center">
  <img src="assets/simpilot-icon.png" width="128" height="128" alt="简驭 | Simpilot icon">
</p>

<h1 align="center">简驭 | Simpilot</h1>

<p align="center">
  <a href="README.md">简体中文</a> |
  <a href="README.zh-TW.md">繁體中文</a> |
  <strong>English</strong>
</p>

<p align="center">
  Bring everyday shortcuts and frequent actions together in one tray icon and a set of global hotkeys.
</p>

<p align="center">
  <a href="https://github.com/wonly211/Simpilot/releases/latest"><img src="https://img.shields.io/github/v/release/wonly211/Simpilot?label=Release" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4" alt="Windows 10 and 11">
  <img src="https://img.shields.io/badge/Architecture-x64-5C6BC0" alt="x64">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/wonly211/Simpilot" alt="License"></a>
</p>

简驭 | Simpilot is a lightweight quick launcher and global hotkey manager for Windows. Instead of digging through nested folders or repeatedly searching the taskbar for a window, use the tray menu or familiar key combinations to launch apps, open folders and files, visit websites, or bring up Everything Search.

It is designed for office users, developers, content creators, and productivity enthusiasts who want to reduce mouse travel, organize scattered shortcuts, and keep frequent actions close at hand.

## Why Simpilot

| Capability | What It Gives You |
|---|---|
| Hierarchical quick-launch menus | Organize apps, folders, files, and websites around the way you work |
| Global hotkeys | Trigger frequent actions directly from any standard desktop application |
| Windows shortcut takeover | Block selected `Win+letter` combinations while Simpilot is running and give custom actions priority |
| Everything integration | Open or restore the search window with one action and locate programs whose full paths were not specified |
| Menu icons and themes | Extract clear icons automatically or select them manually; follow the system theme or choose light or dark mode |
| Portable and localized | Extract the complete archive and run it immediately; settings remain local, with 简体中文, 繁體中文, and English built in |

Simpilot is built with native C++20 and Win32. Its core features require no account or cloud service, and shortcut blocking does not modify Windows system policies. When Simpilot exits, its registered hotkeys and real-time shortcut blocking are removed automatically.

## Get Started in Three Steps

1. Go to [Releases](https://github.com/wonly211/Simpilot/releases/latest) and download the latest `Simpilot-*-win-x64.zip`.
2. Extract the complete archive to a fixed directory writable by the current user. Keep the `Everything/` and `Languages/` directory structure intact.
3. Run `Simpilot.exe`. Left-click the tray icon or press the backtick key to open the quick-launch menu. Right-click the tray icon for settings and maintenance.

Simpilot is a tray application and does not show a conventional main window after launch. If the icon is not visible, check the hidden-icons area of the taskbar.

> Current release packages are not digitally signed. If Windows SmartScreen reports an unknown publisher on first launch, verify that the file came from an official release in this repository and compare it with the included `.sha256` file.

## Common Workflows

- Use one hierarchical menu to organize work applications, project directories, frequently used documents, and websites.
- Assign convenient global hotkeys to meetings, screenshots, terminals, editors, or reference folders.
- Give combinations such as `Win+S` or `Win+G` to your own actions and restore normal Windows behavior automatically when Simpilot exits.
- Bring an existing application window to the foreground with the same hotkey instead of launching duplicate instances.
- Create a second menu alongside the main menu to keep work and personal shortcuts separate.
- Bring up Everything directly. When several versions of the same program are found, choose the path you actually want.

## Feature Overview

- Left-clicking the tray icon shows only the quick-launch menu. Right-clicking shows only settings, language, Everything maintenance, About, and Exit.
- Visually edit the main and second menus, including categories, separators, ordering, hierarchy, and menu access keys.
- Custom hotkeys can open applications, folders, or files. Application actions support arguments, a working directory, administrator privileges, existing-process behavior, and initial window state.
- Built-in hotkeys are available for the main menu, second menu, Settings window, and Everything Search. Each can be recorded, cleared, enabled, or paused independently.
- Supported `Win+A` through `Win+Z` combinations can be blocked in real time. `Win+L` cannot be overridden because of Windows security restrictions.
- Menu configuration changes are monitored automatically. If a reload fails, the last valid menu remains active.
- The tray icon is restored automatically when Windows Explorer restarts.
- Transparent 128x128 icon caches are generated automatically, and icons can also be selected manually from ICO, EXE, or DLL files.
- Simpilot uses the default Everything instance and the official SDK. If Everything is unavailable, Simpilot still starts normally. The menu editor can show or reselect resolved paths for programs configured without a full path.
- Per-Monitor V2 DPI keeps menus clear across displays with different scaling settings.
- Logs are stored in `Log/Simpilot.log`; entries older than 90 days are removed at startup.

## User Manual

For installation, menu editing, hotkey recording, Windows shortcut blocking, Everything integration, backup and migration, and troubleshooting, see:

**[简驭 | Simpilot User Manual (Simplified Chinese)](docs/user-manual.zh-CN.md)**

For problems, check the troubleshooting and FAQ sections first, then report the issue through [GitHub Issues](https://github.com/wonly211/Simpilot/issues). Review the log before posting and avoid exposing personal directory or file names.

## System Requirements

- Windows 10 or Windows 11
- An x64 processor and operating system
- A program directory writable by the current user

Simpilot is distributed as a portable application without an installer. Runtime settings, caches, and logs remain in the program directory for straightforward backup and migration.

<details>
<summary><strong>Build from Source</strong></summary>

Install Visual Studio 2022 with the **Desktop development with C++** workload and CMake 3.24 or newer.

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
cmake --build --preset package-release
```

The release package is generated at:

```text
build/vs2022-x64/Simpilot-<version>-win-x64.zip
```

</details>

<details>
<summary><strong>Runtime Directory</strong></summary>

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

`Simpilot.ini` and the optional `Simpilot2.ini` are the quick-launch menu data sources; `Setting.ini` stores the display language and application settings. Configuration is saved atomically in UTF-8, so most users do not need to edit it manually.

</details>

<details>
<summary><strong>Development and Design Documentation (Simplified Chinese)</strong></summary>

- [Localization resources](docs/localization.md)
- [Quick-launch menu editor](docs/menu-editor.md)
- [Custom global hotkeys](docs/custom-global-hotkeys.md)
- [Custom menu icons](docs/menu-custom-icons.md)
- [Menu icons and themes](docs/menu-icons-and-themes.md)
- [Windows shortcut audit and blocking](docs/windows-hotkey-audit.md)
- [PowerToys Keyboard Manager recording architecture](docs/powertoys-keyboard-recorder.md)

</details>

## Open-Source License and Third-Party Components

Simpilot is open source under the [GNU General Public License v3.0](LICENSE).

The release package includes Everything runtime components and reuses portions of Microsoft PowerToys Keyboard Manager. See [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) for complete source and license notices. Everything components remain in the separate `Everything/` directory; they are not embedded in `Simpilot.exe` or downloaded at runtime.
