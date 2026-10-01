# Menu Icons and Themes

[简体中文](../zh-CN/菜单图标与主题) | **English**

Use **Settings > Menu Icons** to inspect quick-launch icon sources and assign an icon to an individual launch item.

## Automatic icons

Simpilot extracts system icons for applications, folders, and files, then creates transparent 128x128 ICO cache files under `Cache/RunIcon/`. Menus use this cache so they do not repeatedly access original files.

Folders use the Windows folder icon. Files and applications normally use their associated icon. Custom icons support ICO, PNG, JPG/JPEG, BMP, GIF, and TIFF images, as well as icons extracted from EXE or DLL files.

## Assign a custom icon

1. Select the relevant menu entry.
2. Select **Choose Icon**.
3. Select a supported image, or select an EXE or DLL and then choose an icon index.
4. Select **Apply** or **Save**.

Custom icons are indexed by the complete menu label, including its access key. Different menu labels for the same program can have separate icons; changing its path or arguments does not change that identity. Select **Restore Automatic Icon** to remove the current custom icon.

Double-clicking a menu entry also opens the picker. Cancelling or closing it leaves the source and pending changes untouched. Imported images retain their transparency and aspect ratio and are converted to 128x128 ICO files. Read or save failures preserve the current icon.

Deleting `Cache/RunIcon/` rebuilds automatic caches but also removes custom icons. Preserve this folder when you need to back up custom icon choices.

## Menu theme

The quick-launch menu and tray right-click menu can use **Follow Windows**, **Light**, or **Dark**. Settings and editor windows always use their own light interface and are not affected by this menu-theme selection.

For menu structure and launch entries, see [Quick Launch Menu](Quick-Launch-Menu). For cache handling and backup, see [Configuration, Logs, and Backup](Configuration-Logs-and-Backup).
