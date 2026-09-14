# Keyboard Mappings

[简体中文](../zh-CN/键盘映射) | **English**

Open **Settings > Keyboard Mappings** to translate physical key combinations into new key input. The mapping list's second column shows each rule's optional purpose label.
Mappings are active only while Simpilot runs and do not modify Windows system policy.

## Supported rules

- Source: one key, one to four modifiers plus an action key, or up to three modifiers plus two action keys held at the same time.
- Target: one key, or up to four modifiers plus one action key.
- Left and right Ctrl, Alt, Shift, and Win are recorded separately.
- A sided modifier can also be the source primary key by itself, but cannot then be combined with source modifiers or a chord.
- Leave Application empty for a global rule, or enter an `.exe` base name without a path.
- With **Exact application match** disabled, a case-insensitive process-name prefix is used.
- At most 128 rules can be stored.

## Add a mapping

1. Select **Add**.
2. Select **Record Source**, press the source key or combination, then release every key. You can instead select up to four physical modifiers, a primary key, and an optional simultaneous key from the lists.
3. Select **Record Target**, press the target key or shortcut, then release every key. You can instead select up to four physical modifiers and one primary key.
4. Optionally enter an application scope. **Use Foreground Application** uses the latest external foreground process observed by Simpilot.
5. Save the rule, then select **Apply** or **Save** in Settings.

The source primary-key list also includes sided Ctrl, Alt, Shift, and Win keys; target and chord
actions still exclude modifiers. The action-key lists include letters, digits, `F1` through `F24`,
editing and navigation keys, numpad keys, common punctuation, browser keys, and media keys. Modifier choices always identify the left or
right physical key. A recorded hardware key outside the standard list retains its exact identity.

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

Windows UIPI, elevation differences, or the secure desktop may block input injection. A normally
running Simpilot process cannot guarantee delivery to a higher-privilege window. Failures are
reported in `Log/Simpilot.log`.

## Related pages

- [Global Hotkeys](Global-Hotkeys)
- [Windows Hotkey Blocking](Windows-Hotkey-Blocking)
- [Configuration, Logs, and Backup](Configuration-Logs-and-Backup)
