# Global Hotkeys

[简体中文](../zh-CN/全局热键) | **English**

Global hotkeys trigger actions while Simpilot runs in the background. Open **Settings > Global Hotkeys** to record, enable, pause, edit, or remove them.

## Built-in hotkeys

The following built-in functions cannot be deleted. Each has its own displayed hotkey, record control, clear control, and enable switch.

| Built-in function | What it does | Default |
| --- | --- | --- |
| Quick Launch Menu | Opens the main quick-launch menu. | Backtick, enabled |
| Second Menu | Opens the optional second menu. | Not set, disabled |
| Settings | Opens the Simpilot Settings window. | Not set, disabled |
| Everything Search | Opens or restores the Everything Search window. | `Win+S`, disabled |

An item without a recorded hotkey, or whose switch is off, does not respond. If Everything is unavailable, Simpilot still runs normally, but the Everything Search action may be unavailable.

## Add a custom global hotkey

In the **Custom Global Hotkeys** section, select **Add** and then:

1. Choose **Open application**, **Open folder**, or **Open file**.
2. Select or enter the target path.
3. For applications, optionally set arguments, a working directory, administrator privileges, existing-process behavior, and initial window state.
4. Select the record control and press the desired key combination.
5. Turn on the item switch and select **Apply** or **Save**.

You can select an existing custom item to edit or delete it, or use its switch to pause it without deleting its configuration. Custom global hotkeys do not open websites directly; add a website to the [Quick Launch Menu](Quick-Launch-Menu) instead.

## Activation conditions for custom hotkeys

In the custom-hotkey editor, set Caps Lock, Num Lock and Scroll Lock independently to **Any state**, **On** or **Off**. The default is unrestricted. Every selected lock-key requirement must match.

Enable **Only while any of these programs is running** to associate up to 32 EXE files. Matching uses the full path, ignoring case, within the current Windows session. Background, minimized and windowless processes count; any one associated program is sufficient. Select the path again after moving a program. Newly started programs are normally detected on the next scan, about half a second later. Unknown or inaccessible process state does not activate a rule.

When both kinds of conditions are configured, both must match. An inactive rule passes the keystroke through; independently configured mappings and Windows shortcut blocking still apply. Changing conditions while a key is held does not switch actions or retrigger it. A lock key used as the trigger is evaluated using its state before the press; a matched hotkey consumes that press.

Each rule accepts an optional **Name / note**, up to 128 characters. Names may be empty or duplicated. They appear in the list, tooltip and conflict confirmation, and do not affect activation.

Alternatively, select **Only while any of these programs is in the foreground**. Foreground and running/background modes are mutually exclusive options sharing the same list of up to 32 full EXE paths. Unknown foreground identity does not activate a rule. Lock-key requirements still apply.

A single key or combination can have multiple scenarios: Num Lock on/off may open A/B, or editor/browser foreground contexts may select different actions. Foreground lists with no shared executable identity are exclusive and can reuse the key directly.

Background programs can run simultaneously, and foreground/background scenarios can overlap. To keep overlapping rules, enable **explicit same-key priority** on every overlapping rule and assign different values from 0 to 999. Only the matching rule with the highest value executes; saving requires confirmation. Equal or partly implicit priorities still require conflict resolution rather than depending on list order.

No matching scenario passes the keystroke through. Add any default action separately with no lock/program restrictions and an explicit lower priority, for example scenario 100 and default 0. The first down fixes the action until release; holds and mid-press changes never retrigger it. A matched single letter such as F will not type that character; the editor warns about this. Consider function keys or combinations.

Global backups include names, conditions and priorities. Rules using only previous conditions retain format 1; foreground conditions or priority use format 2 of the separate conditional collection. Unconditional rules retain legacy storage. v1.0.12 refuses format 2 rather than ignoring conditions and activating unsafe actions. Export a backup before downgrading and restore your pre-upgrade configuration. The action's **When the program is already running** option remains a separate launch policy.


## Record a hotkey

Select the record control, then press the desired key or key combination. The control displays the recorded result. Press `Esc` to cancel the current recording and keep the previous value.

During recording, Simpilot temporarily handles the keystrokes used for capture. After recording ends or is canceled, normal keyboard handling resumes. Recording a combination does not guarantee that Windows can register it: the final check happens when you apply or save the settings.

`Backspace` is a normal recordable key. Use the dedicated **Clear** control to remove a hotkey.

## Conflicts and Windows combinations

When settings are saved, Simpilot checks for conflicts with built-in hotkeys, other custom hotkeys, and combinations registered by Windows or other programs. It asks for confirmation before replacing an existing Simpilot assignment. Do not override a hotkey unless you understand what it currently does.

An active built-in or unconditional custom `Win+letter` hotkey is linked to [Windows Hotkey Blocking](Windows-Hotkey-Blocking) and locks the corresponding switch. A conditional hotkey intercepts the combination only while its conditions match, without permanently locking that switch. Independent manual blocking still applies even when a conditional hotkey is inactive.

## Important limits

- `Win+L` is the Windows security lock shortcut and cannot be blocked or assigned as a Simpilot hotkey.
- Security-desktop combinations such as `Ctrl+Alt+Del` cannot be controlled by ordinary desktop software.
- Blocking matches the exact `Win+letter` combination. For example, blocking `Win+Z` does not block `Shift+Win+Z`, `Ctrl+Win+Z`, or `Alt+Win+Z`.

## Related pages

- [Wiki Home](Home.en-US)
- [Quick Start](Quick-Start)
- [Quick Launch Menu](Quick-Launch-Menu)
- [Windows Hotkey Blocking](Windows-Hotkey-Blocking)
