# 仓库同步规则 / Repository synchronization

GitHub 保存可完整构建、测试和发布的源码工程及用户文档。唯一机器规则是 [repository-sync-policy.json](../tools/repository-sync-policy.json)；[同步工具](../tools/repository-sync.ps1) 从它生成忽略规则、检查提交并导出公开文件。

允许应用/模块源码、公共接口、三语资源、测试、构建/CI/发布工具、实际应用资源、许可证、用户手册、Wiki 源页面和正式发布说明。Everything 的四个运行组件及 nlohmann 库是显式列出的依赖例外；应用图标也按文件列举。

开发资料、设计/审计/验收材料、各层 AGENTS、项目 Codex 配置/技能、备份、日志、构建产物和临时文件仅保留本地。目录里存在不代表允许提交；强制暂存也会被检查拒绝。Actions 制品与 Release 附件由工作流管理，不进入源码树。公开文档不能链接本地资料。

## 本地使用

需要 Git、PowerShell 7 和 Python 3.10+（只使用标准库，不联网安装包）。在仓库根目录执行：

```powershell
# 修改 JSON 后重新生成；不要手改 .gitignore
./tools/repository-sync.ps1 -Mode GenerateIgnore
./tools/repository-sync.ps1 -Mode Worktree

# 每个克隆安装一次；保留已有自定义 hooks，不自动覆盖
./tools/repository-sync.ps1 -Mode InstallHooks

# 检查完整 index，以及全部待推送提交（Base 必须已存在本地）
./tools/repository-sync.ps1 -Mode Staged
./tools/repository-sync.ps1 -Mode History -Base origin/main -Revision HEAD
# 新远端/新分支没有基线时检查所有可达历史
./tools/repository-sync.ps1 -Mode History -Revision HEAD

# 检查指定提交；CI 在 Prepare 使用这个范围
./tools/repository-sync.ps1 -Mode Commit -Revision HEAD

# 导出到不存在或为空的目录，并在相邻目录生成摘要清单
./tools/repository-sync.ps1 -Mode Export -Destination .local/exports/public
```

pre-commit 检查完整暂存区和暂存策略/忽略规则是否一致。pre-push 从 Git 标准输入读取每个 ref 的新旧 SHA，逐个检查新提交的完整文件树；曾加入本地资料后又删除的历史仍被拒绝。新分支没有远端基线时检查其所有祖先，绝不默认信任旧历史。Git hooks 是本地保护，CI 的提交检查是补充；不要使用 `--no-verify` 绕过。

白名单是用途和路径约束，不能自动判断某段允许路径内源码是否含敏感数据；修改仍需审阅内容。符号链接、子模块和大小写冲突拒绝导出/提交。

## 文档检查

```powershell
./tools/check-project-docs.ps1 -Scope Public
./tools/check-project-docs.ps1 -Scope All
./tests/project_documentation_tests.ps1
./tools/publish-wiki.ps1
```

Public 离线检查公开链接、图片/HTML 引用、标题锚点、双语手册版本、Wiki 文件名与平面发布结果；All 额外检查本地规则、技能、配置、归档和迁移清单。干净克隆只需要 Public，不能依赖本地专属目录。文档和同步回归测试在 CI Prepare 仅运行一次，耗时写入验证记录，脚本和策略纳入 CI 实现摘要。

Wiki 工具只生成文件，不自动提交或推送；GitHub Wiki 是独立仓库，发布时只使用生成的平面页面。新仓库需要先在 GitHub 启用并初始化 Wiki 后才能推送它。

## 全新仓库边界

新仓库从白名单导出创建新的 main 根提交，不继承旧 `.git`、分支或标签。仓库重建前保留 Git/Wiki 历史、工作区、发布说明/附件及可读取设置的本地备份，并检查可读性和摘要。原始附件只备份，不重新发布。必要检查名称保持 **Windows x64 Release**。

只有远端仓库 ID 已改变且没有任何提交/ref 时才允许推送新 main。随后安全切换本地元数据，保留本地专属文件，恢复必要设置并执行全九配置验证。后续正式标签沿用同提交验证复用和自动发布；本轮不创建产品版本或标签。
