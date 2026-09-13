# Troubleshooting

本文件面向开发和 CI 环境。最终用户问题仍以用户手册和 Wiki 故障排查为准。

## CMake 无法读取 Windows SDK 配置

### Symptoms

配置阶段从 `Microsoft.Cpp.WindowsSDK.props` 报错，提示无法访问 `%LOCALAPPDATA%\Microsoft SDKs` 或无法计算最新 SDK 版本。

### Cause

已确认的一种原因是受限执行环境禁止 MSBuild 读取当前用户的 SDK 配置目录，不是 CMake 项目错误。

### Solution

在允许访问本机 Visual Studio 和 Windows SDK 配置的终端中重新执行同一 CMake 命令。不要通过删除 SDK 检查或修改项目目标版本来绕过。

### Verification

配置输出应显示所选 Windows SDK 和 MSVC 编译器版本，并成功生成 `.vcxproj`。

## Debug 构建出现 C1090 PDB API 错误代码 3

### Symptoms

在 SynologyDrive 下构建 Debug 时，编译结束阶段报告：

```text
error C1090: PDB API 调用失败，错误代码“3”
```

错误指向构建目录中的目标 PDB，即使 `--parallel 1` 也可能复现。

### Cause

同一源码、生成器、SDK 和 Debug 配置在非同步的本地目录成功，因此问题位于同步盘、文件系统过滤器或相关本机环境。具体组件尚未确认。

### Solution

把 Debug 二进制目录放到普通本地磁盘并重新执行干净配置。保留源码目录不变即可。

### Verification

Debug 构建成功生成 `Simpilot.exe`。接管审计时的历史基线为 7 个 CTest；当前固定流水线包含 9 个测试，新增键盘录制和映射专项的结果必须以本次运行输出为准。

## 历史构建目录指向不存在的 Visual Studio

### Symptoms

CMake 或 MSBuild 从 `build/vs2022-x64` 引用已经移除的 Build Tools 路径，或目录内包含多个旧版本 ZIP、已删除测试目标和旧项目文件。

### Cause

长期复用生成目录造成缓存和产物混杂。

### Solution

日常环境可执行：

```powershell
cmake --fresh --preset vs2022-x64
```

需要审计或 CI 证据时使用全新的二进制目录，固定 CI 使用 `build/ci-vs2022-x64`。

### Verification

检查 `CMakeCache.txt` 的生成器、平台和编译器路径，并确认目录中没有与当前版本无关的包。

## Release 产物异常增大

### Symptoms

`tools/ci.ps1` 报告 EXE 或 ZIP 相比 `.github/ci/release-baseline.json` 增长超过 5%。

### Cause

可能原因包括 Release 优化缺失、增量链接、额外文件进入 ZIP、第三方组件变化或真实功能/资源增长。

### Solution

检查 CMake cache、生成的 `simpilot.vcxproj`、ZIP 白名单和第三方文件。只有具有明确依据和上限时，才能填写 `approvedGrowth`；不要提高全局阈值或删除检查。

### Verification

CI 摘要必须显示上一版及当前 EXE、ZIP 的精确字节、差值和百分比。

## ZIP 校验和不匹配

### Symptoms

`.sha256` 中的哈希或文件名与当前 ZIP 不一致。

### Cause

ZIP 在校验文件生成后被替换、修改或错误重命名。

### Solution

删除该构建目录并重新运行固定 CI。不要手工修改校验文件来匹配未知来源的包。

### Verification

`Get-FileHash -Algorithm SHA256` 的结果与 `.sha256` 完全一致。

## Everything 功能不可用

### Symptoms

Simpilot 可启动，但无法打开 Everything 或解析无完整路径程序。

### Cause

常见原因是发布目录结构不完整、`Everything64.dll` 缺失、默认实例数据库未就绪或服务权限不足。

### Solution

确认 `Everything/` 四个发布文件存在。使用完整发布包运行，并通过托盘维护命令检查默认实例或服务。取消 UAC 不应影响其他 Simpilot 功能。

### Verification

查看 `Log/Simpilot.log` 中的组件、服务和数据库状态；完整路径菜单项应继续工作。

## UI 或热键自动测试通过但真实行为异常

### Symptoms

CTest 全绿，但托盘、真实快捷键、DPI、窗口恢复或 Everything 服务行为不符合预期。

### Cause

当前自动测试覆盖纯状态、部分窗口和核心逻辑，不覆盖完整桌面集成。

### Solution

按照 `docs/testing.md` 完成人工测试矩阵，不删除或修改测试来适配错误行为。

### Verification

报告中分别列出自动测试与人工验证，不将未执行项写成通过。
