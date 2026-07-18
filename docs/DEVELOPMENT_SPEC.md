# 远程维护隧道工具开发规格书

版本：V1.0  
状态：需求确认稿  
适用阶段：MVP / Demo

## 1. 项目目标

开发一套轻量化反向隧道工具，使公司工程师无需远程控制现场电脑，即可通过本机已有的 SSH 客户端和浏览器访问现场设备。

系统由以下两个程序组成：

- `RemoteTool.exe`：运行在公司工程师的 Windows 电脑上，负责设备管理、Agent 接入、端口映射和隧道转发。
- `Agent`：运行在现场 Windows 电脑或被维护的 Linux/OpenWrt 设备上，主动连接 RemoteTool，并将隧道连接转发到指定目标 IP 和端口。

MVP 不实现 SSH 终端、内置浏览器、远程桌面、虚拟网卡或云端中继。

## 2. 使用场景

### 2.1 Windows 网关代理

Agent 运行在现场 Windows 电脑，通过现场局域网访问设备：

```text
工程师 SSH/浏览器
    ↓
RemoteTool 本地端口
    ↓ 加密反向隧道
Windows Agent
    ↓ 现场局域网
192.168.1.1:22 / 192.168.1.1:80
```

### 2.2 Linux/OpenWrt 设备直连

Agent 直接运行在被维护设备上，只转发设备自身服务：

```text
工程师 SSH/浏览器
    ↓
RemoteTool 本地端口
    ↓ 加密反向隧道
Linux/OpenWrt Agent
    ↓
127.0.0.1:22 / 127.0.0.1:80 / 127.0.0.1:8080
```

第一阶段先实现 Windows RemoteTool 和 Windows Agent。Linux/OpenWrt Agent 在协议稳定后，按实际硬件平台和 OpenWrt SDK 增加。

## 3. 最终软件形态

### 3.1 RemoteTool

运行平台：Windows 10、Windows 11，优先支持 x64。

主要功能：

- 添加、删除、重命名逻辑设备；
- 生成一次性配对码；
- 显示 Agent 在线、离线和最后心跳时间；
- 为设备创建、启动、停止和删除 TCP 端口映射；
- 显示本地入口、远端目标、运行状态和活动连接数；
- 接受同一映射端口上的多个并发连接；
- 保存设备、密钥和端口映射配置；
- 记录必要的连接及错误日志。

发布目录：

```text
RemoteTool/
├── RemoteTool.exe
├── remote_tool.json
└── README.txt
```

### 3.2 Windows Agent

主要功能：

- 读取 `agent.json`；
- 主动连接 RemoteTool；
- 首次配对及后续设备认证；
- 心跳、掉线检测和自动重连；
- 接收建立 Session 的指令；
- 为每个 Session 建立独立目标 TCP 连接；
- 在目标连接与隧道之间双向透明转发；
- 显示最小状态窗口：连接中、已连接、认证失败、已断开；
- 关闭窗口后退出。

发布目录：

```text
WindowsAgent/
├── Agent.exe
├── agent.json
└── README.txt
```

### 3.3 Linux/OpenWrt Agent

后续版本主要功能与 Windows Agent 相同，但无 GUI。按目标设备分别交叉编译，例如：

```text
agent-openwrt-armv7
agent-openwrt-aarch64
agent-openwrt-mips
agent-openwrt-mipsel
agent-linux-x86_64
```

可增加 OpenWrt `procd` 服务脚本，但不属于第一阶段交付内容。

## 4. 核心架构

Agent 使用反向连接模式：

```text
Agent ──TCP/加密隧道──> RemoteTool
```

RemoteTool 不主动连接 Agent。MVP 在公司局域网中测试，RemoteTool 的局域网 IP 和监听端口手工写入 `agent.json`。

RemoteTool 中的“设备”是逻辑对象，不创建虚拟网卡，也不分配 `172.16.x.x` 虚拟 IP。

## 5. 本地端口映射

第一版只监听本机回环地址：

```text
127.0.0.1:<local_port>
```

示例：

