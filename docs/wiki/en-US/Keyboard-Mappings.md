# Keyboard Mappings

[简体中文](../zh-CN/键盘映射) | **English**

Open **Settings > Keyboard Mappings** to translate physical key combinations into new key input. The mapping list's second column shows each rule's optional purpose label.
Mappings are active only while Simpilot runs and do not modify Windows system policy.

## Supported rules

- Source: one key, one to four modifiers plus an action key, or up to three modifiers plus two action keys held at the same time.
- Target: one key, or up to four modifiers plus one action key.
- Left and right Ctrl, Alt, Shift, and Win are recorded separately.
- A sided modifier can also be a source or target primary key by itself, without additional modifiers on that side. A modifier source primary key also cannot have a chord.
- Choose **All applications** for a global rule, or **Specific applications** to associate up to 32 `.exe` basenames. The rule applies when any listed app is in the foreground.
- With **Exact application match** disabled, a case-insensitive process-name prefix is used.
- At most 128 rules can be stored.

## Add a mapping

1. Select **Add**.
2. Select **Record Source**, press the source key or combination, then release every key. You can instead select up to four physical modifiers, a primary key, and an optional simultaneous key from the lists.
3. Select **Record Target**, press the target key or shortcut, then release every key. You can instead select up to four physical modifiers and one primary key.
4. Choose **All applications** or **Specific applications**. For specific apps, enter each name and select **Add**, browse for an EXE, or use **Add foreground app** (the latest external foreground process observed by Simpilot). Duplicate names are merged and individual entries can be removed.
5. Save the rule, then select **Apply** or **Save** in Settings.

For example, associate **Left Win → Right Ctrl** with `Code.exe` and `notepad.exe` to enable it when either program is in the foreground. It does not activate in other programs. An already-held target is still released when its source key is released after switching apps.
An empty specific-app list cannot be saved; removing its last entry does not make it global. Matching always uses the executable basename, including when browsing, and does not distinguish different paths, windows, or web pages. Switchable work/game profiles are a separate feature and are not included.

Both source and target primary-key lists include sided Ctrl, Alt, Shift, and Win keys; chord
actions still exclude modifiers. The action-key lists include letters, digits, `F1` through `F24`,
editing and navigation keys, numpad keys, common punctuation, browser keys, and media keys. Modifier choices always identify the left or
right physical key. A recorded hardware key outside the standard list retains its exact identity.

`Num Lock` can be recorded or selected from the primary-key list as either a source or a target.
For example, mapping `F12` to `Num Lock` toggles Num Lock each time you press and release `F12`.
Existing target mappings recorded as `VK 0x90` work without being recorded again.

For **Left Win → Right Ctrl**, leave all source and target modifier slots set to **None** and select **Left Win** and **Right Ctrl** as the primary keys. Alternatively, record each key by pressing and releasing it.
After saving and applying, pressing Left Win immediately holds Right Ctrl, and releasing Left Win releases Right Ctrl. Holding Left Win and pressing `C` produces `Ctrl+C`, without a 250 ms wait. A companion key may also be pressed first.
Ordinary keys can map to modifiers too, such as `Caps Lock → Left Ctrl`. If the real target key or another mapping also holds the target modifier, it stays held until the last owner releases it.

For example, an ordinary keyboard can map **Right Ctrl** to **Left Win + Left Shift + F23** to emit
the Copilot key sequence. Right Ctrl alone triggers the mapping. Pressing `C`, `V`, or another key
within 250 ms replays Right Ctrl first, preserving normal shortcuts such as `Ctrl+C`. `F23` is the
Windows `VK_F23` value (`0x86`) and is displayed directly instead of the decimal fallback `VK134`.
See Microsoft's [virtual-key code table](https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes)
and its [Copilot key combination explanation](https://learn.microsoft.com/answers/questions/5637848/remap-a-shortcut-for-copilot-key-on-asus-zenbook-w).

Both action keys in a source chord must be down at the same time; either action may be pressed first.
Bare `Esc` is recordable in the mapping editor. Use the dialog's **Cancel** command to abandon an edit.

## Validation and limits

Saving rejects duplicate sources, ambiguous prefixes, cycles, `Win+L`, Windows secure combinations,
and invalid keys. Target chords are not supported. When an older configuration has no
`[KeyboardMappings]` section, all other settings and existing hotkey behavior remain unchanged.
A modifier mapped to another modifier also conflicts with source shortcuts containing the same source modifier. Saving rejects these rules to avoid conflicting immediate-remap and prefix-wait behavior.

Multi-app conflicts are checked in their shared applications, and the editor stays open when a conflict is found. App-specific rules beat global rules, exact matching beats prefix matching, and the longest matching prefix wins. List order and size do not affect priority.

Existing global and single-app settings load directly. v1.0.10 saves multi-app rules with a separate list and a legacy-reader guard. v1.0.9 and earlier skip those rules instead of treating them as global. Back up `Config/` before downgrading: saving settings in an older version may discard multi-app rules.

Windows UIPI, elevation differences, or the secure desktop may block input injection. A normally
running Simpilot process cannot guarantee delivery to a higher-privilege window. Failures are
reported in `Log/Simpilot.log`.

## Related pages

- [Global Hotkeys](Global-Hotkeys)
- [Windows Hotkey Blocking](Windows-Hotkey-Blocking)
- [Configuration, Logs, and Backup](Configuration-Logs-and-Backup)
