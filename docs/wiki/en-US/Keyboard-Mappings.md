# Keyboard Mappings

[简体中文](../zh-CN/键盘映射) | **English**

Open **Settings > Keyboard Mappings** to translate physical key combinations into new key input.
Mappings are active only while Simpilot runs and do not modify Windows system policy.

## Supported rules

- Source: one key, one to four modifiers plus an action key, or up to three modifiers plus two action keys held at the same time.
- Target: one key, or up to four modifiers plus one action key.
- Left and right Ctrl, Alt, Shift, and Win are recorded separately.
- Leave Application empty for a global rule, or enter an `.exe` base name without a path.
- With **Exact application match** disabled, a case-insensitive process-name prefix is used.
- At most 128 rules can be stored.

## Add a mapping

1. Select **Add**.
2. Select **Record Source**, press the source key or combination, then release every key.
3. Select **Record Target**, press the target key or shortcut, then release every key.
4. Optionally enter an application scope. **Use Foreground Application** uses the latest external foreground process observed by Simpilot.
5. Save the rule, then select **Apply** or **Save** in Settings.

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
