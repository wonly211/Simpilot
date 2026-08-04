# Quick Start

[简体中文](../zh-CN/快速开始) | [繁體中文](../zh-TW/快速開始) | **English**

This guide gets Simpilot running and opens your first quick-launch menu.

## System requirements

- Windows 10 or Windows 11.
- An x64 processor and operating system.
- A fixed program folder writable by the current user.

## Download and extract

1. Download the latest `Simpilot-*-win-x64.zip` from [GitHub Releases](https://github.com/wonly211/Simpilot/releases/latest).
2. Extract the **entire** archive to a fixed location, such as a personal software folder or a data drive.
3. Keep `Simpilot.exe` and the `Everything/` folder in their original relative locations. Do not copy the executable by itself.
4. Run `Simpilot.exe`.

Each release includes a `.sha256` file. To verify a downloaded archive in PowerShell, run:

```powershell
Get-FileHash .\Simpilot-*-win-x64.zip -Algorithm SHA256
```

Compare the result with the value in the matching `.sha256` file. Current release packages are not digitally signed. If Windows SmartScreen reports an unknown publisher on first launch, confirm that the archive came from an official repository release and verify its checksum first.

## Program folder

Simpilot is portable. After its first run, settings, caches, and logs remain in the program folder:

```text
Simpilot.exe
Language.lng                 # optional additional-language package
Everything/
  Everything.exe
  Everything64.dll
Config/
  Simpilot.ini
  Simpilot2.ini              # created only when a second menu is used
  Setting.ini
Cache/
  RunIcon/
Log/
  Simpilot.log
```

`Language.lng` is optional. If it is missing, Simpilot ignores it and continues with its built-in Simplified Chinese, Traditional Chinese, and English resources. Keep the `Everything/` folder in place: without it, Everything Search and pathless-program discovery may be unavailable, but Simpilot itself can still start.

## First launch

Simpilot creates a tray icon instead of opening a regular main window. If you cannot see it, look in the taskbar's hidden-icons area.

- **Left-click** the icon to open the quick-launch menu.
- **Right-click** the icon to open settings and maintenance actions.
- By default, the backtick key, usually below `Esc` and left of `1`, opens the quick-launch menu.

On first launch, Simpilot creates `Config/Simpilot.ini` with example entries. You can use those entries immediately or edit your own menu through **Settings > Quick Launch Menu**.

## Next steps

1. Read [Quick Launch Menu](Quick-Launch-Menu) to organize applications, folders, files, and websites.
2. Read [Global Hotkeys](Global-Hotkeys) to record convenient key combinations.
3. In **Settings > General**, choose an interface language or menu theme, or enable automatic startup after Windows sign-in.

Return to [Wiki Home](Home.en-US) for all topics. For complete operational details, including backup and troubleshooting, see the [Simplified Chinese user manual](https://github.com/wonly211/Simpilot/blob/main/docs/zh-CN/用户手册.md).
