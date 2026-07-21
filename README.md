# Remote Maintenance Tunnel Tool

轻量化反向 TCP 隧道工具。公司工程师通过本机 SSH 客户端或浏览器访问 RemoteTool 的回环端口，RemoteTool 经 Agent 反向连接转发到现场设备。

**当前状态：Phase 6（发布与验证）进行中。端到端端口转发已在本机验证通过；目标白名单已在 Agent 侧生效。链路目前为明文 TCP（server-side TLS 未实现），仅限受信任局域网测试。** 详见 [STATUS.md](STATUS.md)（进度唯一真实来源）。

## 绿色发布

发行包仅 2 个 exe，输出到固定路径 `D:\coding\RemoteTool\dist\`：

```
dist/
├── remote_tool.exe    ~1.5 MB   （服务端，工程师机器）
├── agent.exe          ~1.5 MB   （客户端，现场机器）
└── README.md
```

无 MinGW 运行时依赖（仅 KERNEL32/USER32/GDI32/WS2_32/UCRT 系统 DLL），复制到目标机器即可运行。

### 构建

```bash
bash tools/build-release.sh   # 2026-07-19 已验证：MinGW Release 构建 + strip + DLL 依赖检查
```

### 开发构建与测试

```bash
cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw
# 16/16 通过，零编译警告（需把 D:\tools\mingw64\bin 加入 PATH）
# 输出目录：build-dev/bin/  （remote_tool.exe, agent_windows.exe, 测试 exe 等）

python tools/smoke_e2e.py     # 端到端冒烟：echo 回显 sha256 校验，自动清理
```

## 使用

**RemoteTool**（公司工程师机器）：
1. 双击 `remote_tool.exe`（首次运行生成 `remote_tool.json`，默认监听 0.0.0.0:4433）
2. 点 Devices 区 **+ Add** 添加设备，选中设备后在配对码面板查看/复制配对码（10 分钟有效；当前为 UI 层 stub，PSK 配对协议接通前仅作标识）
3. 点 Port mappings 区 **+ Add** 添加映射（名称/设备/本地端口/目标地址），状态 Running 即开始监听
4. 本机连接 `127.0.0.1:<本地端口>` 即可访问现场目标；Start/Stop/Edit/Delete 实时生效
5. `Settings...`（工具栏或 Edit 菜单）可改监听地址/端口、心跳超时、每映射会话上限（端口变更需重启生效）

**Agent**（现场机器）：
1. 复制 `agent.exe` 到现场机器任意目录，首次双击在同目录生成 `agent.json`
2. 点 `Settings...`：Device ID 改成工程师给的 ID、Server 改成 RemoteTool 机器的 IP
3. **编辑 agent.json 的 `target_policy` 白名单**（见下；空白名单 = 全部拒绝，映射不会转发）
4. 大号状态文字显示绿色 `Online` 即连接成功

> 注意：Agent 的 Settings 对话框目前不含 `target_policy` 字段，点 OK 会用手改前的内存配置覆盖 agent.json——修改白名单后请直接重启 Agent，不要点 Settings 的 OK。

### 白名单（agent.json target_policy）

Agent 侧强制执行，RemoteTool 无法绕过。目标是 IP 字面量 + 端口在 `allowed_ports` + IP 落在 `allowed_cidrs` 才放行；域名一律拒绝。

```json
"target_policy": {
  "allowed_cidrs": ["127.0.0.0/8", "192.168.0.0/16"],
  "allowed_ports": [22, 80, 443],
  "allow_ipv6": false
}
```

### 配置文件

| 文件 | 在哪台机器 | 内容 |
|---|---|---|
| `remote_tool.json` | RemoteTool 机器 | 监听端口、心跳、并发上限（GUI Settings 可编辑） |
| `devices.json` | RemoteTool 机器 | 已添加的设备（ID + 显示名） |
| `mappings.json` | RemoteTool 机器 | 端口映射（名称、设备、本地入口、远端目标） |
| `agent.json` | Agent 机器 | Device ID、Server 地址、`target_policy` 白名单 |

## 开发者起点

按顺序阅读：

0. [STATUS.md](STATUS.md)：**当前进度**（进度的唯一真实来源）
1. [docs/INSTALL.md](docs/INSTALL.md)：环境准备与构建安装说明
2. [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md)：环境验证记录
3. [docs/README.md](docs/README.md)：文档索引
4. [docs/DEVELOPMENT_SPEC.md](docs/DEVELOPMENT_SPEC.md)：产品规格
5. [docs/PROTOCOL_SPEC.md](docs/PROTOCOL_SPEC.md)：协议规范
6. [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md)：实施计划
7. [docs/CONFIG_SPEC.md](docs/CONFIG_SPEC.md)：配置规范
8. [docs/TEST_PLAN.md](docs/TEST_PLAN.md)：测试规范
9. [docs/CODING_STANDARDS.md](docs/CODING_STANDARDS.md)：代码规范
10. [docs/AI_HANDOFF_PROMPT.md](docs/AI_HANDOFF_PROMPT.md)：AI 交接提示词（kimi-code 版）

> 接手后先跑 `cmake --preset dev-mingw && cmake --build --preset dev-mingw && ctest --preset dev-mingw` + `python tools/smoke_e2e.py` 验证环境。

不得仅凭本 README 开始实现；完整需求以上述文档为准。

## 体积优化要点

- **mbedTLS 瘦身**：仅启用 PSK + AES-128-GCM（spec §8）。自定义 `third_party/mbedtls/include/mbedtls/config_rmt.h`，关闭 RSA / X.509 / PEM / CERTS / ARIA / CAMELLIA / DES / BLOWFISH / ARC4 / MD2 / MD4 / MD5 / SHA1 / SHA512 / RIPEMD160 / GENPRIME / DHM / ECDH / ECDSA 等。
- **编译开关**：`-Os -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,-s`（MinGW Release），`/Os /Gy /GL /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE`（MSVC Release）。
- **运行时静态链接**：MinGW Release 链接时加 `-static -static-libgcc -static-libstdc++`。
- **strip 符号**：`strip -s`（MinGW），`/DEBUG:NONE`（MSVC）。

## 安全声明

- 仅在公司内部受信任局域网中运行；远程设备 IP 直连 RemoteTool，不经过公网。
- TLS-PSK client path 已完成（mbedTLS 瘦配置），**server-side TLS 未实现，当前构建为明文 TCP 链路**；Agent 侧目标白名单已强制执行（空白名单全拒）。
- **不要在生产环境使用当前的明文模式构建**；等 PSK 配对协议接通后再发布。
