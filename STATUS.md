# 开发状态报告

版本：1.1
日期：2026-07-18
维护者：RemoteTool 团队

> 本文件是项目进度的唯一真实来源。任何 agent 接手前应先读本文件，再用构建和测试验证。

## 当前 Phase

**Phase 0：工程初始化** — 已完成（核心层全部模块就位，两个空壳程序可构建运行，8 个测试集两端全绿）

## 已完成

### CMake 工程
- `CMakeLists.txt` — 顶层 CMake，C++17，rmt_core 静态库（7 个源文件）+ 2 个空壳可执行程序 + 8 个 CTest 单元测试
- `CMakePresets.json` — `dev-mingw`（MinGW+Ninja）+ `windows-x64-debug`（MSVC+Ninja）
- `cmake/Dependencies.cmake` — Phase 0 无外部依赖（纯 C++17 标准库）
- MinGW CMake：`cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw` → 8/8 通过
- MSVC：手动 cl.exe /W4 /O2 编译全部 8 测试 + 2 程序，零警告全过

### 核心层模块
- `include/rmt/common/error_code.h` — 稳定错误码（连接级 / Session 级，对齐 PROTOCOL_SPEC §7）
- `include/rmt/protocol/frame.h` + `src/protocol/frame.cpp` — RMT/1 帧编解码（增量、严格，非法输入直接失败）
- `include/rmt/common/scope_guard.h` — RAII guard（header-only，dismiss + 移动语义，异常路径执行清理）
- `include/rmt/common/log.h` + `src/common/log.cpp` — 线程安全日志（Debug/Info/Warn/Error，可注册 sink，sink 异常不崩溃）
- `include/rmt/config/strict_json.h` + `src/config/strict_json.cpp` — 手写严格 JSON 解析（拒绝注释/尾逗号/重复键/BOM/错误类型，错误带行列位置）
- `include/rmt/config/atomic_write.h` + `src/config/atomic_write.cpp` — 原子写入（同目录 tmp + rename，失败不损坏原文件）
- `include/rmt/config/config_schema.h` + `src/config/config_schema.cpp` — 4 配置 schema 校验（remote_tool/devices/mappings/agent，fail-fast，未知字段拒绝）
- `include/rmt/security/target_whitelist.h` + `src/security/target_whitelist.cpp` — 目标白名单（手写 IPv4/IPv6+CIDR，环路检测，空白名单全拒）
- `include/rmt/security/secret_store.h` + `src/security/dev_file_secret_store.cpp` — SecretStore 抽象 + dev 明文实现（真实往返，magic+store_kind 校验）

### 应用程序骨架
- `apps/remote_tool/main.cpp` — RemoteTool 空壳（Phase 0 骨架，GUI/网络后续 Phase 填充）
- `apps/agent_windows/main.cpp` — Agent 空壳

### 环境与工具链
- MinGW GCC 16.1.0 @ `D:\tools\mingw64`（开发期）
- VS2022 Community + MSVC 19.44 + Windows 11 SDK @ D 盘（生产构建）
- `tools/devcheck.sh`（MinGW frame_test）、`tools/msvc-check.sh`（MSVC frame_test）、`tools/msvc-cmake.bat`（MSVC CMake，需 cmd.exe）

## 测试

- 命令：`cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw`
- 结果：8/8 通过，531 项断言（frame 49 / scope_guard 6 / log 16 / strict_json 173 / atomic_write 42 / target_whitelist 77 / secret_store 43 / config_schema 125）
- 两端一致：MinGW `-Wall -Wextra -Wpedantic` 与 MSVC `/W4 /O2` 均零新增警告

### 测试覆盖
- frame：官方向量 + 拆包/粘包 + 各类非法输入
- scope_guard：正常/异常路径清理、dismiss、移动语义
- log：级别过滤、sink 注册、sink 异常不崩溃、多线程并发
- strict_json：合法/拒绝（注释/尾逗号/重复键/BOM/前导零/坏转义/未闭合/代理对）+ 错误位置
- atomic_write：新建/覆盖/失败不损坏/临时文件清理
- target_whitelist：CIDR 命中/不命中/回环/空白名单/域名/IPv6 开关/环路端口/非法 CIDR
- secret_store：往返还原/空明文/32 字节 PSK/篡改拒绝
- config_schema：4 schema 完整校验 + 未知字段 + 重复 id + 重复 local_port + 跨字段关系

## 已知限制

- `atomic_write` 无 fsync（`std::ofstream::flush` 仅刷用户态缓冲）；真正的持久性需 platform 层 `FlushFileBuffers`/`fsync`（Phase 4+）
- `atomic_write` 的 rename 回退方案（remove+rename）非严格原子（旧工具链下）；当前 MinGW/MSVC 主路径 `MoveFileExW+MOVEFILE_REPLACE_EXISTING` 直接覆盖，回退不触发
- `config_schema` 不做跨文件引用校验（mappings.device_id 是否存在 devices）；留给上层加载器
- `config_schema` 对 host 字段（bind_host / target_host / server.host）放宽为非空字符串校验，IP 字面量格式由运行时绑定层和 target_whitelist 负责
- `SecretStore` 的 DPAPI 生产实现未做（留 `rmt::platform` Phase 4+）；当前仅 dev 明文实现
- MSVC CMake 需在 cmd.exe 跑 `tools/msvc-cmake.bat`（PowerShell 手动 cl 已验证全部测试）
- mbedTLS / Asio 第三方依赖未接入（Phase 1+ / Phase 4+）
- Win32 GUI / DPAPI 实现未开始（Phase 4+）

## 下一步

**Phase 1：基础 TCP、帧协议与在线状态**
- RemoteTool：Agent TCP 监听器、接受多连接、增量解码帧、HELLO/HEARTBEAT、设备在线状态、重复 device_id 拒绝、心跳超时断开
- Agent：连接 RemoteTool、HELLO、HELLO_ACK、心跳、断线退避重连
- 接入 standalone Asio（Phase 1 依赖，需在 `cmake/Dependencies.cmake` 固定版本）

## 需要用户决定

- 无当前阻塞项
- Phase 1 接入 Asio 版本固定（建议 standalone Asio 1.30.2，header-only）需确认
