# 配置、缓存、日志与备份

简驭将本地数据保存在程序目录，便于便携使用和备份：

```text
Config/
  Simpilot.ini       # 主快捷启动菜单
  Simpilot2.ini      # 可选第二菜单
  Setting.ini        # 应用设置与语言选择
Cache/
  program-cache.tsv  # 同名程序的已选路径
  RunIcon/           # 自动与人工菜单图标缓存
Log/
  Simpilot.log       # 诊断日志
```

备份时保留 `Config/`；如使用人工图标，也保留 `Cache/RunIcon/`。其余缓存可删除，简驭会按需重新建立。

`Simpilot.log` 仅保留一个文件，程序启动时删除超过 90 天的记录。提交问题前可以查看该文件，但请勿公开其中可能包含的个人目录、文件名和命令行参数。

菜单配置变更会自动触发刷新。保存失败或配置格式无效时，简驭会保留最后一次有效菜单。