| 设备 | 名称 | RemoteTool 本地入口 | Agent 侧目标 |
|---|---|---|---|
| SITE001 | SSH | `127.0.0.1:10022` | `192.168.1.1:22` |
| SITE001 | Web | `127.0.0.1:18080` | `192.168.1.1:80` |
| SITE001 | Local Web | `127.0.0.1:18081` | `127.0.0.1:8080` |

工程师使用现有工具：

```text
ssh -p 10022 root@127.0.0.1
```

```text
http://127.0.0.1:18080
```

监听地址不得默认绑定 `0.0.0.0`，避免将维护入口暴露到公司局域网。

## 6. 多连接要求

一个端口映射必须支持多个同时存在的 TCP 连接。

- 每接受一个本地 TCP 连接，RemoteTool 创建一个唯一 Session；
- Agent 为每个 Session 建立一个独立的目标 TCP 连接；
- Session 之间的数据、状态和关闭事件必须完全隔离；
- 一个 Session 关闭不得影响同一映射下的其他 Session；
- 浏览器打开一个页面可能建立多个并发连接，Web 映射必须正常支持；
- 多个 SSH 客户端可以同时连接同一个本地映射端口；
- 停止映射时，关闭该映射的监听器和全部活动 Session；
- Agent 离线时关闭相关活动 Session，但保留映射配置；
- Agent 重新上线后，映射可恢复监听或继续等待新连接。

默认资源限制：

- 单个映射最多 32 个活动连接；
- 单台 Agent 最多 128 个活动连接；
- 参数可配置；
- 超限时拒绝新连接并记录明确原因。

## 7. 配对与设备认证

### 7.1 首次配对

RemoteTool 添加设备时创建：

```text
device_id: SITE001
pairing_code: 58392147
```

MVP统一使用8位数字配对码。短码只用于受信任局域网的首次配对，不能作为长期PSK。

流程：

```text
RemoteTool 创建设备
→ 生成一次性配对码
→ 将 device_id 和 pairing_code 写入 Agent 配置
→ Agent 发起首次配对
→ RemoteTool 验证配对码及有效期
→ 生成至少 128 位随机长期设备密钥
→ 安全返回并由 Agent 保存
→ 配对码立即失效
```

### 7.2 后续认证

Agent 使用以下信息自动认证：

```text
device_id + device_key
```

要求：

- 每台设备使用不同的长期密钥；
- 密钥必须由安全随机数生成器产生；
- 删除设备后原密钥立即失效；
- 日志不得输出完整密钥；
- 配置界面不得长期明文显示密钥；
- 认证失败必须明确断开，禁止匿名降级或跳过认证。

Demo 初期允许暂时手工配置随机设备密钥，但协议接口应为一次性配对保留明确消息类型。

## 8. 通信安全

正式交付的隧道必须同时具备：

- RemoteTool 与 Agent 身份认证；
- 会话加密；
- 数据完整性校验；
- 防止简单重放攻击。

Windows MVP 可先完成普通 TCP 链路验证，再加入安全层；最终发布版本不得以明文模式运行。

Linux/OpenWrt Agent 优先方案：

- TLS 1.2 PSK；
- 每台设备独立长期 PSK；
- 握手动态派生会话密钥；
- AES-128-GCM；
- 优先复用设备已有 mbedTLS；
- 无兼容库时静态链接裁剪后的 mbedTLS；
- 不引入完整 CA、X.509、RSA、PEM 证书体系。

是否使用 ECDHE-PSK 由实际设备空间和性能测试决定。不能自行设计未经审计的加密协议。

## 9. 隧道协议

采用固定头部的二进制帧协议。所有多字节整数统一使用网络字节序。

建议帧头：

| 字段 | 大小 | 说明 |
|---|---:|---|
| Magic | 2 bytes | 协议标识 |
| Version | 1 byte | 协议版本 |
| Type | 1 byte | 消息类型 |
| Session ID | 4 bytes | 会话编号，控制消息可为0 |
| Payload Length | 4 bytes | Payload 字节数 |
| Payload | N bytes | 消息内容 |

核心消息类型：

- `PAIR_REQUEST`
- `PAIR_RESPONSE`
- `AUTH_REQUEST`
- `AUTH_RESPONSE`
- `HEARTBEAT`
- `HEARTBEAT_ACK`
- `OPEN_SESSION`
- `SESSION_OPENED`
- `SESSION_OPEN_FAILED`
- `SESSION_DATA`
- `CLOSE_SESSION`
- `ERROR`

