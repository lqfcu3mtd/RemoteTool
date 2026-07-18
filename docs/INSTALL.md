# 安装与构建说明（INSTALL）

> 适用阶段：Phase 0 开发期（平台无关核心层）。生产构建（Win32 GUI / DPAPI / mbedTLS）见第 2 节。
> **磁盘约定：所有开发工具一律安装到 D 盘**（C 盘空间紧张，详见用户约定）。

---

## 0. 获取源码

```bash
git clone <仓库地址> RemoteTool
cd RemoteTool
```

---

## 1. 开发期环境（无需管理员权限）

开发期使用**免安装、解压即用**的 MinGW-w64 工具链，验证**平台无关核心层**（协议编解码、状态机、配置校验、目标策略、背压逻辑）。这套环境不需要 Visual Studio，也不需要管理员。

### 1.1 编译器：MinGW-w64（GCC + MinGW-w64，UCRT x86_64）

- 下载页：<https://winlibs.com/>，选择 **Win64 / UCRT / x86_64** 的 standalone 包（线程模型 posix、异常 seh）。
- 已验证版本：**GCC 16.1.0 + MinGW-w64 14.0.0（UCRT）**。
  直链示例（版本会更新，以官网为准）：
  `https://github.com/brechtsanders/winlibs_mingw/releases/download/16.1.0posix-14.0.0-ucrt-r3/winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64ucrt-14.0.0-r3.zip`
- 解压到 **`D:\tools\mingw64`**（解压即用，无需安装程序）。
- 将 `D:\tools\mingw64\bin` 加入用户 PATH，或每次构建前临时指定：
  ```bash
  export PATH="D:/tools/mingw64/bin:$PATH"
  ```
- 验证：
  ```bash
  g++ --version     # 期望：g++.exe (MinGW-W64 x86_64-ucrt-posix-seh ...) 16.1.0
  ```

### 1.2 构建系统：CMake（≥ 3.24）

- 官方安装包：<https://cmake.org/download/>，安装时目标目录选 **D 盘**（如 `D:\tools\cmake`）。
- 也可通过 `winget install Kitware.CMake` 或 `scoop install cmake` 安装，安装时指定 D 盘路径。
- 将 `D:\tools\cmake\bin` 加入 PATH。
- 验证：`cmake --version`（期望 ≥ 3.24）。

### 1.3 生成器：Ninja（可选但推荐）

- 下载：<https://ninja-build.org/>，将 `ninja.exe` 放到 `D:\tools\ninja` 并加入 PATH。
- 或直接使用随 CMake 附带的 Ninja。
- 验证：`ninja --version`。

### 1.4 验证核心层（当前可用）

仓库提供一键脚本 `tools/devcheck.sh`：

```bash
bash tools/devcheck.sh
```

- 默认搜索 `D:/tools/mingw64`；可用环境变量 `MINGW_ROOT` 覆盖路径，例如：
  ```bash
  MINGW_ROOT=D:/tools/mingw64 bash tools/devcheck.sh
  ```
- 行为：用 g++ 编译 `src/protocol/frame.cpp` + `tests/unit/frame_test.cpp` 并运行。
- 期望输出：`frame_test: 49 passed, 0 failed`。

### 1.5 完整 CMake + CTest（工程骨架落地后启用）

> 当前仓库以 `devcheck.sh` 做开发期验证。当 `rmt_core` 静态库与 `CMakeLists.txt` 按 `IMPLEMENTATION_PLAN.md` 落地后，改用标准 CTest 流程：
> ```bash
> cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
> cmake --build build
> ctest --test-dir build --output-on-failure
> ```

---

## 2. 生产构建环境（需管理员权限，Visual Studio 2022）

生产构建锁定 **MSVC x64 + Win32 API + mbedTLS + Windows DPAPI**，必须在装有 VS2022 的 Windows 上完成。开发期 MinGW **无法**替代这一段。

### 2.1 Visual Studio 2022

- 安装「**使用 C++ 的桌面开发**」工作负载，安装路径选 **D 盘**。
- 必含组件：MSVC v143、Windows 10/11 SDK。
- 需要管理员权限（UAC）；非管理员账户无法安装 VS Build Tools。

### 2.2 CMake（≥ 3.24）

同 1.2，安装到 D 盘并加入 PATH。

### 2.3 mbedTLS（TLS-PSK 依赖）

- 通过 vcpkg 或源码编译集成，具体集成方式在 Phase 4 连接层明确。

### 2.4 Windows DPAPI

- 系统原生 `Crypt32.dll`，无需安装；仅在真实 Windows 上可用（开发期 MinGW 不提供该能力，相关代码以接口隔离，待 VS 验收）。

### 2.5 构建（MSVC）

```bash
cmake --preset windows-x64-debug
cmake --build build --config Debug
ctest --test-dir build --config Debug --output-on-failure
```

> `windows-x64-debug` preset 随 `CMakePresets.json` 落地后生效（见 `IMPLEMENTATION_PLAN.md`）。

---

## 3. 已知限制与边界

- 开发期 MinGW **仅验证平台无关核心层**；Win32 GUI、DPAPI、standalone Asio、mbedTLS 必须在 VS2022 + MSVC 上验收。
- 本机开发账户非管理员，无法安装 VS Build Tools；生产构建需管理员账户或 UAC 提权。
- mbedTLS 在开发期未接入（TLS 属 Phase 4+ 连接层）。
- 所有工具装 D 盘；若 PATH 未配置，请用绝对路径或 `MINGW_ROOT` 显式指定。

---

## 4. 故障排查

| 现象 | 处理 |
|---|---|
| `g++: command not found` | 确认 `D:\tools\mingw64\bin` 在 PATH；或 `export PATH="D:/tools/mingw64/bin:$PATH"` |
| `cmake` 版本 < 3.24 | 升级 CMake 到 ≥ 3.24 |
| `devcheck.sh` 报找不到 g++ | 设 `MINGW_ROOT=D:/tools/mingw64` 后重跑 |
| VS 安装需要 UAC 但无管理员 | 改用管理员账户安装 VS2022；MinGW 路径无需管理员 |
| DPAPI / Win32 相关编译失败 | 属预期：开发期 MinGW 不支持，需在 VS2022 + MSVC 下构建 |
