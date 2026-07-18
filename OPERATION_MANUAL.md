# RemoteTool v0.1 操作手册

## 概述

RemoteTool 是一个反向 TCP 隧道工具。Agent 主动连接 RemoteTool，RemoteTool 通过 Agent 访问内网目标设备。

架构：`用户 → RemoteTool(本机) → Agent(远程内网) → 目标设备`

## 文件清单

```
build-dev/bin/
  remote_tool.exe   — 服务端（GUI 窗口）
  agent_windows.exe — 客户端（GUI 窗口）
```

## 启动方式

### 方式一：双击 exe

直接双击 `remote_tool.exe` 和 `agent_windows.exe`（两台电脑或本机测试均可）。

### 方式二：命令行

```bash
# 服务端
./build-dev/bin/remote_tool.exe

# 客户端（可本机测试）
./build-dev/bin/agent_windows.exe
```

## 操作步骤（本机测试）

### 1. 启动 RemoteTool

双击 `remote_tool.exe`，弹出窗口，底部状态栏显示 **"Listening on :4433"**。

窗口布局：
- 上方：**Devices** 列表（Agent 上线后自动显示）
- 中间：**Mappings** 列表（配置好的端口映射）
- 下方：输入控件（添加设备/映射）

### 2. 启动 Agent

双击 `agent_windows.exe`，弹出窗口显示：
- Device ID: AGENT001
- Server: 127.0.0.1:4433
- Status: Connecting... → Authenticating... → **Online**

### 3. 验证连接

- RemoteTool 窗口的 Devices 列表中应出现 `AGENT001 [127.0.0.1:xxxxx]`
- Agent 窗口 Status 显示 **Online**
- 关闭 Agent 窗口，RemoteTool 中对应设备消失

### 4. 添加端口映射（RemoteTool 窗口）

在 Mappings 区域的输入框填入：

| 字段 | 示例值 | 说明 |
|------|--------|------|
| name | ssh | 映射名称（自定义） |
| device | AGENT001 | 目标设备 ID |
| host:port | 127.0.0.1:22 | 目标地址（Agent 内网可达） |
| lport | 10022 | 本地监听端口（本机访问用） |
| tport | 22 | 目标端口 |

点击 **+Map** 按钮，映射列表中显示新条目，配置持久化到 `mappings.json`。

### 5. 使用映射

添加映射后，在本机连接 `127.0.0.1:10022` 即可访问 Agent 内网的 `127.0.0.1:22`。

## 配置文件（自动生成）

程序运行后自动生成以下文件：

| 文件 | 说明 |
|------|------|
| `devices.json` | 设备列表（含 DPAPI 加密的密钥） |
| `mappings.json` | 映射配置（可通过 RemoteTool GUI 编辑） |

## 端口说明

| 端口 | 用途 |
|------|------|
| 4433 | RemoteTool 监听 Agent 连接的默认端口 |
| 自定义 | 每个映射的本地监听端口 |

## 注意事项

- **Phase 5 TLS 未启用**：当前为明文 TCP，仅限本地测试
- 两个程序均为 Win32 GUI，Windows 7+ 即可运行
- 需要 VC++ 运行时（通常 Windows 自带）或 MinGW 运行时
- 防火墙可能需要允许 4433 端口入站

## 构建方法

```bash
# 开发构建（MinGW）
cmake --preset dev-mingw
cmake --build build-dev

# 生产构建（MSVC，需在 cmd.exe 下先运行 vcvars64.bat）
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -B build-msvc
cmake --build build-msvc
```

## 当前版本已知限制

- TLS 加密未启用（Phase 5）
- Agent 配置为硬编码（device_id=AGENT001，server=127.0.0.1:4433）
- 多 Session 并发集成测试未通（异步链 hang，不影响单连接使用）
