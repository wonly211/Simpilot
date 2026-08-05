# Configuration, Logs, and Backup

[简体中文](../zh-CN/配置-日志与备份) | **English**

Simpilot is a portable application. Its runtime data remains in the program folder so that it can be backed up, moved, and diagnosed easily. Extract the complete release package to a fixed directory writable by the current Windows user.

## Files and folders

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

| Location | Purpose | Can it be removed? |
| --- | --- | --- |
| `Config/Simpilot.ini` | Main quick-launch menu | Not recommended; removing it loses the main-menu content. |
| `Config/Simpilot2.ini` | Optional second quick-launch menu | Yes, when the second menu is no longer needed. |
| `Config/Setting.ini` | Language, theme, automatic startup, hotkeys, and Windows hotkey blocking | Not recommended; related settings return to their defaults. |
| `Cache/program-cache.tsv` | Confirmed locations for applications configured without a full path | Yes; Simpilot resolves them again when needed. |
| `Cache/RunIcon/` | Automatic and custom icons | Yes; automatic icons are recreated, but custom icons are lost. |
| `Log/Simpilot.log` | Startup, menu, Everything, hotkey, and error diagnostics | Yes; only historical diagnostic data is lost. |

Menu configuration and `Setting.ini` use UTF-8. Use the Settings window for normal changes. If you edit a menu manually, use a UTF-8-capable text editor.

## Log retention

Simpilot uses only one log file: `Log/Simpilot.log`. At startup, it removes timestamped entries older than 90 days. It does not create separate date-rotated log files.

Logs can contain program paths, file names, and error data. Inspect and redact personal paths or file names before sharing a log.

## Backup and move

The most reliable backup method is:

1. Select **Exit** from the tray right-click menu.
2. Copy the entire Simpilot program folder to a backup location.
3. To restore, fully extract an equivalent or newer release package, then copy the backed-up `Config/` folder into it.

For a minimal backup, keep at least `Config/`. Also keep `Cache/RunIcon/` when you use custom menu icons. `program-cache.tsv` and the log do not contain settings and do not need to be backed up.

## Updating

Exit Simpilot before extracting a new version and replacing program files. Configuration is not migrated automatically. Continue to use the current file name `Config/Setting.ini`; historical setting file names are not read automatically.

After moving the entire program folder, reopen **Settings > General** and apply the automatic-startup option again if it is enabled. This updates the startup location to the new path.

## Related pages

- [Quick Launch Menu](Quick-Launch-Menu)
- [Menu Icons and Themes](Menu-Icons-and-Themes)
- [FAQ and Troubleshooting](FAQ-and-Troubleshooting)
