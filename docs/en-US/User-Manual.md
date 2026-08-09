# Simpilot User Manual

[简体中文](../zh-CN/用户手册.md) | **English**

Applies to version: 0.18.3

Simpilot is a Windows tray-based quick launcher and global hotkey manager. It organizes applications, folders, files, and websites into hierarchical menus, opens local targets through global hotkeys, and can block selected Windows shortcuts while Simpilot is running.

## 1. Installation and first launch

### 1.1 System requirements

- Windows 10 or Windows 11
- An x64 processor and operating system
- A program directory writable by the current user

### 1.2 Install Simpilot

Simpilot is distributed as a portable application and does not require an installer.

1. Download the latest `Simpilot-*-win-x64.zip` from [GitHub Releases](https://github.com/wonly211/Simpilot/releases/latest).
2. Extract the complete archive to a fixed directory.
3. Keep `Simpilot.exe` and the `Everything/` directory in their original relative locations.
4. Run `Simpilot.exe`.

Choose a directory that the current account can write to. Simpilot creates configuration, cache, and log files beside the application. Do not run it inside an archive preview, and do not copy only `Simpilot.exe` out of the package.

The main release files are:

```text
Simpilot.exe
Everything/
  Everything.exe
  Everything64.dll
  Everything.lng
  Everything.ini
```

Each release also provides a `.sha256` file. To verify the archive in PowerShell, run:

```powershell
Get-FileHash .\Simpilot-*-win-x64.zip -Algorithm SHA256
```

Compare the result with the matching `.sha256` file. Current packages are not digitally signed. If Windows SmartScreen reports an unknown publisher, first confirm that the archive came from the official repository and verify its checksum.

### 1.3 What happens on first launch

Simpilot does not open a conventional main window. It places an icon in the Windows notification area. If the icon is not immediately visible, check the taskbar's hidden-icons area.

On first launch, Simpilot creates `Config/Simpilot.ini` with example entries such as Notepad and Calculator. The default interface language is Simplified Chinese, and the default hotkey for the quick-launch menu is the backtick key, usually below `Esc` and to the left of `1`.

Simpilot also checks the default Everything instance and its database. If a usable instance is already available, it is reused. Otherwise, Simpilot attempts to start the bundled `Everything/Everything.exe`. Everything being unavailable does not prevent Simpilot from starting, but Everything Search and discovery of programs configured without a complete path may be unavailable.

## 2. Tray icon and menus

### 2.1 Left-click: quick-launch menu

Left-click the Simpilot tray icon to show only the quick-launch menu. Select an entry to open its application, folder, file, or website. Categories with an arrow open a submenu.

The menu uses the pointer position as its anchor and automatically opens left, right, up, or down according to the available area of the current monitor. Text and icons follow the monitor's scaling setting.

### 2.2 Right-click: management menu

Right-click the tray icon to show settings and maintenance commands:

- **Settings...** opens the complete Settings window.
- **Language** switches among Simplified Chinese, Traditional Chinese, and English.
- **Maintenance > Reload Menu** rereads the main and second menu configurations.
- **Maintenance > Open Everything** opens or restores the Everything Search window.
- **Maintenance > Install/Repair Everything Service...** requests administrator approval and repairs the bundled default Everything service.
- **About Simpilot** shows product and version information.
- **Exit** closes Simpilot and removes its registered hotkeys and shortcut blocking.

Changes to `Simpilot.ini` and `Simpilot2.ini` are normally detected automatically. Use **Reload Menu** to retry a failed load or confirm an external edit immediately.

If Windows Explorer restarts, Simpilot automatically restores its tray icon.

## 3. Settings window

The Settings window contains five pages:

1. General
2. Quick Launch Menu
3. Menu Icons
4. Global Hotkeys
5. Windows Hotkey Blocking

The buttons at the bottom behave as follows:

- **Save** applies all changes and closes Settings.
- **Apply** applies all changes while keeping Settings open.
- **Cancel** discards changes that have not yet been applied and closes Settings.

Changing the display language is saved immediately and refreshes the interface without waiting for **Save** or **Apply**. Most other settings take effect only after they are applied.

Previously applied built-in and custom hotkeys remain active while Settings is open. Keyboard input is temporarily redirected only while a hotkey recorder is actively recording; normal input resumes as soon as recording completes or is cancelled.

## 4. General settings

### 4.1 Start after Windows sign-in

Enable **Start Simpilot after signing in to Windows**, then select **Apply** or **Save**. The setting affects only the current Windows user and does not require administrator privileges.

If the whole Simpilot directory is moved, open Settings and apply this option again so the startup entry uses the new path.

### 4.2 Display language

Simpilot includes three interface languages:

- Simplified Chinese
- Traditional Chinese
- English

The interface changes immediately without restarting Simpilot or Windows Explorer. User-created menu names, category names, and target paths are not translated.

Additional languages can be installed by placing a compressed `Language.lng` beside `Simpilot.exe`. Simpilot loads compatible languages at startup. A missing, damaged, or incompatible language pack is ignored and does not affect the three built-in languages.

The selected language is stored as `Language=...` in `Config/Setting.ini`. If an external language omits a string, Simpilot falls back to English; if no value is available, `[missing translation]` is displayed instead of an empty control.

The **User Manual** link in About follows the current Simpilot display language. Simplified Chinese and Traditional Chinese open the Simplified Chinese manual. English and languages loaded from `Language.lng` open this English manual.

### 4.3 Quick-launch menu theme

- **Use Windows setting** selects the current Windows application theme whenever a menu opens.
- **Light** always uses a light menu.
- **Dark** always uses a dark menu.

This setting affects Simpilot menus only; it does not modify the Windows system theme. The new theme is used the next time a menu opens after the setting is applied.

## 5. Edit the quick-launch menu

Open **Settings > Quick Launch Menu** to manage the main menu and optional second menu.

### 5.1 Main and second menus

- The **main menu** reads `Config/Simpilot.ini` and opens from the tray icon or the built-in Quick Launch Menu hotkey.
- The **second menu** reads `Config/Simpilot2.ini` and can have its own built-in hotkey.

The second menu is optional. Simpilot creates `Simpilot2.ini` only after content is added and saved.

### 5.2 Add content

The commands above the tree add:

- **Category**: a submenu that can contain other items.
- **Launch item**: an application, folder, file, or website.
- **Separator**: a visual divider between sibling items.

Select an item in the tree and edit it directly in the panel on the right.

### 5.3 Edit a launch item

Launch items support these action types:

- **Open application** launches a program and can include arguments and administrator privileges.
- **Open folder** opens a directory in File Explorer.
- **Open file** uses the Windows default application for that file type.
- **Open website** uses the default browser for an `http://` or `https://` address.

Use **Browse...** to select a local target. The editor handles quoting when a path contains spaces.

When an application contains only a file name, such as `tool.exe`, the editor displays its currently resolved path. Simpilot searches Windows system directories, `PATH`, and confirmed Everything results. If an Everything result is no longer the desired one, select **Choose Again...** beside the resolved path.

### 5.4 Categories, order, and hierarchy

Use the controls below the tree to change the selected item:

- **Move Up / Move Down** changes its order among siblings.
- **Promote / Demote** changes its hierarchy level.
- **Delete** removes it. Deleting a non-empty category requires confirmation.

Drag the divider between the tree and editor to adjust the width of both areas.

### 5.5 Menu access keys

Categories and launch items can have a one-character menu access key. The tree displays it as `[A]`, and Simpilot generates the Windows menu access marker automatically. Do not type `(&A)` into the item name.

When the quick-launch menu is open, press the access key to select a category or run an item. Duplicate keys on the same level are highlighted; the same key may be reused on different levels.

A menu access key works only while its menu is open. It is different from a global hotkey.

### 5.6 Save and external edits

Selecting **Apply** or **Save** validates and writes both menu configurations. Invalid names, empty targets, or malformed website addresses stop the save and select the affected item for correction.

If another program modifies a menu file while the editor is open, Simpilot asks whether to overwrite the external change.

Menu files are monitored in real time. Saving a valid UTF-8 edit in a text editor normally refreshes the menu automatically. If loading fails, Simpilot keeps the last valid menu active.

## 6. Global hotkeys

Open **Settings > Global Hotkeys** to manage built-in and custom hotkeys.

### 6.1 Record, clear, and enable

To record a hotkey:

1. Select **Record Hotkey** or **Record Again**.
2. Press the key or combination you want.
3. Select **Apply** or **Save**.

During recording, the captured key events are not passed on to ordinary applications or Windows shortcut handlers. Press `Esc` to cancel recording and keep the previous value.

**Clear** removes the recorded combination and disables that item. Turning off its switch pauses it without deleting the combination. `Backspace` is an ordinary recordable key and is not used to clear a hotkey.

### 6.2 Built-in hotkeys

Simpilot provides four built-in actions that cannot be deleted:

| Action | Initial hotkey | Initial state |
|---|---|---|
| Quick Launch Menu | Backtick | Enabled |
| Second Menu | Not set | Disabled |
| Settings | Not set | Disabled |
| Everything Search | `Win+S` | Disabled |

The Everything Search action directly uses Simpilot's Everything integration and does not require a user-configured executable path.

### 6.3 Add a custom global hotkey

Select **Add**, then:

1. Record a key or combination.
2. Choose **Open application**, **Open folder**, or **Open file**.
3. Select an existing target.
4. For applications, optionally configure arguments, working directory, privileges, existing-process behavior, and initial window state.
5. Save the hotkey, then apply the Settings window.

Application actions provide three existing-process behaviors:

- **Show existing window** restores and activates a usable window. If the process has no usable window, Simpilot invokes the application again so its own single-instance logic can respond.
- **Start a new instance** always launches the application again.
- **Do nothing** skips the action when the same executable is already running.

Folder and file actions are handled directly by Windows and do not use application arguments, administrator privileges, duplicate-process behavior, or window-state options.

Use the switch in the custom hotkey list to pause an action without deleting it. Select an item to edit or delete it.

### 6.4 Hotkey conflicts

When saving, Simpilot checks whether the combination:

- duplicates a built-in hotkey;
- duplicates another custom hotkey; or
- is already registered by Windows or another application.

Simpilot explains the conflict and requests confirmation before replacing or attempting to take over a combination.

Exact `Win+A` through `Win+Z` combinations integrate with Windows Hotkey Blocking. `Win+L` is a Windows security shortcut and cannot be configured as a Simpilot global hotkey. Secure combinations such as `Ctrl+Alt+Del` cannot be overridden by a normal desktop application.

## 7. Windows hotkey blocking

Open **Settings > Windows Hotkey Blocking** to select supported combinations from `Win+A` through `Win+Z`. `Win+L` is intentionally not provided.

### 7.1 Effective behavior

- Changes take effect after **Apply** or **Save**; restarting Windows Explorer is not required.
- Blocking exists only while Simpilot is running and is removed on exit.
- Simpilot does not modify Windows policy or disable Game Bar, Feedback Hub, or other system features themselves.
- Both the left and right Windows keys are supported.
- Only the exact `Win+letter` combination is blocked. Blocking `Win+Z` does not block `Shift+Win+Z`, `Ctrl+Win+Z`, or `Alt+Win+Z`.

### 7.2 Integration with global hotkeys

If an enabled built-in or custom hotkey is exactly `Win+letter`, Simpilot automatically enables and locks the matching blocking switch. This prevents Windows from acting first and gives the Simpilot action priority.

When the final hotkey using that combination is disabled, cleared, or deleted, the automatic lock is removed. A separately selected manual blocking preference remains unchanged.

### 7.3 Unsupported scope

Simpilot cannot block shortcuts on the Windows secure desktop, including `Ctrl+Alt+Del` and `Win+L`. The sign-in screen, other user sessions, and some Remote Desktop environments are also outside the supported scope.

## 8. Everything integration

### 8.1 What Everything provides

Everything provides two Simpilot capabilities:

1. Open or restore the Everything Search window.
2. Locate an executable when a quick-launch item contains only a program name and normal path lookup cannot find it.

Everything components remain in the separate `Everything/` directory. They are not embedded in `Simpilot.exe` and are not downloaded at runtime.

### 8.2 When Everything is already running

Simpilot uses `Everything64.dll` to connect to the default Everything instance. If its database is ready, Simpilot reuses it regardless of where that Everything client is installed. When opening the search window, Simpilot first attempts to restore an existing window or use the source path of the running client.

Named Everything instances are not currently selected explicitly. If only a named instance is running, Simpilot may be unable to connect to its database and may start the bundled default instance.

### 8.3 When Everything is not running

Simpilot checks the default Everything service:

- If the service is installed but stopped, Simpilot attempts to start it.
- If the database remains unavailable, Simpilot attempts to launch `Everything/Everything.exe`.
- If the components are missing or startup fails, Simpilot continues running, but Everything Search and database-based program discovery are unavailable.

**Maintenance > Install/Repair Everything Service...** invokes the bundled Everything service setup and displays a Windows UAC prompt. Cancelling UAC does not affect other Simpilot features.

### 8.4 Multiple programs with the same name

When Everything returns multiple valid candidates, Simpilot displays a selection window with icons, paths, file versions, and modification times. Candidates are ordered by:

1. highest file version;
2. newest modification time when versions match; and
3. case-insensitive full-path order when both still match.

The selected result is saved in `Cache/program-cache.tsv`. It expires automatically if the target is moved or removed. To change a valid cached choice immediately, use **Choose Again...** beside the resolved path in the quick-launch menu editor.

## 9. Menu icons

### 9.1 Automatic icons

Simpilot automatically displays icons for supported applications, files, websites, and categories. Icons for local targets are cached in `Cache/RunIcon/`. If a program does not contain a native 128×128 image, Simpilot scales the best available icon.

An icon extraction failure does not prevent the launch item from working; the item falls back to text without an icon.

### 9.2 Manually selected icons

Open **Settings > Menu Icons**:

1. Select a local launch item.
2. Select **Choose Icon...**.
3. Choose an ICO file or select an icon from an EXE or DLL.
4. Select **Apply** or **Save**.

**Restore Automatic Icon** removes the selected custom icon. Custom icons are identified by the complete launch action, so the same executable with different arguments can use different icons.

Websites, categories, and separators do not appear in the custom-icon list. Websites and categories use automatic icons; separators have no icon.

Deleting the entire `Cache/RunIcon/` directory also deletes manually selected icons. Do not treat it as disposable cache if those customizations need to be retained.

## 10. Configuration, cache, and logs

Simpilot creates these files as needed:

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

| File or directory | Purpose |
|---|---|
| `Config/Simpilot.ini` | Main quick-launch menu |
| `Config/Simpilot2.ini` | Optional second menu |
| `Config/Setting.ini` | Interface language, general settings, hotkeys, and Windows hotkey blocking |
| `Cache/program-cache.tsv` | Confirmed resolutions for programs configured without a path |
| `Cache/RunIcon/` | Automatic and manually selected menu icons |
| `Log/Simpilot.log` | Startup, menu, Everything, hotkey, and error diagnostics |

Menu configuration uses UTF-8. Most users should edit it through Settings; direct edits should be made with a UTF-8-capable text editor.

`program-cache.tsv` can be deleted after Simpilot exits; unresolved programs are searched again when needed. Simpilot keeps one `Simpilot.log` file and removes entries older than 90 days during startup.

For migration or backup, exit Simpilot and copy the entire application directory. At minimum, retain `Config/`. If manually selected icons are used, also retain `Cache/RunIcon/`.

## 11. Troubleshooting

### 11.1 No window appears after launch

This is normally expected because Simpilot starts in the notification area. Check the taskbar and hidden-icons area.

If no tray icon appears:

1. Confirm that the complete archive was extracted.
2. Confirm that the program directory is writable and `Everything/` remains present.
3. If Windows Explorer has just restarted, wait briefly for the icon to return.
4. Run `Simpilot.exe` again.
5. Check `Log/Simpilot.log`.

### 11.2 An item is missing from the quick-launch menu

Simpilot hides launch targets that do not exist or cannot be resolved. Check:

1. whether the target path exists;
2. whether a relative path is correct relative to the configuration directory;
3. whether a program name can be found in Windows system directories, `PATH`, or Everything;
4. whether the Everything database is available; and
5. whether `Simpilot.ini` is valid UTF-8 configuration.

After correcting the entry, wait for automatic reload or select **Maintenance > Reload Menu**. If reloading fails, the last valid menu remains active and the error is written to the log.

### 11.3 A global hotkey does not respond

1. Confirm that the hotkey is recorded and its switch is enabled.
2. Select **Apply** or **Save**.
3. Ensure no hotkey recorder is active.
4. Check for a conflict with another application.
5. Try a combination that is not protected by Windows.
6. Check `Log/Simpilot.log` for registration errors.

### 11.4 A Windows shortcut still runs

1. Confirm that the matching switch is enabled and applied.
2. Confirm that you pressed the exact `Win+letter` combination without Ctrl, Alt, or Shift.
3. Confirm that Simpilot is still running.
4. Remember that `Win+L` and secure-desktop combinations are unsupported.
5. The sign-in screen, other user sessions, and some remote sessions are outside the guaranteed scope.

This feature does not require an Explorer restart or a Windows policy change.

### 11.5 Everything Search does not open

1. Confirm that `Everything/Everything.exe` and `Everything/Everything64.dll` exist.
2. Select **Maintenance > Open Everything** from the tray menu.
3. If needed, select **Install/Repair Everything Service...** and approve UAC.
4. If using a separate Everything installation, confirm that the default instance is running.
5. Review the Everything status and errors in `Log/Simpilot.log`.

### 11.6 A menu icon does not update

Open the quick-launch menu again. If the icon is still stale, exit Simpilot, remove only the affected automatic icon cache, and restart. Do not delete all of `Cache/RunIcon/` unless losing manually selected icons is acceptable.

### 11.7 Settings or menus cannot be saved

1. Confirm that the program directory and `Config/` are writable.
2. Check whether another program is modifying the configuration.
3. Correct any invalid name, empty target, or malformed website reported by the editor.
4. Review `Log/Simpilot.log`.

## 12. Frequently asked questions

### Is Simpilot installed or portable?

It is portable. Extract the complete archive and run it; configuration, cache, and logs remain in the application directory.

### Why does only a tray icon appear?

The tray and global hotkeys are Simpilot's primary entry points. Left-click opens the quick-launch menu; right-click opens settings and maintenance.

### What is the difference between a menu access key and a global hotkey?

A menu access key works only after a quick-launch menu is open. A global hotkey can trigger an action from another standard desktop application while Simpilot is running.

### Does Simpilot modify Windows shortcut policy?

No. Windows hotkey blocking intercepts selected combinations in real time only while Simpilot is running. Enabling startup writes a current-user startup entry, and manually repairing the Everything service requests administrator privileges for that service operation.

### Can `Win+L` be blocked or reassigned?

No. `Win+L` is a Windows security shortcut and is not offered for blocking or global-hotkey takeover.

### If `Win+Z` is blocked, does `Shift+Win+Z` still work?

Yes. Manual blocking matches only the exact `Win+Z` combination. Combinations that also include Ctrl, Alt, or Shift remain available to Windows or applications.

### What happens if my own Everything installation is already running?

If its default-instance database is available, Simpilot reuses it and prefers its existing client when opening the search window. Everything does not need to be installed in the Simpilot directory.

### Can Simpilot run without Everything?

Yes. Targets with explicit paths, Windows system programs, and programs found through `PATH` continue to work. Everything Search and database-dependent discovery of pathless programs are unavailable.

### Why does a program selection window appear?

Everything found more than one file with the requested name, so Simpilot cannot determine the intended path automatically. The selection is cached and can later be changed with **Choose Again...** in the menu editor.

### Can I manually assign an icon to a website or category?

Not currently. Manual icons apply to launch items that resolve to local files. Websites and categories use automatic icons.

### Can a custom global hotkey open a website directly?

Not currently. Custom global hotkeys can open applications, folders, and files. Websites can be added to a quick-launch menu.

### How do I preserve settings during an update?

Exit Simpilot and back up the original directory. Retain `Config/`, and retain `Cache/RunIcon/` when manually selected icons are used. Keep the new `Simpilot.exe` and bundled `Everything/` components together. Also retain `Language.lng` if an additional language is installed.

### Can cache and log files be deleted?

After Simpilot exits, `Cache/program-cache.tsv` and `Log/Simpilot.log` can be deleted and will be recreated when needed. Deleting `Cache/RunIcon/` rebuilds automatic icons but also removes manually selected icons.

## 13. Uninstall Simpilot

Simpilot has no Control Panel uninstall entry.

1. Open **Settings > General**, disable startup after Windows sign-in, and select **Apply**.
2. If the bundled Everything service was installed through Simpilot, uninstall it from an administrator PowerShell or Command Prompt opened in the Simpilot directory:

   ```powershell
   .\Everything\Everything.exe -uninstall-service
   ```

   Do not perform this step for a separate Everything service that you want to keep.
3. Right-click the tray icon and select **Exit**.
4. Delete the Simpilot directory.

Deleting the directory also removes personal configuration, manually selected icons, cache data, and logs. Back up anything that should be retained first.

## 14. Collect diagnostic information

Start with:

```text
Log/Simpilot.log
```

When reporting a problem, include:

- the Simpilot version shown in About;
- the Windows version;
- the action performed before the problem;
- whether it can be reproduced consistently;
- relevant screenshots; and
- log entries near the time of the problem.

Logs can contain program paths and configured targets. Remove personal directory names, user names, and sensitive file names before sharing them publicly.

Report problems and suggestions through [GitHub Issues](https://github.com/wonly211/Simpilot/issues).
