# v1.0.1 原生实现截图

以下均为隔离夹具调用真实代码的窗口截图，不是 HTML 提案。语言及模拟数据来自夹具，实际 DPI 为 144。窗口截图为采集工具输出的逻辑尺寸；设置窗口为 1031×715（包含原生非客户区）。

| 编号 | 内容 | 文件 |
| --- | --- | --- |
| V01 | 常规，中文 | [图片](native/V01-general-zh.jpg) |
| V02 | 快捷启动编辑器，中文 | [图片](native/V02-quick-launch-zh.jpg) |
| V03 | 菜单图标及长目标，中文 | [图片](native/V03-icons-zh.jpg) |
| V04 | 全局热键，中文 | [图片](native/V04-hotkeys-zh.jpg) |
| V05 | 键盘映射列表，中文 | [图片](native/V05-mapping-page-zh.jpg) |
| V06 | Everything，不可用隔离状态 | [图片](native/V06-everything-zh.jpg) |
| V07 | 光标定位设置，中文 | [图片](native/V07-mouse-zh.jpg) |
| V08 | Windows 快捷键屏蔽，中文 | [图片](native/V08-blocker-zh.jpg) |
| V09 | 映射编辑弹窗，中文，966×653 | [图片](native/V09-mapping-dialog-zh.jpg) |
| V10 | 自定义热键弹窗，中文，971×695 | [图片](native/V10-custom-dialog-zh.jpg) |
| V11 | 关于，中文，686×453 | [图片](native/V11-about-zh.jpg) |
| V12 | 程序选择，中文，888×514 | [图片](native/V12-program-picker-zh.jpg) |
| V13 | 主菜单，中文，299×119 | [图片](native/V13-main-menu-zh.jpg) |
| V14 | 主菜单选中项，未完整捕获子菜单 | [图片](native/V14-main-menu-selection-zh.jpg) |
| V15 | 常规，英文 | [图片](native/V15-general-en.jpg) |
| V16 | 全局热键，英文 | [图片](native/V16-hotkeys-en.jpg) |
| V17 | 未应用草稿，英文 | [图片](native/V17-unapplied-en.jpg) |
| V18 | 隔离应用成功，英文 | [图片](native/V18-applied-en.jpg) |
| V19 | 快捷启动编辑器，英文 | [图片](native/V19-quick-launch-en.jpg) |

部分图中蓝色鼠标光晕来自采集环境，非应用绘制。关于版本字符串刻意标记为 AUDIT FIXTURE，不能用于判断二进制版本；正式版本由 PE 资源与 CI 校验。V13/V14 的菜单外框和阴影由 Windows 控制。

本轮未完整捕获子菜单、第二菜单、真实托盘、真实录制、保存失败提示及光标定位动画；历史审计中的对应图片不冒充 v1.0.1 验收。真实 100%/200% DPI、系统高对比度及全部页面最小窗口仍需人工验证。页切换和隔离应用做过操作观察，不据静态图宣称所有交互流畅。
