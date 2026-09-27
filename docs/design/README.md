# Simpilot 视觉审计与设计基线

原生采集：2026-09-26；设计定稿与复核：2026-09-27 · 现代原生工具 · 浅色优先  
**设计交付，不是 UI 实现或版本发布。**

以上是历史审计的交付边界。后续正式实施及验证见 [v1.0.1 实现记录](implementation-v1.0.1/README.md)，两批截图与结论不混用。

## 从这里看

1. [四组前后对照](COMPARE.html) — 常规、全局热键、映射编辑、快捷启动菜单
2. [全部原生截图画廊](GALLERY.html) — 点击全尺寸；每张有唯一编号
3. [截图索引](SCREENSHOT-INDEX.md) — 37个编号，39份原图；N14退役，由N29替代
4. [逐页审计与覆盖缺口](AUDIT.md)
5. [统一视觉基线](VISUAL-BASELINE.md) — 选定的DIP、颜色、排版、状态、布局与职责
6. [可运行离线提案](proposals/gallery.html) — 四组提案及中英文、窄窗、栅格倍率矩阵
7. [视觉终审](reviews/visual-final.md) / [交互终审](reviews/interaction-final.md)

## 已交付

- 八类设置页、编辑器数据状态、新增/带值弹窗、程序选择、关于、菜单、提交反馈的原生截图。
- 两个独立设计角色逐批收到明确图片附件，分别记录可见证据、审美取舍和交互推断。
- 四类统一布局：简单设置、密集列表/编辑器、模态、弹出菜单。
- 四组效果图、第一轮追溯、第二轮语义修订与离线渲染检查；不是正式应用截图。
- 独立 Win32 夹具，使用临时配置与模拟数据，禁止外部执行，不启动键盘钩子、不注册全局热键、不安装服务。
- 173个生产/构建/测试文件前后SHA一致；既存用户修改与Logo文件未动。当前工作区Release增量构建成功，14项采集链接输入SHA未变化。

## 第二轮结论

视觉与交互两个角色均已实际查看四张中文修订稿及热键、映射的英文窄窗稿，结论为**所见静态方案通过，无新增必修图稿项**。单规则启用、适用应用与精确匹配成组、登录启动措辞、菜单2入口遗漏四项已在设计稿层面关闭。完整记录保留首轮问题及第二轮关闭边界。

2026-09-27 重跑离线矩阵：42/42场景通过；7张截图总览和前后对照中的图片均可加载。它们不代替原生DPI、真实录制、保存或菜单操作验收。

## 明确未完成的原生覆盖

实际100%/200% DPI和最小窗口、真实录制与冲突、光标定位动画、生产托盘宿主、主菜单Tools深层展开/屏幕边缘、系统文件选择器与外部Everything、高对比度等尚未全部采集或验证。原生实测DPI为144（150%），首批DPI未记录。

因此本次是**有明确缺口的设计审计交付**，不是“全界面、全状态已验收”。全部缺口在审计报告列明；浏览器1/1.5/2倍率不代替原生验证。无修改正式UI，无提交、推送、发布。

## 目录

```text
docs/design/
  README.md                 交付入口
  AUDIT.md                  逐页问题、操作观察、边界与缺口
  VISUAL-BASELINE.md         选定设计规范与后续实现映射
  SCREENSHOT-INDEX.md        原图索引
  GALLERY.html              原生截图总览
  COMPARE.html              原生 / 提案对照
  evidence/native/          原始JPEG、辅助UIA文本、SHA索引
  evidence/metadata/        原生窗口尺寸和DPI记录
  evidence/contact-sheets/  总览（非替代原图）
  reviews/                  两设计角色审计与最终复核
  proposals/                离线HTML/CSS、PNG、矩阵及首稿
  fixture/                  与产品target隔离的真实原生夹具
  tools/                    构建、证据归档及验证脚本
```

## 复现与检查

详见 [fixture/README](fixture/README)。先在本地构建工作区Release输入，再独立配置夹具：

```powershell
cmake -S docs/design/fixture -B build/design-audit -G "Visual Studio 17 2022" -A x64
./docs/design/tools/build-fixture.ps1
```

构建脚本固定本机已安装SDK以避免受保护用户SDK路径探测，且只对子进程消除PATH/Path重复；不修改系统环境。如果本机SDK不同，按本机安装路径修改此审计脚本。

非UI自检必须等待进程并检查真实退出码，不能使用PowerShell启动GUI程序后立即返回的shell码代替：

```powershell
$p = Start-Process -FilePath (Resolve-Path build/design-audit/Release/audit_preview.exe) `
  -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
$p.ExitCode
```

最终自检退出0，凭据见 [self-test-results.ini](evidence/self-test-results.ini)。0/3/16/40数据、编解码、注册与生命周期、模拟成功/失败回滚、两个Shell执行边界检查通过；不包含截图或交互美感验收。

本轮自检修复了夹具40条样本的映射前缀冲突和Shell延迟库名预加载探测；未修改生产验证器/执行器。最终夹具较截图采集版本仅多这两个非视觉修正，已有16条样本与界面未改变。

```powershell
node docs/design/proposals/render.cjs
./docs/design/tools/provenance.ps1 -Output ./docs/design/evidence/source-after.json
node docs/design/tools/assemble.cjs
node docs/design/tools/render-overviews.cjs
```

[verification.json](evidence/verification.json) 给出生产SHA、HEAD、原图SHA及链接输入核对。`git diff --check`退出0；仅报告原有文件的LF/CRLF提示。没有将本轮构建或夹具自检冒称完整CTest/原生UI回归。
