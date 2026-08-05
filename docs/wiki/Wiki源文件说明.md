# Simpilot Wiki 源文件

此目录保存 GitHub Wiki 的可审阅源文件。正文按语言分目录保存：`zh-CN/` 和 `en-US/`。每次更新 Wiki 时，应同时提交这里的对应 Markdown，避免 Wiki 内容只存在于独立仓库。

除 GitHub Wiki 固定识别的 `_Sidebar.md` 和本说明文件外，页面文件名应与正文语言一致：简体中文页面使用简体中文名称，英文页面使用英文名称。简体中文目录中的 `Home.md` 是默认 Wiki 首页源文件；英文首页使用 `Home.en-US.md`。每个用户页面顶部均须提供简体中文和英文的切换链接。

GitHub Wiki 发布仓库要求页面处于根目录，因此发布时会将三个语言目录展平到 Wiki 仓库，并把源文件中的相对语言目录链接转换为扁平页面链接；源目录本身的相对链接必须保持可浏览。

## 发布流程

在仓库根目录执行：

```powershell
.\tools\publish-wiki.ps1 -Target .\build\wiki-publish
```

脚本只会重建目标目录中的根级 Markdown 文件，并保留 Git 仓库元数据。完成后，在 `build/wiki-publish` 中检查链接和差异，再提交并推送 Wiki 的 `master` 分支。`build/` 属于本地构建目录，不纳入主仓库版本控制。
