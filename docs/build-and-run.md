# Build And Run

## Prerequisites

- Windows 10 或 Windows 11 x64；
- Visual Studio 2022，安装“使用 C++ 的桌面开发”工作负载；
- CMake 3.24 或更高版本；
- Windows 10/11 SDK；
- PowerShell 7 或 Windows PowerShell 可用于 CI 脚本。

项目依赖均已在仓库中或由 Windows SDK 提供，不使用包管理器，因此没有独立的 restore 步骤，也不需要联网下载运行时依赖。

## Quick Build

在仓库根目录执行：

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
```

生成目录为 `build/vs2022-x64`。该目录适合日常增量开发，但不能作为独立、可信的基线证据。

## Debug Build

```powershell
cmake --preset vs2022-x64
cmake --build build/vs2022-x64 --config Debug
ctest --test-dir build/vs2022-x64 -C Debug --output-on-failure
```

2026-09-12 的 Phase 2 初始基线（键盘映射专项测试加入前）已在本机非同步的全新构建目录验证：Debug 编译成功，7/7 测试通过，总测试时间 1.07 秒。接管实施后 CTest 已增加键盘录制和映射两个专项目标，当前总数为 9；实施后的完整结果以最新 `tools/ci.ps1` 输出为准。

在当前 SynologyDrive 工作区内，Debug 编译稳定出现 MSVC `C1090` PDB API 错误代码 `3`；同一源码和工具链在非同步目录通过，因此这是已确认的同步盘/PDB 环境问题。遇到该问题时，将构建目录放到普通本地磁盘，而不是修改源码或测试。

## Clean Baseline Build

用于工程审计时采用独立目录：

```powershell
cmake --fresh -S . -B build/takeover-vs2022-x64 `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_CONFIGURATION_TYPES="Debug;Release" `
  -DBUILD_TESTING=ON

cmake --build build/takeover-vs2022-x64 --config Release --parallel 2
ctest --test-dir build/takeover-vs2022-x64 -C Release --output-on-failure
cmake --build build/takeover-vs2022-x64 --config Release --target package --parallel 2
```

2026-09-12 的已验证工具链：

| 项目 | 值 |
|---|---|
| CMake | `3.31.6` |
| Visual Studio | 2022 Community |
| MSVC | `19.44.35228.0` |
| Windows SDK | `10.0.26100.0` |
| 架构 | x64 |
| 配置 | Debug、Release |

## Fixed CI Pipeline

构建、打包、版本或 CI 相关改动必须运行：

```powershell
.\tools\ci.ps1
```

该命令与 GitHub Actions 共用，自动删除并重建 `build/ci-vs2022-x64`，然后检查：

1. workflow runner、Action SHA 和唯一入口策略；
2. CMake 生成器、平台、配置和 Release flags；
3. `simpilot.vcxproj` 中的优化及增量链接状态；
4. Release 编译与全部测试；
5. Release ZIP、版本资源、内容白名单和 SHA-256；
6. 与正式发布基线的 EXE、ZIP 字节差异和百分比。

## Release Configuration Checks

可信 Release 配置必须包含：

```text
CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG
CMAKE_EXE_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO
Optimization=MaxSpeed
LinkIncremental=false
```

2026-09-12 的独立构建已经验证以上四项。

## Packaging

日常 preset：

```powershell
cmake --build --preset package-release
```

输出：

```text
build/vs2022-x64/Simpilot-<version>-win-x64.zip
build/vs2022-x64/Simpilot-<version>-win-x64.zip.sha256
```

ZIP 只允许包含：

```text
Simpilot.exe
Everything/Everything.exe
Everything/Everything64.dll
Everything/Everything.ini
Everything/Everything.lng
LICENSE
THIRD-PARTY-NOTICES.txt
```

2026-09-12 的独立 Release 结果：

| 产物 | 字节数 |
|---|---:|
| `Simpilot.exe` | 1,019,904 |
| 正式 `v0.18.5` ZIP 基线 | 2,474,630 |
| 本次重建 ZIP | 2,474,631 |

上表是新增键盘映射前的 Phase 2 历史基线。2026-09-13 的当前固定 CI 结果为：

| 产物 | 字节数 |
|---|---:|
| 当前 `Simpilot.exe` | 1,156,608 |
| 当前 ZIP | 2,534,502 |

当前 ZIP SHA-256 为
`C0B75B410095FF7C1D0F0C169F03206276A8E191C097C4375048EE9C2F61E2D0`。
EXE 的功能性增长由发布基线中的限额批准覆盖，具体理由和上限见
`.github/ci/release-baseline.json`。

ZIP 中的时间戳和压缩元数据可能造成不同构建之间的少量字节或哈希差异。每个 ZIP 必须与自身 `.sha256` 一致，并通过内容和体积检查；不要求重建哈希等于历史哈希。

## Run And Debug

Release 程序位于：

```powershell
.\build\vs2022-x64\Release\Simpilot.exe
```

主程序是托盘应用，没有普通主窗口。调试时从 Visual Studio 启动 `simpilot` 目标，或附加到 `Simpilot.exe`。配置、缓存和日志会相对被启动的 EXE 写入，因此开发构建不会使用源码根目录的用户配置。

完整发布冒烟应从解压后的 ZIP 运行，确认托盘图标、菜单、设置和正常退出。自动测试不能替代此步骤。

## Output Directories

| 路径 | 内容 |
|---|---|
| `build/<name>/Debug/` | Debug 程序、测试及 Everything 组件 |
| `build/<name>/Release/` | Release 程序、测试及 Everything 组件 |
| `build/<name>/_CPack_Packages/` | CPack 中间产物 |
| `build/<name>/Testing/` | CTest 日志 |
| `build/<name>/*.zip` | 发布包 |

所有 `build/` 内容均为本地产物，不应提交。

## Phase 2 Report

### Completed

完成独立 Debug、Release、Phase 2 初始基线的 7 项测试、Release 配置检查和 CPack 打包。键盘映射独立实现后新增的两个专项测试属于后续验证，不回写为初始基线证据。

### Evidence

Release 在 `build/takeover-vs2022-x64` 通过；Debug 在本机非同步独立目录通过。工具链、测试时间、版本和产物大小已记录在本文件。

### Findings

源码可在 Debug 和 Release 下构建；Release 产物与正式基线一致，ZIP 仅相差 1 字节。

### Problems

SynologyDrive 内的 Debug PDB 写入失败，非同步目录可恢复。首次受限执行还因无法访问本机 SDK 配置失败，不属于仓库缺陷。

### Unknowns

Debug PDB 错误由同步客户端、文件系统过滤驱动或安全软件中的哪一项引起，当前无法进一步确认。

### Next

依据验证结果重建测试、配置、数据、排障和项目状态文档。
