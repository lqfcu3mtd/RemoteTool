# 开发状态报告

版本：1.2
日期：2026-07-18
维护者：RemoteTool 团队

> 本文件是项目进度的唯一真实来源。任何 agent 接手前应先读本文件，再用构建和测试验证。

## 当前 Phase

**Phase 1：基础 TCP、帧协议与在线状态** — 已完成（RemoteTool TCP 监听器 + Agent 连接状态机 + HELLO/HEARTBEAT + 端到端集成测试通过）

## 已完成

### Phase 0 — 工程初始化与核心工具
- `rmt_core` 静态库（13 个源文件）+ 2 个可执行程序
- 核心工具：error_code、frame 编解码、scope_guard、log、strict_json、atomic_write、config_schema、target_whitelist、secret_store

### Phase 1 — 基础 TCP + 帧协议 + 在线状态
- `include/rmt/protocol/messages.h` + `src/protocol/messages.cpp` — HELLO/HELLO_ACK/HEARTBEAT/HEARTBEAT_ACK JSON 编解码（81 测试，严格字段校验）
- `include/rmt/tunnel/connection.h` + `src/tunnel/connection.cpp` — TCP socket RAII + FrameDecoder（async read/write 串行化，46 测试含本地 echo server）
- `include/rmt/tunnel/agent_connection.h` + `src/tunnel/agent_connection.cpp` — Agent 连接状态机（CONNECTING→WAIT_HELLO_ACK→ONLINE，心跳，看门狗，指数退避重连 ±20% 抖动）
- `include/rmt/tunnel/acceptor.h` + `src/tunnel/acceptor.cpp` — RemoteTool TCP acceptor（async accept 循环）
- `include/rmt/tunnel/device_manager.h` + `src/tunnel/device_manager.cpp` — 设备管理（HELLO 处理/重复拒绝，HEARTBEAT ACK 回复，超时清理）

### 应用程序
- `apps/remote_tool/main.cpp` — Acceptor + DeviceManager 启动监听 :4433
- `apps/agent_windows/main.cpp` — AgentConnection 连接 RemoteTool

### 集成测试
- `tests/integration/hello_heartbeat_test.cpp` — 端到端 RemoteTool Acceptor + Agent HELLO 握手 + 心跳 + 断开检测（2.75s）

### 依赖
- standalone Asio 1.30.2 vendored in `third_party/asio/`
- 环境：MinGW GCC 16.1.0 / VS2022 MSVC 19.44 + Windows 11 SDK

## 测试

- 命令：`cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw`
- 结果：**13/13 通过**
  - Phase 0（8）：frame 49 / scope_guard 6 / log 16 / strict_json 173 / atomic_write 42 / target_whitelist 77 / secret_store 43 / config_schema 125
  - Phase 1（5）：connection 46 / messages 81 / agent_connection 7 / device_manager 44 / hello_heartbeat 3
  - 总计：~712 项断言，MinGW `-Wall -Wextra -Wpedantic` 零警告

### 集成测试覆盖
- hello_heartbeat：RemoteTool Acceptor 接受 Agent → HELLO 握手 → 设备上线回调 → 心跳交换 → Agent stop → 设备离线回调

## 已知限制

- `atomic_write` 无 fsync（留 platform 层 Phase 4+）
- `SecretStore` DPAPI 生产实现未做（留 Phase 4+）
- Phase 1 无 TLS（TCP 明文，TLS 在 Phase 5 实施）
- Agent 配置硬编码（未加载 agent.json，留 Phase 2）
- 重连退避在 Agent 状态机实现，RemoteTool 侧无设备列表持久化
- MSVC CMake 需在 cmd.exe 跑 `tools/msvc-cmake.bat`

## 下一步

**Phase 2：单 Session 端口转发**
- RemoteTool 创建固定 MappingListener
- 接受本地 TCP 连接 → 分配 SessionId → 发送 OPEN_SESSION
- Agent 检查目标策略并异步连接目标
- SESSION_OPENED/FAILED 处理
- 双向转发 SESSION_DATA + 半关闭
- 统计双向字节数
- 使用本地 echo server 验证双向转发 + 100 MiB 随机数据哈希一致

## 需要用户决定

- 无当前阻塞项
- Phase 2 前确认 Session 转发是否需在 Phase 1 集成测试基础上继续（已就绪）
