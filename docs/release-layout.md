# 发布目录与升级规则

适用版本：0.18.0 及以后。

```text
Simpilot.exe
Everything/
  Everything.exe
  Everything64.dll
  Everything.lng
  Everything.ini
LICENSE
THIRD-PARTY-NOTICES.txt
```

`Simpilot.exe` 内置简体中文、繁体中文和英文界面资源。发布包不包含 `Languages/` 目录，也不包含三种内置语言的 JSON 源文件。

可选的额外语言包放在 `Simpilot.exe` 同目录：

```text
Language.lng
```

`Language.lng` 缺失、损坏或不兼容时会被忽略，不影响简驭启动或内置语言使用。

首次运行后，简驭会按需创建以下本地数据：

```text
Config/
Cache/
Log/
```

升级时请完整替换程序文件和 `Everything/` 目录，同时保留 `Config/`；使用人工图标时保留 `Cache/RunIcon/`。`Cache/` 其余内容可删除后自动重建，`Log/Simpilot.log` 仅保留最近 90 天记录。

旧配置不提供自动迁移。旧版本用户如需保留配置，应在升级前自行备份并按当前文件名整理内容。
