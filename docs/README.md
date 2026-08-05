# Simpilot Documentation

[简体中文](zh-CN/文档中心.md) | [English](en-US/README.md)

The repository documentation is organized by language first. Each language directory keeps its user-facing facts together, while `development/` contains implementation and design records for that language. This avoids mixing translated documents in one directory and makes an incomplete translation visible instead of silently presenting the wrong language.

```text
docs/
  zh-CN/
    文档中心.md
    用户手册.md
    development/
  en-US/
    README.md
    development/
  wiki/
    zh-CN/
    en-US/
    _Sidebar.md
```

The GitHub Wiki source follows the same language-first layout. The Wiki publication step flattens those pages because GitHub Wiki does not support subdirectories in page URLs.

`docs/wiki/` is the source of truth for user-facing Wiki pages. Use `tools/publish-wiki.ps1` to generate the flat working tree used by the separate GitHub Wiki repository; do not edit the generated Wiki copy directly and then expect the repository documentation to stay synchronized.
