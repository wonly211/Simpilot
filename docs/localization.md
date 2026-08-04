# 多语言资源设计

## 支持范围

简驭 | Simpilot 内建三种完整界面语言：

- `zh-CN`：简体中文，首次运行和无效配置时的默认语言；
- `zh-TW`：繁体中文，采用台湾常用软件术语；
- `en-US`：英文，同时作为缺少资源时的兜底语言。

品牌名称始终写作“简驭 | Simpilot”，繁体中文界面不会改写品牌中的“简驭”。用户在快捷启动菜单中创建的名称和分类属于用户数据，不会自动翻译。

## 资源目录

语言文件随 `Simpilot.exe` 一起发布：

```text
Languages/
  en-US.json
  zh-CN.json
  zh-TW.json
```

每个文件使用 UTF-8 JSON：

```json
{
  "locale": "zh-CN",
  "strings": {
    "ui.settings": "设置...",
    "settings.display_language": "显示语言"
  }
}
```

`locale` 必须与文件名一致，`strings` 中的键和值均不得为空。三种内建语言必须具有完全相同的键集合，带 `{}` 的动态文本还必须在各语言中保留相同数量的格式占位符。自动化测试会验证这些约束。

## 选择与切换

用户可通过托盘右键菜单的“语言”，或“设置 > 常规 > 外观 > 显示语言”切换语言。语言名称始终自显为：

```text
简体中文
繁體中文
English
```

切换立即把 `Language=zh-CN`、`Language=zh-TW` 或 `Language=en-US` 写入 `Config/Setting.ini`，并刷新当前设置窗口，不需要重启 Simpilot 或 Windows 资源管理器。当前设置页面、未应用的设置和列表选择保持不变。之后打开的模态窗口使用新语言。

程序不提供旧配置迁移：更新旧版本时，用户需要手动把 `Config/Simpilot.settings.ini` 改名为 `Config/Setting.ini`，并将原 `language.txt` 中的语言代码写入 `[General]` 下的 `Language=` 项。旧文件不会被读取。

## 回退规则

读取文本时使用以下顺序：

```text
当前语言的文本
→ en-US 中的同名文本
→ [missing translation]
```

选中的语言文件缺失、JSON 无效或 UTF-8 解码失败时，整个文件视为空资源并回退英文。英文文件异常时仍显示 `[missing translation]`，避免按钮或提示无文字。

## 新增语言

新增语言时应：

1. 以 `en-US.json` 为键集合基准创建新的 UTF-8 JSON 文件；
2. 翻译全部文本，并保留动态格式占位符；
3. 在语言注册列表与发布资源列表中加入语言代码；
4. 为语言的自显名称补充产品定义；
5. 运行完整构建和测试，确认 JSON 键集合、安装目录和发布包。

界面不会使用国旗表示语言。翻译不得通过缩小字体规避布局问题；导航和控件应根据本地化文字长度自适应。
