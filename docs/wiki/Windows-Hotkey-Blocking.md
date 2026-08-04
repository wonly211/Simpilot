# Windows Hotkey Blocking

[简体中文](Windows-快捷键屏蔽) | [繁體中文](Windows-快速鍵封鎖) | **English**

Use **Settings > Windows Hotkey Blocking** to select supported `Win+letter` combinations that Simpilot should intercept in real time.

## How it works

- Blocking is active only while Simpilot is running. Windows behavior returns immediately when Simpilot exits.
- Simpilot intercepts keyboard events; it does not modify Windows policy, the registry, or Windows features.
- Restarting Windows Explorer is not required.
- Each supported combination has its own switch. Select **Apply** or **Save** after changing switches.

Blocking is exact. For example, blocking `Win+Z` does not affect `Shift+Win+Z`, `Ctrl+Win+Z`, or `Alt+Win+Z`. Both the left and right Windows keys are supported.

## Link with global hotkeys

When an enabled built-in or custom hotkey uses an exact `Win+letter` combination, Simpilot automatically blocks the matching Windows shortcut and gives the Simpilot action priority. The Settings page identifies the switch as being enabled by a global hotkey.

If the last global hotkey using that combination is disabled, cleared, or deleted, the automatic link is removed. A blocking state that you had selected independently remains in place.

See [Global Hotkeys](Global-Hotkeys) for recording and managing global hotkeys.

## Limits

- `Win+L` is the Windows security lock shortcut. Simpilot does not provide blocking or global-hotkey assignment for it.
- Security-desktop combinations such as `Ctrl+Alt+Del` cannot be intercepted by ordinary desktop applications.
- The sign-in screen, other user sessions, elevated desktops, and some Remote Desktop environments are outside the supported scope.

For common checks when a shortcut still reaches Windows, see [FAQ and Troubleshooting](FAQ-and-Troubleshooting).