协议要求：

- 必须正确处理 TCP 粘包、拆包和半包；
- Payload 长度必须设定严格上限；
- 未知版本、未知类型和非法长度必须立即报错并断开；
- 不做静默兼容和协议猜测；
- `SESSION_DATA` 必须通过 Session ID 路由；
- Session ID 在连接生命周期内不得重复；
- 双向关闭和异常关闭必须释放资源。

## 10. Session 建立流程

```text
1. SSH工具或浏览器连接 RemoteTool 本地监听端口
2. RemoteTool 接受连接并分配 Session ID
3. RemoteTool 向 Agent 发送 OPEN_SESSION(target_host, target_port)
4. Agent 建立目标 TCP 连接
5. Agent 返回 SESSION_OPENED 或 SESSION_OPEN_FAILED
6. 成功后双向发送 SESSION_DATA
7. 任意一端关闭，发送 CLOSE_SESSION 并释放两端资源
```

目标地址必须来自 RemoteTool 中已保存的映射配置。Agent 可配置目标地址或端口白名单，禁止未授权的任意目标访问。

## 11. 多路复用与流量控制

MVP 使用一条 Agent 长连接承载控制消息和多个 Session 数据帧。

必须避免以下问题：

- 一个慢 Session 无限堆积导致整个 Agent 内存增长；
- 一个大流量 Session 饿死心跳和其他 Session；
- GUI 线程被网络读写阻塞；
- 向已关闭 Session 继续发送数据。

最低实现要求：

- 每个 Session 使用独立发送缓冲区并设置上限；
- 控制消息优先于普通数据消息；
- 使用轮询或公平队列调度 Session 数据；
- 写队列达到上限时暂停对应源端读取，形成背压；
- 不允许简单丢弃 TCP Payload；
- 心跳超时后统一关闭相关 Session。

如果实测发现单隧道存在明显队头阻塞，后续版本可升级为“一条控制连接＋每个 Session 独立数据连接”，但不属于第一版默认实现。

## 12. 线程和模块设计

### 12.1 RemoteTool 模块

```text
RemoteTool
├── GUI
├── DeviceManager
├── MappingManager
├── AgentServer
├── Authentication
├── TunnelProtocol
├── SessionManager
├── LocalListener
├── ConfigStore
└── Logger
```

线程原则：

- GUI 主线程只处理界面与消息分发；
- 网络监听、Agent连接、Session转发在工作线程或异步事件循环执行；
- 工作线程通过线程安全消息队列通知 GUI；
- GUI 不直接持有和操作裸 Socket；
- 关闭程序时按监听器、Session、Agent连接的顺序清理。

### 12.2 Agent 模块

```text
Agent
├── ConnectionManager
├── Authentication
├── TunnelProtocol
├── SessionManager
├── TargetConnector
├── ConfigStore
├── Platform
└── Logger
```

核心网络、协议和 Session 逻辑不得依赖 GUI，以便后续复用到 Linux/OpenWrt。

## 13. 配置示例

### 13.1 RemoteTool 配置

```json
{
  "listen_host": "0.0.0.0",
  "listen_port": 4433,
  "max_sessions_per_device": 128,
  "max_sessions_per_mapping": 32,
  "heartbeat_timeout_seconds": 30
}
```

说明：`listen_host` 是 Agent 接入监听地址；端口映射监听地址固定默认为 `127.0.0.1`，两者不得混淆。

### 13.2 Agent 首次配对配置

```json
{
  "server": "192.168.1.100",
  "port": 4433,
  "device_id": "SITE001",
  "pairing_code": "58392147"
}
```

配对成功后保存长期设备密钥，并删除或清空一次性配对码。

## 14. GUI 最小范围

RemoteTool 主界面至少包含：

### 设备列表

- 设备名称/ID；
- 在线状态；
- Agent版本；
- 最后上线或心跳时间；
- 当前 Session 数量。

### 映射列表

- 映射名称；
- 本地入口；
- 远端目标；
- 启动/停止状态；
- 活动连接数；
- 启动、停止、编辑、删除操作。

