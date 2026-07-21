# 开发环境验证记录

版本：1.0
日期：2026-07-18
维护者：RemoteTool 团队

本文档记录已验证的开发与生产构建环境。任何 agent 接手开发前，应先按本文档确认环境就绪。

## 1. 工具链总览

| 端 | 工具 | 版本 | 安装位置 | 用途 |
|---|---|---|---|---|
| 开发期 | MinGW-w64 GCC | 16.1.0 (MinGW-w64 14.0.0, UCRT x86_64) | `D:\tools\mingw64` | 编译平台无关核心层 + 单元测试 |
| 开发期 | CMake | 4.3.3 (≥3.24 满足规格) | 系统 PATH | 工程构建 + CTest |
| 开发期 | Ninja | 1.13.2 | 系统 PATH | CMake 构建生成器 |
| 开发期 | Git | 2.54.0 | 系统 PATH | 版本控制 |
| 生产构建 | MSVC cl | 19.44.35228 (MSVC 14.44.35207) | `D:\Program Files\Microsoft Visual Studio\2022\Community` | 生产构建（Win32 GUI / DPAPI / mbedTLS） |
| 生产构建 | Windows 11 SDK | 10.0.26100.0 | `D:\Windows Kits\10` | Windows 头文件 + 库（含 UCRT / DPAPI） |

> **所有工具均装 D 盘**（用户要求，C 盘空间紧张）。

## 2. 验证脚本

| 脚本 | 编译器 | 用法 | 预期结果 |
|---|---|---|---|
| `tools/devcheck.sh` | MinGW g++ | `bash tools/devcheck.sh` | `frame_test: 49 passed, 0 failed` |
| `tools/msvc-check.sh` | MSVC cl /O2 | `bash tools/msvc-check.sh` | `frame_test: 49 passed, 0 failed` |

两个脚本均自动定位工具链、配置环境变量、编译并运行 `frame_test`。无需手动设置 PATH 或调用 vcvars64.bat。

## 3. 已验证结果

- MinGW g++ 16.1.0：C++17 特性全可用（filesystem / optional / variant / string_view / 结构化绑定）
- MSVC cl 19.44：C++17 探针编译运行通过
- `frame_test` 在两个编译器下均为 **49/49 通过**，结果一致

## 4. 已知坑与解决方案

### 4.1 Git Bash 下 vcvars64.bat 不可用

**问题**：Git Bash 中 `cmd //c "vcvars64.bat && cl ..."` 始终进入交互模式，路径转义导致命令不执行。

**解决方案**：`tools/msvc-check.sh` 采用手动设置 `INCLUDE` / `LIB` 环境变量 + `cl.exe` 完整路径，绕过 vcvars64.bat。

### 4.2 PowerShell Enter-VsDevShell PATH 重复键

**问题**：VS 自带的 `Enter-VsDevShell` 在 PowerShell 中报 `字典中的关键字:"Path"所添加的关键字:"PATH"` 错误。

**解决方案**：不使用 Enter-VsDevShell，改用上述手动环境变量方案。

### 4.3 PowerShell 无 bash

**问题**：用户 PowerShell 中直接 `bash tools/devcheck.sh` 报 CommandNotFoundException。

**解决方案**：用 Git Bash 终端运行脚本；或在 PowerShell 中用 g++ 完整路径直接编译。

### 4.4 MinGW Debug 构建缺运行时 DLL（2026-07-21 踩坑）

**问题**：直接双击 `build-dev/bin/agent_windows.exe` 报「找不到 libgcc_s_seh-1.dll」。

**原因**：
- `dev-mingw` preset 默认 `CMAKE_BUILD_TYPE=Debug`
- Debug 走动态链接，依赖 `libgcc_s_seh-1.dll` / `libstdc++-6.dll` / `libwinpthread-1.dll`
- 这三个 DLL 不在 PATH 也不在 exe 旁 → 加载失败
- `cbmk_set_link_flags` 只在 **Release** 时用 `-static -static-libgcc -static-libstdc++`，Debug 不沾

**解决方案**：`CMakeLists.txt` 里有 `cbmk_copy_mingw_runtime(target)` 函数，构建后自动把 DLL 拷到 exe 旁边（仅 Debug）。Release 走静态链接，函数是 no-op，dist/ 仍然是干净的 2 个 exe。

**注意**：
- 如果手动从 IDE 启动 Debug 编译，可能漏掉 post-build step（视 IDE 配置），从命令行 `cmake --build` 没问题
- `dist/` 下的 exe 是 Release 静态链接的，不需要任何 DLL

## 5. 新机器环境搭建

详见 [docs/INSTALL.md](INSTALL.md)。核心步骤：

1. **开发期**（免管理员）：下载 winlibs GCC → 解压到 `D:\tools\mingw64` → 确保 CMake ≥3.24 + Ninja 在 PATH
2. **生产构建**（需管理员）：安装 VS2022 Community + 「使用 C++ 的桌面开发」工作负载 → 装到 D 盘 → 确保 Windows SDK 装在 `D:\Windows Kits\10`
3. **验证**：运行 `tools/devcheck.sh` + `tools/msvc-check.sh`，确认 49/49 通过

## 6. 环境边界

- **开发期**（MinGW）：可编译验证所有平台无关核心层（协议、状态机、配置、目标策略、背压逻辑）
- **生产构建**（MSVC）：必须用于 Win32 GUI / DPAPI / mbedTLS 的最终验收
- 开发期发现的问题在 MinGW 下修复后，须在 MSVC 下回归确认
