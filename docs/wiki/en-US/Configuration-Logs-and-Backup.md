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
| `Config/Setting.ini` | Language, theme, automatic startup, hotkeys, keyboard mappings, and Windows hotkey blocking | Not recommended; related settings return to their defaults. |
| `Cache/program-cache.tsv` | Confirmed locations for applications configured without a full path | Yes; Simpilot resolves them again when needed. |
| `Cache/RunIcon/` | Automatic and custom icons | Yes; automatic icons are recreated, but custom icons are lost. |
| `Log/Simpilot.log` | Startup, menu, Everything, hotkey, and error diagnostics | Yes; only historical diagnostic data is lost. |

Menu configuration and `Setting.ini` use UTF-8. Use the Settings window for normal changes. If you edit a menu manually, use a UTF-8-capable text editor.

Keyboard mappings are stored in the `[KeyboardMappings]` section of `Setting.ini`. Older files without
that section create no mappings and retain existing hotkey behavior. Startup preflight preserves and
reports invalid configuration for repair instead of skipping rules and overwriting them with defaults.
The Settings window also validates the complete rule set before saving.

## Log retention

Simpilot uses only one log file: `Log/Simpilot.log`. At startup, it removes timestamped entries older than 90 days. It does not create separate date-rotated log files.

Logs can contain program paths, file names, and error data. Inspect and redact personal paths or file names before sharing a log.

## Backup and move

Since v1.0.11, **Settings > General** can export all saved settings or migrate an older folder, as described below. You can also back up manually:

1. Select **Exit** from the tray right-click menu.
2. Copy the entire Simpilot program folder to a backup location.
3. To restore, fully extract an equivalent or newer release package, then copy the saved configuration files listed below into it, preserving their relative paths.

A manual backup should retain `Config/`, `Cache/program-cache.tsv`, `Cache/RunIcon/`, and your custom `Language.lng` to preserve input-method history, program selections, and custom icons. Logs can be kept separately if needed. Copying the whole directory also retains existing `Backups/` recovery packages.

## Updating

Exit Simpilot before extracting the full new package over program files in the same folder; existing configuration is reused. When extracting into a new folder, choose migration on first launch or under **Settings > General**. The configuration name remains `Config/Setting.ini`; unsupported historical file names are not converted automatically.

After moving the entire program folder, reopen **Settings > General** and apply the automatic-startup option again if it is enabled. This updates the startup location to the new path.

## Related pages

- [Quick Launch Menu](Quick-Launch-Menu)
- [Menu Icons and Themes](Menu-Icons-and-Themes)
- [FAQ and Troubleshooting](FAQ-and-Troubleshooting)

### Global backup, restore, and migration

Use **Settings > General > Settings backup and migration** to view the configuration directory, **Export all settings**, **Import backup**, or **Migrate old folder**. A `.simpilot-backup` file includes saved module settings, both menus, menu icons, input-method history, program selections, and the external language pack. Unsaved drafts can be applied first or left out of the export.

Import restores the entire managed configuration. Managed files absent from the backup, including a second menu, are removed. Before restoring, Simpilot saves the current configuration in `Backups/`, then restarts to load the restored settings. A failed restore rolls back; the next startup recovers interrupted operations. Invalid formats, checksums, or rules are reported instead of silently dropping settings. Automatic backups remain until you remove them.

Starting in a directory with no configuration offers a fresh setup, migration from an old folder, or backup import. Versions 1.0.8, 1.0.9, and 1.0.10 can migrate directly without first gaining an export button. The source folder is unchanged. Other versions are checked against supported file formats.

Portable storage is unchanged. Updating program files in the same folder keeps settings; extracting into a new folder does not automatically discover the previous folder. Unreadable or damaged settings are retained and reported, rather than overwritten by defaults.

Backups exclude application binaries, Everything components, logs, and external programs or scripts referenced by menus. After moving between folders or computers, check relative paths, unavailable applications, and missing input methods. Simpilot does not install or download these dependencies. The current backup format supports up to 128 MiB and 8192 files.
