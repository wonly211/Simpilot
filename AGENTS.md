# Simpilot 仓库工作约束

## Release 构建与发布

以下规则是强制要求。任何代理在构建或发布 Simpilot 正式版本时都必须遵守，不得为了节省时间跳过。

1. **正式发布不得直接信任或复用现有 CMake 缓存。** 发布前必须使用全新构建目录，或先执行：

   ```powershell
   cmake --fresh --preset vs2022-x64
   ```

   不得仅因为现有 `build/vs2022-x64` 能成功编译，就认定它适合正式发布。

2. **构建前必须验证 Release 优化参数。** `build/vs2022-x64/CMakeCache.txt` 至少应包含：

   ```text
   CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG
   CMAKE_EXE_LINKER_FLAGS_RELEASE=/INCREMENTAL:NO
   ```

   生成的 `simpilot.vcxproj` 中，Release 配置还应满足：

   ```text
   Optimization=MaxSpeed
   LinkIncremental=false
   ```

   任何值为空、缺失或不符合上述要求时，必须停止发布，重新执行干净配置并查明原因。

3. **正式发布必须依次完成以下验证：**

   - Release 编译成功；
   - 全部自动测试通过；
   - 使用 Release 配置生成发布包；
   - 检查 `Simpilot.exe` 的 FileVersion、ProductVersion 和产品名称；
   - 检查 ZIP 内容及 SHA-256 校验文件；
   - 与上一正式版本比较 `Simpilot.exe` 和 ZIP 的字节大小。

4. **产物体积异常时禁止发布。** 如果 `Simpilot.exe` 或 ZIP 相比上一版本增长超过 5%，且没有明确、可验证的功能或资源变更依据，必须停止发布并检查：

   - Release 优化是否启用；
   - 是否错误启用了增量链接；
   - 是否混入 Debug 文件、缓存、PDB 或无关资源；
   - 包内第三方组件是否发生意外变化。

5. **发布前必须报告精确数据，而不是凭文件名或构建成功作判断。** 至少记录上一版本与当前版本的 EXE 大小、ZIP 大小、差值和百分比。

6. **不得用测试通过替代构建配置检查。** 自动测试主要验证行为，不保证 Release 优化参数正确，也不能发现增量链接造成的体积膨胀。

7. **只有在上述检查全部通过后，才允许提交版本号、推送标签并创建或更新 GitHub Release。** 发布后必须回读 Release，确认它不是草稿或预发布版本，并验证所有附件名称、大小和摘要。

## 事故记录：v0.18.1 构建缓存污染

2026-08-09，v0.18.1 首次发布时复用了异常的 `build/vs2022-x64/CMakeCache.txt`。其中 `CMAKE_CXX_FLAGS_RELEASE` 和 `CMAKE_EXE_LINKER_FLAGS_RELEASE` 被清空，导致 `/O2 /Ob2 /DNDEBUG` 与 `/INCREMENTAL:NO` 均未生效。

结果：

- v0.18.0 `Simpilot.exe`：1,002,496 字节；
- 错误构建的 v0.18.1 `Simpilot.exe`：2,567,168 字节；
- 使用全新构建目录生成的正常 v0.18.1 `Simpilot.exe`：1,002,496 字节；
- 发布 ZIP 因此增加 225,198 字节（9.13%），而 Everything 等其他文件没有变化。

结论：正式发布必须从干净配置开始，并独立验证 Release 优化参数与产物体积。不得再次从未经验证的历史构建缓存直接发布。
