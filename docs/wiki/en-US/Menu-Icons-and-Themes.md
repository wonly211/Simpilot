# Menu Icons and Themes

[简体中文](../zh-CN/菜单图标与主题) | [繁體中文](../zh-TW/選單圖示與佈景主題) | **English**

Use **Settings > Menu Icons** to inspect quick-launch icon sources and assign an icon to an individual launch item.

## Automatic icons

Simpilot extracts system icons for applications, folders, and files, then creates transparent 128x128 ICO cache files under `Cache/RunIcon/`. Menus use this cache so they do not repeatedly access original files.

Folders use the Windows folder icon. Files and applications normally use their associated icon. EXE, DLL, and ICO files can be used as sources for manually selected icons.

## Assign a custom icon

1. Select the relevant menu entry.
2. Select **Choose Icon**.
3. Select an ICO, EXE, or DLL, and choose an icon index when applicable.
4. Select **Apply** or **Save**.

The same EXE can receive distinct icons when launched with different arguments, paths, or `explorer.exe shell:::{GUID}` targets. Select **Restore Automatic Icon** to remove the custom icon for the current launch action.

Deleting `Cache/RunIcon/` rebuilds automatic caches but also removes custom icons. Preserve this folder when you need to back up custom icon choices.

## Menu theme

The quick-launch menu and tray right-click menu can use **Follow Windows**, **Light**, or **Dark**. Settings and editor windows always use their own light interface and are not affected by this menu-theme selection.

For menu structure and launch entries, see [Quick Launch Menu](Quick-Launch-Menu). For cache handling and backup, see [Configuration, Logs, and Backup](Configuration-Logs-and-Backup).
