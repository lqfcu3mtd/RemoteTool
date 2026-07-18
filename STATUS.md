# 开发状态报告

版本：1.0
日期：2026-07-18
维护者：RemoteTool 团队

> 本文件是项目进度的唯一真实来源。任何 agent 接手前应先读本文件，再用构建和测试验证。

## 当前 Phase

**Phase 0：工程初始化** — 进行中（CMake 工程已搭建，核心层首片已写并验证）

## 已完成

### CMake 工程（2026-07-18）
- `CMakeLists.txt` — 顶层 CMake，C++17，rmt_core 静态库，frame_test 收编进 CTest
- `CMakePresets.json` — `dev-mingw`（MinGW+Ninja）+ `windows-x64-debug`（MSVC+Ninja）
- `cmake/Dependencies.cmake` — 第三方依赖占位（Phase 0 无外部依赖）
- MinGW CMake 验证：`cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw` → 100% 通过
- MSVC CMake：用 `tools/msvc-cmake.bat`（在 cmd.exe 里跑 vcvars64 + cmake）

### Phase 0/1 核心层（首片）
- `include/rmt/common/error_code.h` — 稳定错误码（连接级 / Session 级，对齐 PROTOCOL_SPEC §7）
- `include/rmt/protocol/frame.h` — RMT/1 帧结构定义 + `FrameDecoder` 增量解码器接口
- `src/protocol/frame.cpp` — 帧编解码实现（16 字节定长头、网络字节序、拆包/粘包、非法输入直接失败）
- `tests/unit/frame_test.cpp` — 协议测试（官方向量 + 17 项边界用例）
- `include/rmt/test.h` — 轻量测试框架
- `tools/devcheck.sh` — MinGW 开发期验证脚本
- `tools/msvc-check.sh` — MSVC 生产构建验证脚本

### 环境与工具链
- MinGW GCC 16.1.0 @ `D:\tools\mingw64`（开发期）
- VS2022 Community + MSVC 19.44 + Windows 11 SDK @ D 盘（生产构建）
- 两个验证脚本均 49/49 通过

### 文档
- `docs/INSTALL.md` — 安装说明
- `docs/ENVIRONMENT.md` — 环境验证记录
- `docs/CODING_STANDARDS.md` — 代码规范

## 测试

- 命令：`bash tools/devcheck.sh`（MinGW）/ `bash tools/msvc-check.sh`（MSVC）
- 结果：`frame_test: 49 passed, 0 failed`（两端一致）

### 测试覆盖
- 官方测试向量：HEARTBEAT / SESSION_DATA 字节级对齐
- 逐字节喂入（拆包验证）
- 多帧一次输入（粘包验证）
- 超长帧、零长 SESSION_DATA、控制帧带 SessionId
- 错误 Magic / Version → 断开
- 非零 Flags / 非法类型 / 超长 payload → 协议错误

## 已知限制

- MSVC CMake 需在 cmd.exe 里跑 `tools/msvc-cmake.bat`（Git Bash 下 PATH 带空格传递给 cmd 子进程会丢失）
- Phase 0 剩余任务未完成：目标白名单、配置 schema、原子写入、JSON 解析、SecretStore 接口
- mbedTLS / Asio 第三方依赖未接入（Phase 1+ / Phase 4+）
- Win32 GUI / DPAPI 实现未开始（Phase 4+）

## 下一步

1. 补全 Phase 0 核心层（用多 agent 流程，见 `docs/DEV_WORKFLOW.md`）：
   - 目标白名单（CIDR + 端口匹配）
   - 配置 schema 校验 + 原子写入 + 严格 JSON 解析
   - `SecretStore` 接口（dev 文件实现 / 生产 DPAPI 实现）
   - 日志接口（INFO / WARN / ERROR）
   - 范围退出清理工具（RAII guard）
2. 验证：`cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw`

## 需要用户决定

- 无当前阻塞项
- 后续 Phase 4 GUI 设计方案需用户确认时再提出
