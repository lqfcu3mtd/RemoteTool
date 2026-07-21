# RemoteTool v0.1 操作手册

## 概述

RemoteTool 是一个反向 TCP 隧道工具。Agent 主动连接 RemoteTool，RemoteTool 通过 Agent 访问内网目标设备。

架构：`用户 → RemoteTool(本机) → Agent(远程内网) → 目标设备`

## 文件清单

```
build/bin/
  remote_tool.exe   — 服务端（GUI 窗口）
  agent_windows.exe — 客户端（GUI 窗口）
```

## 启动方式

### 方式一：双击 exe

直接双击 `remote_tool.exe` 和 `agent_windows.exe`（两台电脑或本机测试均可）。

### 方式二：命令行

```bash
# 服务端
./build/bin/remote_tool.exe

# 客户端（可本机测试）
./build/bin/agent_windows.exe
```

## 操作步骤（本机测试）

### 1. 启动 RemoteTool

双击 `remote_tool.exe`，弹出窗口，顶部工具栏与底部状态栏均显示 **"Listening on 0.0.0.0:4433"**。

窗口布局：
- 顶部：工具栏（监听地址、在线/运行统计、Settings 按钮）
- 左侧：**Devices** 列表（Agent 上线后自动显示）+ 配对码面板
- 右侧：**Port mappings** 列表（配置好的端口映射）+ 活动会话面板
- 底部：状态栏（监听状态 / 最近事件 / 在线数·映射数·会话数）
- 添加/编辑设备与映射均通过模态对话框（列表下方按钮或 Edit 菜单）

### 2. 启动 Agent

双击 `agent_windows.exe`，弹出窗口显示：
- 大号彩色状态文字：Offline（红）→ Connecting...（橙）→ Authenticating...（蓝）→ **Online**（绿）
- Device ID、Server、重连次数
- 最近事件日志区

### 3. 验证连接

- RemoteTool 窗口的 Devices 列表中应出现 `AGENT001`，状态为 Online，Address 列为 `127.0.0.1:xxxxx`
- Agent 窗口状态显示绿色 **Online**
- 关闭 Agent 窗口，RemoteTool 中对应设备变为 Offline（灰色）

### 4. 添加端口映射（RemoteTool 窗口）

点击映射列表下方的 **+ Add**（或菜单 `Edit → Add mapping...`），在对话框中填入：

| 字段 | 示例值 | 说明 |
|------|--------|------|
| Name | ssh | 映射名称（自定义） |
| Device | AGENT001 | 目标设备 ID（下拉选择） |
| Local port | 10022 | 本地监听端口（本机访问用） |
| Target host | 127.0.0.1 | 目标地址（Agent 内网可达） |
| Target port | 22 | 目标端口 |

点击 OK，映射列表中显示新条目且状态为 **Running**（本地监听立即生效），配置持久化到 `mappings.json`。选中映射可用 Start / Stop / Edit / Delete 管理。

### 5. 使用映射

添加映射后，在本机连接 `127.0.0.1:10022` 即可访问 Agent 内网的 `127.0.0.1:22`（前提：Agent 的 agent.json `target_policy` 白名单已放行该目标，见下文白名单章节）。

### 6. RemoteTool Settings

工具栏 **Settings...**（或菜单 `Edit → Settings...`）可编辑 `remote_tool.json`：Bind host、Agent 监听端口、心跳超时（ms）、每映射会话上限。端口/地址变更需重启 RemoteTool 生效；会话上限即时生效。

## 端到端冒烟验证

仓库自带一键冒烟脚本（本机即可，无需两台机器）：

```bash
python tools/smoke_e2e.py
```

脚本会：启动 python TCP echo server（:19001）→ 在临时目录生成配置（映射 10099→19001、白名单放行）→ 启动 remote_tool.exe 与 agent_windows.exe → 通过映射端口发送 256 KiB 随机数据 + 20 KiB 文本，校验回显 sha256 一致 → 自动结束全部进程并清理。输出 `[smoke] PASS` 即端到端转发正常。

> Debug 构建的 exe 依赖 MinGW 运行时 DLL：命令行运行前需 `export PATH="/d/tools/mingw64/bin:$PATH"`（或直接使用 `dist/` 下静态链接的 Release 版）。

## 配置文件（自动生成）

程序运行后自动生成以下文件：

| 文件 | 说明 |
|------|------|
| `remote_tool.json` | RemoteTool 监听地址/端口、心跳与并发上限（GUI Settings 可编辑） |
| `devices.json` | 设备列表（含 DPAPI 加密的密钥） |
| `mappings.json` | 映射配置（可通过 RemoteTool GUI 编辑） |
| `agent.json` | Agent 的 Device ID 与 Server 地址（Agent Settings 对话框可编辑） |

## 端口说明

| 端口 | 用途 |
|------|------|
| 4433 | RemoteTool 监听 Agent 连接的默认端口 |
| 自定义 | 每个映射的本地监听端口 |

## 注意事项

- **TLS 未启用**：当前链路为明文 TCP（server-side TLS 待做），仅限受信任局域网；Agent 侧目标白名单已强制执行（空白名单全拒）
- 两个程序均为 Win32 GUI，Windows 7+ 即可运行
- `dist/` 下的 Release 版已静态链接运行时（无 DLL 依赖）；`build/bin/` 下的 Debug 版构建后会自动拷贝 MinGW 运行时 DLL 到 exe 旁边
- 防火墙可能需要允许 4433 端口入站

## 构建方法

```bash
# 开发构建（MinGW）
cmake --preset dev-mingw
cmake --build --preset dev-mingw

# 生产构建（MSVC，需在 cmd.exe 下先运行 vcvars64.bat）
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -B build-msvc
cmake --build build-msvc
```

## 当前版本已知限制

- TLS 加密未启用（Phase 5 client path 已完成，server-side TLS 待做）
- Agent 首次启动生成默认配置（device_id=AGENT001，server=127.0.0.1:4433），需通过 Settings 对话框修改
- Agent 的 `target_policy`（目标白名单）需直接编辑 agent.json；**空白名单 = 全部拒绝**，映射转发前必须配置 `allowed_cidrs` + `allowed_ports`
- 多 Session 并发集成测试未通（异步链 hang；端到端冒烟 `tools/smoke_e2e.py` 已验证单/双连接双向转发）

## 白名单（agent.json target_policy）

Agent 侧强制执行目标白名单，RemoteTool 无法绕过。仅当同时满足时放行：

- 目标是 IP 字面量（IPv4；IPv6 需 `allow_ipv6: true`），域名一律拒绝
- 目标端口在 `allowed_ports` 中
- 目标 IP 落在 `allowed_cidrs` 之一内

示例（仅允许转发到本机 22 端口）：

```json
"target_policy": {
  "allowed_cidrs": ["127.0.0.0/8"],
  "allowed_ports": [22],
  "allow_ipv6": false
}
```

> 注意：Agent 的 Settings 对话框目前**不含** `target_policy` 字段，点击 OK 会用内存中的旧配置覆盖 agent.json。手工修改白名单后请直接重启 Agent 生效，不要点 Settings 的 OK。