Windows Agent 只提供最小状态窗口，不提供映射编辑功能。

## 15. 技术选型

### RemoteTool与Windows Agent

- C++17；
- CMake 3.24或更高；
- Win32 API；
- standalone Asio（Windows底层使用Winsock）；
- mbedTLS；
- Windows 安全随机数接口；
- JSON 配置；
- 不使用 Qt；
- 不使用 Boost；
- 不引入大型运行时或复杂框架。

Windows MVP固定使用mbedTLS，以便后续与Linux/OpenWrt Agent复用协议、安全配置和测试。依赖版本在Phase 0锁定，禁止运行时自动选择其他TLS实现或弱密码套件。

### Linux/OpenWrt Agent

- C或精简C++；
- POSIX Socket；
- 裁剪后的 mbedTLS 或复用固件已有 mbedTLS；
- OpenWrt SDK交叉编译；
- 按CPU架构分别发布；
- 无GUI。

## 16. 预计发布大小

以下为 Release、去除调试符号后的目标范围，不作为强制验收阈值：

| 程序 | 目标大小 |
|---|---:|
| RemoteTool.exe | 约 3～10 MB |
| Windows Agent.exe | 约 1～4 MB |
| Linux/OpenWrt Agent，复用现有TLS库 | 约 100～500 KB |
| Linux/OpenWrt Agent，静态精简mbedTLS | 约 300 KB～1 MB |

最终体积取决于 CPU、C库、静态/动态链接方式、密码套件和编译器。不得为了体积取消身份认证、完整性校验或正式版本加密。

## 17. 开发原则

- Keep it simple；
- Fail fast；
- 不添加 fallback logic；
- 不保留未要求的旧行为；
- 不做复杂兼容层；
- 协议假设被破坏时明确失败；
- 先完成核心链路，再增加安全层和GUI；
- 网络、协议、Session和GUI保持模块隔离；
- 不提前实现后续版本功能。

## 18. 开发阶段

### Phase 0：工程初始化

任务：

- 创建 CMake 工程；
- 建立 RemoteTool、Agent、Common、Tests、docs 目录；
- 实现基础日志、错误码和配置读取；
- 生成两个可执行程序。

验收：两个程序可在 Windows x64 Release 模式编译和启动。

### Phase 1：基础TCP与协议

任务：

- RemoteTool TCP Server；
- Agent TCP Client；
- 帧编解码；
- 注册、心跳、断线检测和自动重连；
- 单Agent状态显示或控制台日志。

验收：Agent连接后RemoteTool显示设备上线，拆包、粘包测试通过。

### Phase 2：单Session端口转发

任务：

- 本地监听端口；
- `OPEN_SESSION`；
- Agent目标连接；
- 双向数据转发；
- 关闭及错误处理。

验收：通过RemoteTool本地端口访问现场SSH和HTTP服务。

### Phase 3：多Session并发

任务：

- Session ID分配和路由；
- 同一映射多连接；
- 多映射并发；
- 背压和缓冲区限制；
- 活动连接计数。

验收：多个SSH客户端同时工作；浏览器页面及其并发资源请求正常；关闭单个连接不影响其他连接。

### Phase 4：设备管理和GUI

任务：

- 设备列表；
- 映射增删改、启停；
- 状态更新；
- 配置持久化；
- Windows Agent最小状态窗口。

验收：所有核心操作可通过界面完成，重启RemoteTool后配置保留。

### Phase 5：配对与安全层

任务：

- 一次性配对码；
- 长期设备密钥；
- Agent认证；
- 隧道加密和完整性保护；
- 密钥安全保存；
- 错误及攻击场景测试。

验收：无有效密钥无法上线；配对码不能重复使用；抓包不可读取隧道明文；篡改数据会导致连接失败。

### Phase 6：绿色发布与系统测试

任务：

- Windows 10/11测试；
- Release优化；
- 去除调试符号；
- 整理配置模板和README；
- 长时间运行、重连和并发测试。

验收：无需安装和管理员权限即可运行；复制目录后可完成完整维护流程。

### Phase 7：Linux/OpenWrt Agent（后续）

任务：

