# AI 交接提示词（kimi-code 版）

> 本文档面向 kimi-code（或任何具备仓库读写与命令执行能力的 AI agent）。不依赖任何 agent 的私人记忆或会话历史——所有知识都在仓库内。上一棒：kimi-work agent（2026-07-19 完成 GUI 审查加固 + Agent 侧 Session 通路接通）。

## 当前完成状态（一句话版）

Phase 0–5 完成，Phase 6 进行中：**端到端端口转发已在本机验证通过**（RemoteTool 映射监听 → 隧道 → Agent → 目标 echo，双向 sha256 一致），Agent 侧目标白名单已强制执行；两个 Win32 GUI 可用；MinGW 构建零警告、16/16 ctest 全绿；`tools/build-release.sh` 绿色发布已验证。**链路目前是明文 TCP（server-side TLS 未实现），配对码是 UI 层 stub——这是剩余的主要工作。**

## 接手前必读（按顺序）

| 序号 | 文件 | 内容 |
|---|---|---|
| 1 | `STATUS.md` | **当前进度、架构要点、已知限制、下一步**（进度的唯一真实来源） |
| 2 | `README.md` | 构建/使用/白名单/发布说明 |
| 3 | `OPERATION_MANUAL.md` | GUI 操作与冒烟验证 |
| 4 | `docs/README.md` | 文档索引与已确认的关键决策 |
| 5 | `docs/DEVELOPMENT_SPEC.md` | 产品规格（最高优先级） |
| 6 | `docs/PROTOCOL_SPEC.md` | RMT/1 协议规范（线上格式不可偏离） |
| 7 | `docs/CONFIG_SPEC.md` | 4 种配置文件 schema |
| 8 | `docs/IMPLEMENTATION_PLAN.md` | Phase 0–6 任务与完成定义 |
| 9 | `docs/TEST_PLAN.md` | 测试矩阵与发布验收 |
| 10 | `docs/CODING_STANDARDS.md` | 命名/分层/RAII/提交规范 |
| 11 | `docs/ENVIRONMENT.md` + `docs/INSTALL.md` | 工具链位置与已知坑 |

## 环境验证（接手后第一步，必做）

```bash
# Git Bash 下；cmake/ninja/g++ 都在 D:\tools\mingw64\bin
export PATH="/d/tools/mingw64/bin:$PATH"
cd /d/coding/RemoteTool

cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw
# 期望：零警告，16/16 通过（multi_session_test Disabled 属正常）

python tools/smoke_e2e.py
# 期望：[smoke] PASS（端到端 echo sha256 校验，自动清理进程）

bash tools/build-release.sh   # 可选：绿色发布验证 → dist/
```

MSVC 生产验证：`bash tools/msvc-check.sh`（无 vcvars，脚本手动设 INCLUDE/LIB；Git Bash 下 `cmd //c vcvars64.bat` 不可用，见 ENVIRONMENT.md）。

## 架构要点（不要违背）

- **分层**：核心库 `rmt_core`（`include/rmt/` + `src/`：protocol/config/security/tunnel/session/common/platform）不依赖 `apps/`；Win32 API 只允许出现在 `src/platform/` 和两个 GUI app 中。
- **线程模型**：GUI 线程绝不触碰 socket；io_context 跑在工作线程。
  - remote_tool：网络回调 → `EventQueue`（互斥队列）→ 500ms 定时器在 GUI 线程排空；per-mapping 会话计数反向经 `asio::post` 到 io 线程查询、再经 EventQueue 回投。
  - agent_windows：回调 → `PostMessage(WM_USER)` + 互斥日志队列（定时器排空）。
- **帧分发模式**（两侧对称）：
  - RemoteTool：`DeviceManager::set_on_unhandled_frame → SessionManager::on_session_frame(session_id, frame)`
  - Agent：`AgentSessionManager` 接管 `AgentConnection::set_on_frame`；OPEN_SESSION 创建 `AgentSession`（白名单检查、目标连接、超时在其中完成），SESSION_DATA/HALF_CLOSE/CLOSE_SESSION 按 `frame.header.session_id` 路由；断线 `clear_all()`。
