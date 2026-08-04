# 多语言资源设计

## 内置语言

简驭 | Simpilot 将以下完整界面语言压缩后嵌入 `Simpilot.exe`：

- `zh-CN`：简体中文，也是首次运行和无效配置时的默认语言；
- `zh-TW`：繁体中文；
- `en-US`：英文，也是缺少翻译项时的兜底语言。

发布目录不再携带 `Languages/*.json`。因此删除、移动或损坏外部语言文件都不会影响这三种内置语言，也不会阻止程序启动。

## 可选外部语言

需要其他语言时，将一个名为 `Language.lng` 的语言包放在 `Simpilot.exe` 同目录：

```text
Simpilot.exe
Language.lng                 # 可选；不存在时直接忽略
Everything/
Config/
```

一个 `Language.lng` 可以包含一个或多个额外语言。程序会在启动时读取它，并在“设置 > 常规 > 显示语言”中列出包内语言。选择后，`Config/Setting.ini` 会保存相应的区域代码，例如 `Language=fr-FR`。

语言包缺失、格式错误、版本不兼容或解压失败时，简驭会忽略它并继续使用内置语言。外部包不能替换简体中文、繁体中文或英文的内置资源。

## 文件格式

`Language.lng` 是 Simpilot 语言包格式 1：固定二进制头加 XPRESS Huffman 压缩负载。正常打开文件时不会直接看到可编辑的翻译文本。

这是一种发布资源封装，不是加密或安全边界。持有文件的人仍可以提取内容；它的目的只是让发布目录不暴露明文 JSON，并减小资源体积。

压缩前的负载结构如下，仅供翻译与打包时使用：

```json
{
  "languages": [
    {
      "locale": "fr-FR",
      "name": "Francais",
      "strings": {
        "ui.settings": "Parametres"
      }
    }
  ]
}
```

`locale` 必须是类似 `fr-FR` 的语言区域代码，`name` 是设置页显示的语言自称。`strings` 应以 `Languages/en-US.json` 的完整键集合为基准；缺少的键会回退到内置英文。

仓库中的 `tools/language_pack_builder.cpp` 是构建期打包工具，不会放入最终发布包。先使用 Visual Studio 2022 的 C++ 桌面开发环境构建 `simpilot_language_pack_builder` 目标：

```powershell
cmake -S . -B build/vs2022-x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/vs2022-x64 --config Release --target simpilot_language_pack_builder
```

再使用其输出程序生成外部包：

```text
simpilot_language_pack_builder.exe Language.lng fr-FR.json
```

可以在一次命令中追加多个 JSON 文件，以生成包含多种额外语言的一个 `Language.lng`。

## 翻译发布检查

1. 以 `Languages/en-US.json` 为基准补齐全部键和 `{}` 格式占位符。
2. 使用 UTF-8 保存 JSON，并填写正确的 `locale` 与本地语言自称 `name`。
3. 生成 `Language.lng`，放入测试版本 `Simpilot.exe` 同目录并重启程序。
4. 检查托盘菜单、设置、菜单编辑器和对话框，确认文本没有截断、重叠或遗留英文。
5. 移走或损坏 `Language.lng`，确认简驭仍能正常启动并回退到内置语言。

## 回退规则

读取文本时使用以下顺序：

```text
当前语言的文本
-> 内置 en-US 的同名文本
-> [missing translation]
```

用户创建的快捷启动菜单标题、分类名称和路径属于用户数据，不会自动翻译。品牌名称始终写作“简驭 | Simpilot”。
