# Remote Maintenance Tunnel Tool

轻量化反向 TCP 隧道工具。公司工程师通过本机 SSH 客户端或浏览器访问 RemoteTool 的回环端口，RemoteTool 经 Agent 反向连接转发到现场设备。

## 绿色发布（Phase 6）

整个发行包**仅 2 个 exe**：

```
dist/
├── remote_tool.exe    1.5 MB
└── agent.exe          446 KB
```

无 DLL 依赖（MinGW 运行时已静态链入）、无 .json / .dll / README 等附属文件，复制到目标机器即可运行。

### 构建

```bash
bash tools/build-release.sh
```

输出落在 `dist/`，自动 strip 符号 + 静态链接 mbedTLS 瘦配置（PSK + AES-128-GCM only）。

### 使用

**RemoteTool** (运行在公司工程师的机器)：
1. 双击 `remote_tool.exe`
2. 菜单 `Edit → Add device...`，记下生成的配对码（Phase 4 临时；Phase 5 接通真协议）
3. 把设备 ID + Server 地址告诉现场工程师

**Agent** (运行在现场的电脑)：
1. 第一次启动会自动生成 `agent.json` 默认配置（`AGENT001` / `127.0.0.1:4433`）
2. 点 `Settings...`，把 Device ID 改成工程师给的 ID、Server 改成 RemoteTool 机器的 IP
3. 状态显示 `Online` 即连接成功

> Phase 4 临时：`Settings` 写明文 `device_id` 到 `agent.json`；Phase 5 接通一次性配对码 + 长期 PSK 后会替换此流程。

### 配置

| 文件 | 在哪台机器 | 内容 |
|---|---|---|
| `remote_tool.json` | RemoteTool 机器 | 监听端口、并发上限、心跳超时 |
| `devices.json` | RemoteTool 机器 | 已添加的设备（ID + 显示名） |
| `mappings.json` | RemoteTool 机器 | 端口映射（名称、设备、本地入口、远端目标） |
| `agent.json` | Agent 机器 | Device ID、Server 地址、配对码/PSK |

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
10. [docs/AI_HANDOFF_PROMPT.md](docs/AI_HANDOFF_PROMPT.md)：AI 交接提示词

> 接手后先跑 `bash tools/devcheck.sh` + `bash tools/msvc-check.sh` 验证环境。

不得仅凭本 README 开始实现；完整需求以上述文档为准。

## 体积优化要点

- **mbedTLS 瘦身**：仅启用 PSK + AES-128-GCM（spec §8）。自定义 `third_party/mbedtls/include/mbedtls/config_rmt.h`，关闭 RSA / X.509 / PEM / CERTS / ARIA / CAMELLIA / DES / BLOWFISH / ARC4 / MD2 / MD4 / MD5 / SHA1 / SHA512 / RIPEMD160 / GENPRIME / DHM / ECDH / ECDSA 等。
- **编译开关**：`/Os -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,-s`（MinGW Release），`/Os /Gy /GL /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE`（MSVC Release）。
- **运行时静态链接**：MinGW Release 链接时加 `-static -static-libgcc -static-libstdc++`，把 GCC 运行时编入 exe。
- **strip 符号**：`strip -s`（MinGW），`/DEBUG:NONE`（MSVC）。

## 安全声明

- 仅在公司内部受信任局域网中运行；远程设备 IP 直连 RemoteTool，不经过公网。
- 正式 Demo 隧道启用 PSK 认证 + AES-128-GCM 加密（Phase 5 工作进行中；当前 MVP 仅做明文 TCP 链路验证）。
- **不要在生产环境使用本仓库当前的明文模式构建**；等 Phase 5 完成 PSK 配对协议后再发布。