- **隧道共享**：`AgentConnection::tunnel()` 返回 `shared_ptr<TunnelConnection>`，会话可安全持有旧隧道直到关闭。
- **半关闭状态机**（2026-07-19 修过，勿回退）：`SESSION_DATA` 在 `Connected|HalfClosedLocal` 接收；对端 HALF_CLOSE 在 `HalfClosedLocal` 表示双侧关闭；本地读/转发在 `HalfClosedRemote` 保持开放。
- **白名单 fail closed**：`TargetWhitelist` 空白名单/无效策略 = 全部拒绝；只接受 IP 字面量，永不解析域名。
- **GDI/句柄**：字体等 GDI 对象成成员、WM_DESTROY 释放；MappingListener 销毁经 `asio::post` 延迟到 io 线程（accept 回调持裸 this）。
- **构建纪律**：`-Wall -Wextra -Wpedantic` 零警告是硬要求；已定义 `_WIN32_WINNT=0x0601`。

## 遗留任务清单（按优先级）

1. **Settings 对话框支持编辑 `target_policy`**（agent_windows）：当前对话框保存会用手改前的内存配置覆盖 agent.json 的白名单（workaround：手改后重启、不点 OK）。
2. **PSK 配对协议接通**：替换配对码 UI stub；实现 server-side TLS（HELLO 后按设备 PSK 升级为 mbedTLS PSK + AES-128-GCM；client path 已完成可参考 `src/security/tls_mbedtls.cpp`）。
3. **心跳 `active_sessions` 接真实统计**（`AgentConnection::send_heartbeat` 目前硬编码 0）。
4. **MSVC 构建复验**：完整 CMake + mbedTLS + GUI 链接 + Release 回归（上次仅 frame/messages/config_loader 单测过）。
5. **启用/修复 2 个 Disabled 集成测试**：`multi_session_test`（async chain hang）、`agent_session_test::test_bidirectional`；补大规模并发（32/128 echo）与 30 分钟稳定性测试。
6. **每设备 session 统计列**（Devices 列表加回 Sess 列，需 per-device 计数）。

次要：`atomic_write` 无 fsync；`AgentSessionManager` 的 `on_event` 只传文本（可考虑结构化事件）。

## 开发规则（沿用）

- 严格按 `docs/CODING_STANDARDS.md`：命名、命名空间、头文件最小包含、平台隔离、RAII（禁止 detach 线程、禁止跨线程直接操作控件、禁止吞错误）。
- 不添加 fallback、旧协议兼容、静默降级；不以空实现或放宽测试通过验收。
- 协议线上格式必须完全符合 `docs/PROTOCOL_SPEC.md`；配置 schema 必须符合 `docs/CONFIG_SPEC.md`。
- 改动后必跑：`cmake --build --preset dev-mingw`（零警告）+ `ctest --preset dev-mingw`（全绿）+ 行为相关时 `python tools/smoke_e2e.py`。
- 每完成一块工作更新 `STATUS.md`（它是唯一进度来源）。
- Git 提交规范见 CODING_STANDARDS §10（phaseN 小步提交；当前工作区有未提交改动，先 `git status` 确认再动手）。**ClaudeCode 等外部 agent 提交须按下方「Git 提交指南」操作，勿直接跳过。**

## Git 提交指南（快速参考）

> `git` 已安装到 `D:\tools\git\cmd\git.exe`，已加入用户 PATH。新开终端直接执行 `git` 即可。

```bash
cd /d/coding/RemoteTool
git add -A
git commit -m "<scope>: <动词> <描述>"
git push origin <当前分支>
```

```powershell
# 如果 git 找不到，用完整路径
cd D:\coding\RemoteTool
D:\tools\git\cmd\git.exe add -A
D:\tools\git\cmd\git.exe commit -m "<scope>: <动词> <描述>"
D:\tools\git\cmd\git.exe push origin <当前分支>
```

**提交信息格式**：`<scope>: <动词> <描述>`，例如 `ui: add dark theme`、`fix: correct session half-close`。

**scope 参考**：`ui` / `core` / `fix` / `feat` / `docs` / `cmake` / `refactor` / `test`

**硬性要求**：
- 每个提交必须可编译
- 提交前跑 `cmake --build --preset dev-mingw`（零警告）+ `ctest --preset dev-mingw`（全绿）
- 行为相关时跑 `python tools/smoke_e2e.py`
