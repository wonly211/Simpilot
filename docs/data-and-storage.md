# Data And Storage

## Storage Model

项目没有数据库、服务端存储或迁移框架。所有持久化均为 `Simpilot.exe` 同级目录中的文本、二进制语言包或 ICO 文件。

```text
Simpilot.exe
Config/
  Simpilot.ini
  Simpilot2.ini
  Setting.ini
Cache/
  program-cache.tsv
  RunIcon/
Log/
  Simpilot.log
Language.lng
Everything/
```

## Menu Data

`Simpilot.ini` 和可选 `Simpilot2.ini` 使用 UTF-8 文本表示分类、分隔线、应用、文件夹、文件和网址。`MenuParser` 生成树形 `MenuDocument`，`MenuWriter` 校验并规范化写回。

编辑器保存结构修改时会重新序列化，原注释和空行不属于模型，可能不被保留。两份菜单在写入前一起验证；运行时解析失败时保留上一份有效菜单。

## Application Settings

`Setting.ini` 使用 UTF-8 INI 风格键值。读取失败、损坏或缺失时返回代码默认设置；没有自动迁移旧文件名的逻辑。自定义热键数量上限为 128。

`[KeyboardMappings]` 与其他设置同样写入 `Setting.ini`，规则以
`KeyboardMapping1` 到 `KeyboardMapping128` 的索引字段保存。每条规则保存启用状态、
进程基名、精确匹配标志、源修饰键/动作键/可选第二动作键以及目标修饰键/动作键。
按键身份是 `vkCode + scanCode + extended`，修饰键以 `|` 分隔；没有第二动作键时
`SourceChord=0`。空进程名代表全局规则，非空值只保存不含路径的大小写不敏感 `.exe`
基名。源支持单键、快捷键和单级同时按住的 chord，目标不支持 chord。最多保留 128 条
规则；保存前整表验证，读取时格式错误或验证失败的映射被跳过，不影响其他设置。
`AppSettingsStore::load` 通过可选诊断回调报告具体规则编号和跳过原因；正式应用将该回调
接入 `Log/Simpilot.log`。

保存使用 `AtomicFileReplacement`：在目标同目录创建唯一临时文件，写入完成后通过 `MoveFileExW` 的替换和 write-through 标志提交。未提交的临时文件由 RAII 清理。

## Program Resolution Cache

`Cache/program-cache.tsv` 格式版本头为：

```text
# Simpilot program resolution cache v2
```

后续每行是转义后的程序名键和绝对路径，以制表符分隔。键会去除首尾空白并转换为小写。读取时只保留当前仍存在的普通文件；命中路径失效时删除条目并重新保存。

缓存是可再生数据，可以删除。删除后无完整路径程序会重新经过 Windows 路径、`PATH` 和 Everything 解析。

## Icon Cache

`Cache/RunIcon/` 保存 128x128 ICO：

- 普通 `.ico` 是从目标、Shell 类型或系统图像列表提取的可再生缓存；
- `.custom.ico` 是用户人工指定的覆盖项，迁移时应与配置一起备份；
- 文件名来自稳定哈希，不包含明文目标路径；
- 进程内还维护 HICON 缓存，退出后释放。

## Logs

`Log/Simpilot.log` 为 UTF-8 文本，每行以本地时间 `YYYY-MM-DDTHH:mm:ss.SSS` 开头。Logger 构造时删除时间戳早于 90 天的行，并通过互斥锁保护多线程追加。

无时间戳或格式不符合预期的行也会在清理时移除。日志可能包含程序名和本地路径，公开前应检查隐私信息。

## Language Pack

`Language.lng` 是可选 XPRESS Huffman 压缩包，最大读取 64 MiB。损坏、过大或键不完整时不会影响内置语言和主程序启动。

三种内置语言从 `Languages/*.json` 在构建时打包进 Windows 资源，不作为运行时外部文件保存。

## Temporary And Rollback Data

- 原子写入临时文件位于目标文件同目录，名称含进程、线程和递增标识；
- 设置窗口会在构建输出或运行目录的临时草稿/回滚目录中保存补偿材料；
- 正常完成后应清理这些材料；异常终止可能留下可删除的临时目录。

## Backup And Restore

最小用户数据备份是 `Config/`。需要保留人工图标时同时备份 `Cache/RunIcon/*.custom.ico`；日志和普通缓存可不备份。

恢复时应先退出 Simpilot，将数据复制到完整解压且版本相同或更新的发布目录，再启动并检查日志。当前没有自动 schema migration；格式兼容性由解析器的默认值和容错逻辑提供。

## Corruption Handling

| 数据 | 损坏行为 |
|---|---|
| 菜单文件 | 重载失败并保留上一份有效菜单；首次加载时功能可能不可用 |
| `Setting.ini` | 文件无法读取或整体解析失败时回退默认设置；其中无效的键盘映射规则单独跳过，其他设置继续加载 |
| 程序缓存 | 忽略无效行或清空内存缓存，可重新生成 |
| 图标缓存 | 图标加载失败时重新提取或使用回退图标 |
| 日志 | 写入/清理失败被忽略，不阻止主程序 |
| `Language.lng` | 忽略外部包，继续使用内置语言 |

具体错误通常通过 `Log/Simpilot.log` 或用户消息观察；部分 `noexcept` 存储接口只返回默认值或失败布尔值。
