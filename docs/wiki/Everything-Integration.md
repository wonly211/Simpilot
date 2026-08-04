# Everything Integration

[简体中文](Everything-集成) | [繁體中文](Everything-整合) | **English**

Everything is an optional search capability for Simpilot. It opens or restores the Everything Search window and helps resolve application entries that contain only a program name instead of a complete path.

Simpilot does not require Everything to start. If Everything is unavailable, the tray menu, entries with complete paths, and most global hotkeys continue to work. Only Everything Search and discovery of pathless applications are affected.

## Bundled components

Keep this release layout intact:

```text
Simpilot.exe
Everything/
  Everything.exe
  Everything64.dll
  Everything.ini
```

- `Everything64.dll` is the official SDK used to query an Everything database. It stays in `Everything/` and is not embedded in `Simpilot.exe`.
- `Everything.exe` is the bundled default client and the source used to repair the service.
- Simpilot does not download Everything at runtime and does not modify separately installed Everything program files.

## Startup and reuse behavior

At startup, Simpilot attempts to connect to the default Everything instance:

1. If the default instance database is available, Simpilot uses it. The instance does not need to come from the Simpilot folder.
2. If the database is unavailable and the Windows service named `Everything` is installed but stopped, Simpilot attempts to start the service.
3. If the database remains unavailable, Simpilot attempts to start the bundled `Everything/Everything.exe`.
4. If those steps fail or components are missing, Simpilot still starts and reports Everything-dependent actions as unavailable.

The default instance is Everything's standard instance when no instance name is specified. Most users use it. If you run only a named instance, Simpilot may not connect to its database and can then attempt to start the bundled default instance.

## Open or restore Everything

Use either of these actions:

- Right-click the tray icon and select **Maintenance > Open Everything**.
- Enable the **Everything Search** built-in hotkey under **Settings > Global Hotkeys**. Its default combination is `Win+S`, but it is disabled by default.

If an Everything Search window already exists, Simpilot restores and brings it to the foreground. If no window exists, Simpilot prefers the active default client's path and otherwise uses the bundled `Everything/Everything.exe`. Closing the search window does not prevent a later Simpilot action from opening it again.

## Everything service

The service is not required to run Simpilot. It can make indexing more reliable, but installing or repairing it requires administrator approval.

1. Right-click the Simpilot tray icon.
2. Choose **Maintenance > Install/Repair Everything Service**.
3. Approve the Windows UAC prompt.

Simpilot uses the bundled `Everything.exe` service-install function to repair the default service. Canceling UAC, a failed service installation, or a failed service start does not affect other Simpilot features.

## Resolve a program name

When an **Open application** item contains only a filename such as `tool.exe`, Simpilot searches normal Windows locations, then `PATH`, then an available Everything database.

When Everything returns multiple candidates, Simpilot shows a selection dialog. Candidates are ordered by:

1. Highest file version.
2. Most recent modification time when versions match.
3. Case-insensitive full-path alphabetical order when both values match.

The selected result is stored in `Cache/program-cache.tsv`. If that target is moved or deleted, the cache expires and Simpilot resolves it again when needed. You can also choose **Reselect** beside the current resolved path in the Quick Launch Menu editor.

## Known boundary

- Only the default Everything instance is supported; named instances are not guaranteed to work.
- When the default instance is running, Simpilot does not depend on the path of its `Everything.exe`.
- Without `Everything64.dll`, Simpilot cannot query the Everything database, but it can still start.

For troubleshooting, see [FAQ and Troubleshooting](FAQ-and-Troubleshooting).
