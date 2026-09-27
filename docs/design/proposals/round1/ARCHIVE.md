# 第一轮提案归档

2026-09-26，在第二轮任何画布修改或重渲染之前，从 `../renders/` 复制四张默认 PNG。复制后 SHA-256 与源文件逐一相等，且与当时 `qa.json` 的默认四项一致。

| 图片 | SHA-256 |
| --- | --- |
| `general.png` | `d7acbc6ea63ccfdea02628e8fbe84c1e8bcd366a4532b87f0f89e407f1befe0f` |
| `hotkeys.png` | `09e0b387560a022ec4f7713a6e858e77df853eaf11b10c8c23a5dae703a2b5f2` |
| `mapping.png` | `cf0c20cbff8591bb872373338285ce0dff656a158da27b27a8bdbc647a9f4114` |
| `menu.png` | `18cb24c58fb94b5329ea9bb8bf60938662a30561a228689dc7bc60d490132635` |

一并保留当时的 `qa.json`、`QA-REPORT.md`、`README.md`。这些是历史原文，内部路径仍相对于原 proposals 根目录；没有复制首轮全部矩阵图片或 HTML 源码。历史 README 的数值归因有误，当前 `../README.md` 已更正为“审计团队选定”，不把历史措辞作为事实依据。

这些图片是含 IF-01/02/03 残余及菜单入口遗漏的首稿，不是最新方案或已批准设计。最新图片位于 `../renders/`，本归档不由渲染脚本覆盖。
