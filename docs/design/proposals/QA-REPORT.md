# 离线提案渲染复核

修订日期：2026-09-26；重跑及复核日期：2026-09-27。轮次：第二轮修订。状态：全矩阵重跑 42/42 通过。两设计角色已明确复核四张中文默认稿及热键、映射英文窄窗稿，在所见静态范围内通过；批准依据见 `../reviews/visual-final.md` 和 `../reviews/interaction-final.md`，不以本 QA 代替。不是产品实现验收。

## 本轮范围

只修改 `docs/design/proposals/`。已阅读 `../reviews/interaction-final.md`，实际查看 `../evidence/native/N24.jpg` 和 `N37.jpg`，确认主菜单的自动附加入口。不修改审计报告、原生证据、fixture 或产品代码，不扩展其他设计。

| 项目 | 修订结果 | 保留边界 |
| --- | --- | --- |
| IF-01 | “启用此规则 / Enable this rule” | 仍为 on，仅表达本条规则，不改页面总开关 |
| IF-02 | “适用应用 / Applies to”；精确匹配与应用输入、前台应用按钮归入同一具名字段组 | 精确匹配 off；checkbox 左缘与输入框 x=132 对齐，位于输入下方；规则启用独立于该组；不改自动跟随或取值行为 |
| IF-03 | “登录 Windows 后自动启动 Simpilot”；英文 “Automatically start Simpilot after signing in to Windows” | 仍为 off，原设置行宽与高度不变，不修改系统启动项 |
| P04 遗漏 | Tools 后补回分隔及“菜单 2 / Menu 2”，保留文件夹图标与子菜单箭头 | 与 N24/N37 可见项目一致；仍只展开 Tools 一级，菜单 2 保持折叠 |
| 数值归因 | 当前 README 更正为“审计团队选定” | 归档旧文档不改写；历史错误在归档说明中明确指出 |

以上是离线提案修改记录。2026-09-27 两角色已据实际附件复核：IF-01/02/03 及 Menu2 入口遗漏在可见静态方案层面关闭，实际原生行为仍待验证。

## 交付

| 视图 | 默认文件 | CSS 画布 / PNG 像素 |
| --- | --- | --- |
| General | `renders/general.png` | 1028 × 714 |
| Hotkeys | `renders/hotkeys.png` | 1028 × 714 |
| Mapping | `renders/mapping.png` | 963 × 651 |
| Menu | `renders/menu.png` | 868 × 752 |

外部复核入口：`gallery.html`。设计依据、语义保留及脚本用法：`README.md`。完整自动检查：`qa.json`。生成脚本：`render.cjs`。

## 首稿追溯

覆盖前将四张首稿逐字节复制到 `round1/general.png`、`round1/hotkeys.png`、`round1/mapping.png`、`round1/menu.png`，同时保留当时 QA 和说明。全部归档图片 SHA-256 与首轮 `qa.json` 对应条目一致，详见 `round1/ARCHIVE.md`。

Hotkeys 不改设计；全矩阵重渲染后的默认 `renders/hotkeys.png` 与归档首稿 SHA-256 相同。未将全部场景的 PNG 字节一致性作为通过标准。

## 自动结果

- 第二轮全量重跑：42/42 场景通过，0 失败；另附 `hotkeys-last-rows.png`，当前 `renders/` 共 43 张 PNG，另有四张首稿归档。
- 浏览器：本机 Chrome 153.0.8010.53，无头模式；Playwright 显式模块加载。
- 默认画布及窄窗画布均覆盖中文、英文和 deviceScaleFactor 1 / 1.5 / 2。
- 窄窗：General / Hotkeys 960 × 714；Mapping 900 × 640。
- 检查通过：标题栏、导航、内容边距、底栏、按钮、固定热键列、表头及数据行尺寸；无意外文本溢出、无文档横向溢出、控件不进入底栏。
- 16 条长路径逐条匹配 N15；首 5 行可见；末条可聚焦滚入视口；focus 提示保留完整路径。
- 映射双侧各 4 修饰键、源主键与同时按下键、目标主键、录制、用途、适用应用、前台应用、两 checkbox 均保留，最小画布不滚动且用途文字完整可见。
- 新增检查：IF-01 单条规则文案；IF-02 适用应用标签、字段组归属、精确匹配与输入 x 对齐、相邻及不与规则启用重叠；IF-03 登录时点文案与辅助名称。
- 菜单保持 fixture 层级，仅展开一级；中英文 UI 下用户自定义双语名称不变，自动附加入口按语言显示“菜单 2 / Menu 2”。
- 新增检查：两条 9px 分隔；末尾菜单 2 紧随自动附加分隔，具备子菜单属性与箭头且保持折叠。
- 页面无脚本错误、无外部网络请求，图片和本地 Lucide 图标加载成功。

本轮首次运行发现英文 “Applies to app” 在原 88px 标签列换行，影响选项间距和最小窗口高度；改用含义一致的紧凑标签 “Applies to”，不扩大列宽，随后全量重跑通过。首轮渲染过程记录保留在 `round1/QA-REPORT.md`。

## 人工看图

本轮实际查看：N24/N37 原生证据；中文 General / Mapping / Menu 修订图；最终英文 960px General、900 × 640 Mapping、868px Menu；默认 Hotkeys 与归档首稿。核对登录措辞、规则单项范围、适用应用关联、完整字段、最小窗口底栏及恢复的菜单 2 入口。没有逐张人工审阅全部 42 个场景；其余由自动矩阵覆盖。

## 限制

**不是原生 DPI 验证。** 浏览器 deviceScaleFactor 不等于 Windows 原生 DPI 测试。未验证原生字体渲染、高对比度、辅助技术、真实键盘录制、前台进程查找、提交或持久化、原生菜单交互。

没有改动产品目录、审计报告、证据或 fixture。本次文件编辑和渲染输出仅位于 `docs/design/proposals/`；已有工作区修改保留。
