# Simpilot 离线设计提案

日期：2026-09-26。供视觉设计、交互设计两角色复核。不是正式产品实现、原生截图或运行时验收。

## 入口

- `gallery.html`：带提案标识的外部复核页及四张默认 PNG。
- `index.html?view=general|hotkeys|mapping|menu`：独立画布，默认中文；追加 `&lang=en` 查看英文。
- `renders/general.png`、`renders/hotkeys.png`：1028 × 714，含 30px 标题栏。
- `renders/mapping.png`：963 × 651；`renders/menu.png`：868 × 752。
- `qa.json`：逐场景检查、画布与 PNG 尺寸、哈希、浏览器版本及结果。

HTML 可以直接通过 file 协议离线打开，不需要服务、不访问网络。页面随浏览器可用画布铺满；精确尺寸以渲染脚本为准。所有新增文件与生成输出均在本目录下，品牌图标只读引用仓库现有 `assets/simpilot-icon.png`。

## 渲染

在仓库根目录执行：

```powershell
node docs/design/proposals/render.cjs
```

默认使用显式模块路径：

```text
C:/Users/WoNly/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright/index.js
```

可用 `SIMPILOT_PLAYWRIGHT` 指定另一个 Playwright 入口，或用 `SIMPILOT_CHROMIUM` 指定可用 Chromium。未指定时依次探测 Playwright Chromium、本机 Chrome、Edge，以无头模式渲染。脚本不安装依赖，不启动或自动化原生产品应用，只加载本地 HTML。若运行环境没有 Playwright，HTML 和脚本仍可独立交付后手动渲染。

`icons.js` 是本地 Lucide 的生成子集，已附许可证，不需要联网。仅在需要重新生成图标时执行 `node docs/design/proposals/prepare-icons.cjs`；可通过 `SIMPILOT_NODE_MODULES` 指定本地模块根目录。

## 数据与设计取舍

已阅读 `../reviews/interaction-review.md`、`../reviews/visual-review.md`，补充只读核对 `../evidence/native/N15.txt`、`N21.txt`、`N21.jpg` 和 `../fixture/audit_preview.cpp`。

| 视图 | 对应审计项 | 提案处理 |
| --- | --- | --- |
| General | IR-01/02；N01-01/02；S01/02 | 640px 简单设置宽，64/72px 行，开机 off、简中；语言旁保留“立即保存”短状态，不增加教程 |
| Hotkeys | IR-03/04/05；N02-01/02/03 | 固定 132+12+228+8+32+12+44 列；轻量列头；录制图标与明确清除名称；32px 工具栏及表头、36px 数据行 |
| Mapping | 后续模态复核范围，N21 真实字段为依据 | 每侧 4 修饰键下拉、源主键与同时按下键、目标主键、两录制按钮、用途、应用、前台应用、两个 checkbox 全部保留；取消在保存左侧 |
| Menu | 两份报告后续实际菜单复核范围 | 280px 菜单、32px 行、16px 图标、24px 图标槽、9px 分隔、4px 圆角；仅一级展开，未宣称原生菜单问题已修复 |

颜色和字体尺寸按本次用户选定共同值，不以浏览器截图反推原生 DIP。输入边界与轻分隔使用不同 token。导航固定 216px，内容左右 28px，底栏 72px，底部按钮 88 × 36px / 间隔 8px。

内置热键依次 Alt+Space on、Ctrl+Space on、Ctrl+Alt+S on、Win+S off。自定义 16 条来自 N15：前 12 条 Ctrl+Alt+F1..F12，后 4 条增加 Shift；动作按应用、文件夹、文件循环，每第 4 条 off。保留真实长路径，在可用列宽内显示可识别文件名；hover 或 focus 浮层显示完整路径，HTML 属性及 QA 保留完整数据。默认显示首 5 条，表体可滚动，底栏不参与滚动。

映射为左 Ctrl+A → 左 Alt+F1；用途完整保留 `Long mapping purpose: review selection across multiple workspace panels / 选择切换`，32px 输入框在 900px 最小画布下仍完整可见，QA 以实际字体测量文字宽度。应用为 `audittool.exe`，精确匹配 off，启用 on。不删减源同时按下键，不给目标虚构 chord 字段。

菜单真实层级为 Audit Tool、分隔、Tools [T]；Tools 下为 Fixture folder 和 Review utilities；Review utilities 下为 Document 2、4、6 等。按照“仅显示一级子菜单”，默认仅展开 Tools，Review utilities 不再展开第二层，但完整数据保留在 `window.PROPOSAL_MENU`。用户定义的中英混合名称不随 UI 语言翻译或替换。

## 验证边界

矩阵：4 视图 × 2 语言 × 3 deviceScaleFactor；另外 960px settings 两视图与 900 × 640 mapping，各 2 语言 × 3 deviceScaleFactor，共 42 个场景。默认四张先渲染；额外输出热键末尾条目截图。

QA 自动检查尺寸、列宽、控件越界、文本溢出、完整字段与 fixture 状态、首五行、列表末尾可达、完整路径 focus 提示、图标和图片加载、菜单一级层级、无外部请求与脚本错误。程序检查不代替人工看图。

**这不是原生 DPI 验证。** deviceScaleFactor 仅改变 Chromium 栅格输出，不能证明 Win32 布局、系统缩放、字体抗锯齿、高对比度、辅助技术、真实菜单行为正确。

离线稿只提供少量浏览器内状态展示：开关、checkbox、下拉、文本编辑、行选择、路径提示、视图/语言切换。录制、保存、应用、关闭、增删改、前台应用查找及程序启动不执行，不伪造成功反馈；不写磁盘或个人配置。完整产品键集合与快捷键校验不在此稿实现。真实提交、录制 Esc 语义、焦点恢复、导航键盘模型仍待原生实现复核。

## 两角色复核重点

- 视觉：四页锚点、密度、固定列、中英文换行、长路径辨识、900px 映射字段、菜单背景与边界。
- 交互：录制与清除对象、启用列语义、完整字段清单、取消/保存顺序、菜单层级、语言即时保存与普通设置提交边界。
- 未覆盖其他设置页、空态/错误/成功状态矩阵；不能将本提案视为 IR-01 至 IR-21 全量修复。