- 抽离平台接口；
- 使用目标OpenWrt SDK交叉编译；
- TLS库裁剪或复用；
- 设备直连模式；
- 按实际CPU架构发布。

## 19. MVP 验收标准

MVP完成必须同时满足：

1. Windows Agent 能主动连接指定 RemoteTool；
2. RemoteTool 能正确显示设备在线、离线；
3. 设备认证失败时拒绝连接；
4. 可以创建多个端口映射；
5. 每个映射可以接受多个并发连接；
6. 多个SSH客户端可同时通过同一映射工作；
7. Web页面及并发资源请求可正常加载；
8. 一个Session关闭不影响其他Session；
9. Agent断线后能自动重连；
10. RemoteTool重启后设备和映射配置保留；
11. 映射默认只监听 `127.0.0.1`；
12. 正式Demo隧道启用认证、加密和完整性保护；
13. Windows 10、Windows 11无需安装、无需管理员权限运行。

## 20. 第一版非目标

第一版明确不实现：

- 内置SSH终端；
- 内置Web浏览器；
- UDP转发；
- 虚拟网卡和VPN；
- 云端中继服务；
- 文件管理器；
- 文件上传下载；
- 固件升级；
- 用户权限和RBAC；
- 多租户；
- 操作审计平台；
- 远程桌面；
- 自动升级；
- macOS Agent；
- 通用任意网络跳板。

## 21. 建议工程目录

```text
RemoteMaintenance/
├── CMakeLists.txt
├── RemoteTool/
│   ├── gui/
│   ├── device/
│   ├── mapping/
│   ├── network/
│   ├── session/
│   └── main.cpp
├── Agent/
│   ├── network/
│   ├── session/
│   ├── platform/
│   ├── gui/
│   └── main.cpp
├── Common/
│   ├── protocol/
│   ├── security/
│   ├── config/
│   └── logging/
├── Tests/
├── docs/
│   └── DEVELOPMENT_SPEC.md
└── packaging/
```

## 22. 开发文档集与适用顺序

本项目不依赖任何特定 AI 开发工具。完整开发上下文由以下文件组成：

1. `docs/DEVELOPMENT_SPEC.md`：产品范围、业务行为和总体技术约束；
2. `docs/PROTOCOL_SPEC.md`：字节级协议、状态机、超时、并发和安全规则；
3. `docs/IMPLEMENTATION_PLAN.md`：目录、模块接口、Phase 0～6任务与完成定义；
4. `docs/CONFIG_SPEC.md`：配置文件位置、schema、默认值、验证和敏感信息落盘；
5. `docs/TEST_PLAN.md`：自动化、集成、故障注入、并发及Windows验收用例；
6. `docs/AI_HANDOFF_PROMPT.md`：可复制给任意代码大模型的统一开发指令。

发生冲突时，按以下优先级处理：

```text
用户最新明确决定
> DEVELOPMENT_SPEC.md 中的产品行为
> PROTOCOL_SPEC.md 中的通信行为
> IMPLEMENTATION_PLAN.md 中的实现建议
> TEST_PLAN.md 中的测试细节
```

不得静默选择冲突项。无法从上述优先级解决时，停止相关功能开发并提出一个具体问题；不相关模块可以继续开发。

## 23. 通用首个开发指令

```text
请先按顺序完整阅读：
- docs/DEVELOPMENT_SPEC.md
- docs/PROTOCOL_SPEC.md
- docs/IMPLEMENTATION_PLAN.md
- docs/CONFIG_SPEC.md
- docs/TEST_PLAN.md

这些文件构成完整需求，不要依赖聊天记录或自行补充产品行为。
按照 IMPLEMENTATION_PLAN.md 分阶段实施，不要一次性实现整个项目。
先完成 Phase 0 和 Phase 1：建立 C++17/CMake 工程，实现 RemoteTool TCP Server、Windows Agent TCP Client、二进制帧编解码、设备注册、心跳、掉线检测和自动重连。

要求：
- 不添加 fallback logic；
- 不实现兼容层和非目标功能；
- 网络、协议和GUI分离；
- 对非法协议输入明确失败；
- 为帧拆包、粘包、非法长度和心跳超时编写测试；
- 完成后给出构建命令、运行方法和验收结果。
```
