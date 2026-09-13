# Build And Release Knowledge

## 固定环境

- Windows 10/11 x64；
- Visual Studio 2022，安装“使用 C++ 的桌面开发”；
- CMake 3.24 或更高版本；
- C++20；
- 依赖均由仓库和 Windows SDK 提供，不执行包管理器 restore。

## 日常构建

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
```

历史 `build/vs2022-x64` 可能含旧缓存和旧产物。需要可信结果时使用全新目录；CI 始终使用 `build/ci-vs2022-x64`。

## 固定 CI

本地与 GitHub Actions 的正式质量门禁均为：

```powershell
.\tools\ci.ps1
```

脚本负责清理专用目录、检查 Release 配置、编译、运行全部 9 个测试、打包、版本资源、
ZIP 白名单、SHA-256、体积基线以及已移除第三方源码残留检查。不要在 workflow YAML 或个人脚本中复制这些步骤。

## 发布相关变更

- 版本唯一构建来源为 `CMakeLists.txt` 中的 `project(... VERSION ...)`；
- 版本资源、CPack 文件名和关于窗口测试必须从项目版本派生；
- 发布包只包含 `Simpilot.exe`、Everything 四个文件、`LICENSE` 和 `THIRD-PARTY-NOTICES.txt`；
- `.github/ci/release-baseline.json` 保存最近正式版本的产物基线；
- ZIP 受时间戳等元数据影响，不要求不同构建的哈希相同，但每个包必须与自己的 `.sha256` 一致；
- CI 只验证和上传产物，不创建 Git 标签或 GitHub Release。

完整命令和故障处理见 `docs/build-and-run.md` 与 `docs/troubleshooting.md`。

当前正式基线为 `v0.18.8`：`Simpilot.exe` 为 `1,187,840` 字节，ZIP 为
`2,547,504` 字节，ZIP SHA-256 为
`E24652F32FCD978D9A8EF02266CD5E2F9E194F2BCD75A45BFF8B7856D4063011`；
`approvedGrowth` 为空。后续发布候选必须直接与该基线比较。
