# Simpilot 0.8.0+: Custom Menu Icons

## Scope

The **Menu icons** page in Settings lets the user replace the automatically
extracted icon for an available local program or file in either quick-launch
menu. It deliberately is not an icon manager: there are no icon packs, online
sources, URL icons, PNG/SVG conversion, bulk operations, or category icons.

## Selection

- Select a menu item, then choose **Choose icon...**.
- The Windows icon picker accepts ICO files and lets the user browse EXE and
  DLL files.
- When an EXE or DLL contains more than one icon, the picker presents those
  icons and the selected index is used.
- The chosen icon is rendered to a transparent 128 x 128 ICO file before it is
  used. This keeps the quick-launch menu independent of the source file after
  selection.

## Storage And Identity

All automatic and custom icon files are stored in `Cache\\RunIcon`:

```text
Cache/
  RunIcon/
    <target-hash>.ico
    <target-hash>.custom.ico
```

The complete configured launch action is the identity: entry kind, executable or
file path, command-line arguments, and administrator state. Entries such as
different Chrome profiles, Chrome app IDs, or `explorer.exe shell:::{GUID}`
commands therefore remain independent even though they use the same EXE. Only
fully identical launch actions are combined across the main and second menus.
Custom icon files take priority over automatic cache files. Selecting
**Restore automatic icon** removes only the `.custom.ico` override; the next
menu display uses the automatic icon.

Deleting `Cache\\RunIcon` is also a full reset of custom icons and automatic
icon cache. It never changes `Simpilot.ini`, `Simpilot2.ini`, or settings.

## Runtime Behavior

Selecting or restoring an icon writes the cache immediately and clears the
active menu icon memory cache. The next quick-launch menu opened by Simpilot
uses the update; restarting Simpilot is not required. Changes made on this
page are intentionally independent of the Settings window's Save and Cancel
buttons because they are file operations, not application settings.
