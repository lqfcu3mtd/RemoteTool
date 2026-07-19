# 开发状态报告

版本：1.6
日期：2026-07-19 (end of day)
维护者：RemoteTool 团队

> 本文件是项目进度的唯一真实来源。

## 当前 Phase

**Phase 6：发布与验证** — 进行中。端到端转发已在本机验证通过（python echo + 映射 + Agent 上线，回显 sha256 一致）；绿色发布脚本 `tools/build-release.sh` 已于 2026-07-19 复验通过。

## 整体进度

| Phase | 状态 |
|---|---|
| 0 — 工程初始化 + 核心工具 | ✅ |
| 1 — 基础 TCP + HELLO/HEARTBEAT | ✅ |
| 2 — 单 Session 端口转发 | ✅ |
| 3 — 多 Session + 控制帧优先 + 背压 | ✅ |
| 4 — GUI + 配置持久化 + DPAPI | ✅ |
| 5 — TLS-PSK (mbedTLS) | ✅ client path（server-side TLS 未做，当前链路为明文 TCP） |
| 6 — 发布与验证 | 🟡 端到端冒烟通过；MSVC 构建与长时间稳定性未验证 |
| **总计** | **~93%** |

## 构建与测试（最近验证：2026-07-19）

- 开发构建（MinGW GCC 16.1 + Ninja）：
  `cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw`
  → 编译器警告/错误 **0**（含两个 GUI 应用；已定义 `_WIN32_WINNT=0x0601`），**16/16 ctest 全绿**（multi_session_test 维持 Disabled）
- 绿色发布：`bash tools/build-release.sh` → `dist/remote_tool.exe`（1.5 MB）+ `dist/agent.exe`（1.5 MB），仅依赖系统 DLL（KERNEL32/USER32/GDI32/WS2_32/UCRT），无 MinGW 运行时依赖
- 端到端冒烟：`python tools/smoke_e2e.py` → PASS（256 KiB 随机 + 20 KiB 文本，双连接回显 sha256 一致；脚本自行清理进程与临时目录）
- MSVC x64 /O2（历史验证）：frame_test(49) + messages_test(135) + config_loader_test(24) 通过；完整 MSVC CMake 构建（含 mbedTLS/GUI 链接）未复验

## 架构要点（交接用）

- 核心库 `rmt_core`（`include/rmt/` + `src/`）与 GUI（`apps/`）分层；GUI 线程不触碰 socket，io_context 跑在工作线程
- RemoteTool 侧帧分发：`DeviceManager::set_on_unhandled_frame → SessionManager::on_session_frame`；本地端口接入：`MappingListener`（Start/Stop 真实启停，`start()` 返回 `ErrorCode`）
- Agent 侧帧分发：新增 `AgentSessionManager`，接管 `AgentConnection::set_on_frame`，OPEN_SESSION 创建 `AgentSession`（白名单检查/目标连接在其中完成），SESSION_DATA/HALF_CLOSE/CLOSE_SESSION 按 session_id 路由；`AgentConnection::tunnel()` 以 shared_ptr 共享隧道
- 白名单：agent_windows 按 agent.json `target_policy` 构建 `TargetWhitelist`，空白名单/无效策略 = 全部拒绝（fail closed）
- GUI 线程同步：RemoteTool 用 `EventQueue`（500ms 定时器排空）+ per-mapping 会话计数经 io 线程查询回投；Agent 用 `PostMessage(WM_USER)` + 互斥队列 + 定时器排空
- 半关闭状态机：SESSION_DATA 在 `Connected|HalfClosedLocal` 接收；对端 HALF_CLOSE 在 `HalfClosedLocal` 触发双侧关闭；本地读/转发在 `HalfClosedRemote` 保持开放（2026-07-19 修复）

## 已完成（本轮要点，更早历史见 git log）

### 2026-07-19 GUI 审查与加固
- remote_tool：Mappings Start/Stop 真实启停 `MappingListener`；接通 Session 帧分发；设备离线清理会话；加载 `remote_tool.json` + 新增 Settings 对话框
- 修复：配对码字体 GDI 泄漏、`enabled` 误写成在线状态、剪贴板泄漏、Agent Reconnect 按钮永久断连（改为重建连接实例）
- UI：统一 Segoe UI 字体（DPI 感知）、窗口可调整大小 + 自适应布局 + 最小尺寸、状态栏 4 分区、Agent 大号彩色状态 + 事件日志；消除全部编译警告

### 2026-07-19 Agent 侧 Session 通路接通（端到端可用）
- 新增 `AgentSessionManager`；`AgentConnection::connection_` 改 shared_ptr + `tunnel()`/`io()` 访问器
- 修复 SessionManager 半关闭状态机数据丢弃/会话悬挂 bug
- `MappingListener::start` 返回 `ErrorCode`；`SessionManager::active_sessions_for_mapping()` per-mapping 统计，GUI Conn 列显示真实数字
- agent_windows 强制执行 `TargetWhitelist`；新增 `tools/smoke_e2e.py` 端到端冒烟

### Phase 0–5 已有成果（摘要）
- 帧编解码、HELLO/HEARTBEAT、Session 全套消息（严格 JSON 校验）
- TunnelConnection（写串行化 + 控制帧优先）、AgentConnection（状态机/看门狗/指数退避重连）
- Acceptor、DeviceManager（HELLO/心跳/超时清理）、MappingListener、SessionManager（并发上限/背压高低水位 256/128 KiB/掉线清理）
- AgentSession（白名单→目标连接→双向转发→半关闭）
- 配置体系：strict_json + config_schema + atomic_write + config_loader（4 种配置文件，24 项往返测试）
- DPAPI SecretStore、TargetWhitelist（手写 IP/CIDR 解析，无平台 API 依赖）
- mbedTLS 2.28.7 vendored（瘦配置，PSK + AES-128-GCM），client path 完成
- 两个 Win32 GUI 应用（remote_tool / agent_windows）

## 已知限制与遗留风险

1. **配对码仍是 UI 层 stub**：8 位随机码只在 GUI 生成/展示，未与协议侧校验挂钩；设备认证目前仅有 HELLO 中的 device_id（PSK 配对协议未接通）
2. **Settings 对话框会覆盖手工编辑的 agent.json**：对话框不含 `target_policy` 字段，点 OK 会把外部手改的白名单冲掉（需重启进程重新加载）
3. `AgentConnection` 心跳的 `active_sessions` 字段仍硬编码 0
4. 2 个集成测试维持 Disabled：`multi_session_test`（async chain hang）、`agent_session_test::test_bidirectional`（保留为参考，标 `[[maybe_unused]]`）
5. MSVC 完整构建（mbedTLS + GUI 链接）未复验；30 分钟/8 小时稳定性测试未做
6. `atomic_write` 无 fsync
7. server-side TLS 未实现（需 HELLO 后按设备 PSK 升级），当前链路为明文 TCP —— **不要在生产环境使用当前构建**
8. 每设备 session 数统计未做（Devices 列表无 Sess 列；per-mapping 已有）

## 下一步（按优先级）

1. Settings 对话框支持编辑 `target_policy`（消除手改 JSON 被覆盖的问题）
2. PSK 配对协议接通（替换配对码 stub；server-side TLS）
3. 心跳 `active_sessions` 接真实统计
4. MSVC 构建复验 + Release 全量回归
5. 启用/修复 2 个 Disabled 集成测试；大规模并发（32/128 echo）与稳定性测试
6. 每设备 session 统计列

## 需要用户决定

- 无当前阻塞项
