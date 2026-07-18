# 开发状态报告

版本：1.3
日期：2026-07-18
维护者：RemoteTool 团队

> 本文件是项目进度的唯一真实来源。任何 agent 接手前应先读本文件，再用构建和测试验证。

## 当前 Phase

**Phase 4：配置持久化与 SecretStore** — 进行中（DPAPI + ConfigLoader 完成，15/15）

### Phase 4 新增
- `include/rmt/platform/dpapi_secret_store.h` + `src/platform/dpapi_secret_store.cpp` — DPAPI 生产实现（CryptProtectData/CryptUnprotectData, #ifdef _WIN32）
- `include/rmt/config/config_loader.h` + `src/config/config_loader.cpp` — 配置加载/保存（4 种配置文件，24 测试往返验证）
- 下一步：Win32 GUI（主窗口 + 设备/映射列表）

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

### Phase 2 — 单 Session 端口转发
- `include/rmt/protocol/messages.h/cpp` 扩展 — Session 消息编解码（OPEN_SESSION/SESSION_OPENED/SESSION_OPEN_FAILED/SESSION_DATA/HALF_CLOSE/CLOSE_SESSION，54 新测试）
- `include/rmt/session/session_id.h` — SessionId 分配器（单调递增，60s 黑名单防重用）
- `include/rmt/tunnel/agent_session.h` + `src/tunnel/agent_session.cpp` — Agent 侧 Session 处理器（白名单检查→目标连接→双向转发→半关闭/关闭）
- `include/rmt/tunnel/mapping_listener.h` + `src/tunnel/mapping_listener.cpp` — RemoteTool 本地端口监听器
- `include/rmt/tunnel/session_manager.h` + `src/tunnel/session_manager.cpp` — RemoteTool Session 管理器（状态转移、双向转发、统计，33 测试）
- DeviceManager 扩展：`get_connection()` + `set_on_unhandled_frame()`（Session 帧分发）

### Phase 3 — 多 Session 与背压
- TunnelConnection 控制帧优先：`control_queue_` + `data_queue_`，do_write 优先发控制帧（PROTOCOL_SPEC §11）
- SessionManager 并发上限：`set_max_sessions(32)` + `active_session_count()` + `create_session` 拒绝超限
- SessionManager Agent 掉线清理：`remove_all_sessions_for_device()`
- SessionManager 背压：高低水位（256 KiB / 128 KiB），暂停/恢复 local socket 读
- 多 Session 并发验证：`test_multi_session_independent`（3 Session 独立生命周期）

### 依赖
- standalone Asio 1.30.2 vendored in `third_party/asio/`
- 环境：MinGW GCC 16.1.0 / VS2022 MSVC 19.44 + Windows 11 SDK

## 测试

- 命令：`cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw`
- 结果：**15/15 通过**
  - Phase 0（8）、Phase 1（5）、Phase 2（1 session_manager + 1 messages expanded）、Phase 3（1 session_manager expanded）
  - 总计 ~800 项断言，MinGW `-Wall -Wextra -Wpedantic` 零警告

### 已知限制
- `agent_session_test` 双向转发异步链 test_bidirectional 注释（Phase 3 大规模集成测试将替代）
- 大规模并发集成测试未做（32/128 并发 echo + 30 分钟稳定性）
- 无 TLS（Phase 5）
- MSVC CMake 需在 cmd.exe 跑

## 下一步

**Phase 3 收尾**：大规模并发集成测试（32 echo + 背压验证）
**Phase 4**：RemoteTool GUI + 配置持久化（Win32 控件 + 设备/映射管理）

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
