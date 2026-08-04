# Everything 集成

**简体中文** | [繁體中文](../zh-TW/Everything-整合) | [English](../en-US/Everything-Integration)

Everything 是简驭 | Simpilot 的可选搜索能力。它用于打开或恢复 Everything 搜索窗口，并在快捷启动菜单只填写程序名时协助定位文件。

简驭本身不依赖 Everything 才能启动。Everything 不可用时，托盘菜单、已有完整路径的启动项和大多数全局热键仍可正常使用；受影响的是 Everything 搜索，以及依赖 Everything 查找的无完整路径程序。

## 随包组件

请保持发布包中的目录结构不变：

```text
Simpilot.exe
Everything/
  Everything.exe
  Everything64.dll
  Everything.ini
```

- `Everything64.dll` 是简驭查询 Everything 数据库所使用的官方 SDK；它位于 `Everything/`，不会嵌入 `Simpilot.exe`。
- `Everything.exe` 是随包的默认客户端与服务修复来源。
- 简驭不会在运行时下载 Everything，也不会改动您单独安装的 Everything 程序文件。

## 启动与复用规则

启动简驭时会尝试连接默认 Everything 实例：

1. 默认实例的数据库已经可用时，直接复用它；该实例不必来自简驭目录。
2. 数据库不可用且名为 `Everything` 的 Windows 服务已安装但停止时，尝试启动该服务。
3. 数据库仍不可用时，尝试启动随包的 `Everything/Everything.exe`。
4. 上述操作失败或组件缺失时，简驭继续启动，并在需要 Everything 的操作中显示不可用提示。

“默认实例”是 Everything 未指定实例名时使用的标准实例。绝大多数用户运行的都是默认实例。若您只运行了一个命名实例，简驭目前可能无法连接其数据库，随后会尝试启动随包的默认实例。

## 打开 Everything

可通过以下任一方式打开或恢复搜索窗口：

- 托盘图标右键，选择“维护 > 打开 Everything”；
- 在“设置 > 全局热键”中启用“Everything 搜索”内置热键；默认按键为 `Win+S`，默认不启用。

已存在 Everything 搜索窗口时，简驭会优先恢复并前置该窗口。没有窗口时，简驭优先使用当前运行中的默认客户端路径；找不到时使用随包的 `Everything/Everything.exe`。因此，关闭搜索窗口后再次触发仍可以重新显示它。

## Everything 服务

Everything 服务不是运行简驭的前提。它可让 Everything 更稳定地建立和维护索引，但安装或修复服务需要管理员授权。

1. 右键单击托盘图标。
2. 选择“维护 > 安装/修复 Everything 服务...”。
3. 在 Windows 的权限确认窗口中允许操作。

简驭会使用随包 `Everything.exe` 的服务安装功能修复默认服务。取消权限确认、服务无法安装或服务无法启动都不会影响简驭的其他功能。

## 用于程序定位

当“打开应用”只填写了例如 `tool.exe` 的文件名时，简驭按以下顺序定位：Windows 常规搜索路径、`PATH`，然后是可用的 Everything 数据库。

Everything 返回多个候选项时，简驭会显示选择窗口。候选项依次按文件版本号、修改时间、完整路径排序，您可以自行选择实际使用的文件。选择结果会记录在 `Cache/program-cache.tsv`；目标移动或删除后会自动重新定位。

## 已知边界

- 仅支持默认 Everything 实例；命名实例不保证可连接。
- 只要默认实例正在运行，简驭查询时不关心该 `Everything.exe` 的安装路径。
- `Everything64.dll` 缺失时，简驭不能查询 Everything 数据库；但不影响简驭启动。
- `Ctrl+Alt+Del`、权限更高的桌面会话和其他用户会话不属于简驭或 Everything 的控制范围。

遇到无法打开或无法定位时，请先确认 `Everything/Everything.exe` 与 `Everything/Everything64.dll` 存在，再查看 [常见问题与故障排查](常见问题与故障排查)。
