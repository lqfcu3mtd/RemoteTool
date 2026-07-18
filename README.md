# Remote Maintenance Tunnel Tool

轻量化反向TCP隧道工具。公司工程师通过本机SSH客户端或浏览器访问RemoteTool的回环端口，RemoteTool经Agent反向连接转发到现场设备。

当前仓库处于 Phase 0 开发期：平台无关核心层（RMT/1 帧编解码）已实现并通过单元测试（验证方式见 [docs/INSTALL.md](docs/INSTALL.md)）；Win32 GUI / DPAPI / mbedTLS 待 VS2022 生产构建验收。

开发者或代码大模型请从以下文件开始：

0. [docs/INSTALL.md](docs/INSTALL.md)：环境准备与构建安装说明（先按此搭好工具链）
1. [docs/README.md](docs/README.md)
2. [docs/DEVELOPMENT_SPEC.md](docs/DEVELOPMENT_SPEC.md)
3. [docs/PROTOCOL_SPEC.md](docs/PROTOCOL_SPEC.md)
4. [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md)
5. [docs/CONFIG_SPEC.md](docs/CONFIG_SPEC.md)
6. [docs/TEST_PLAN.md](docs/TEST_PLAN.md)
7. [docs/AI_HANDOFF_PROMPT.md](docs/AI_HANDOFF_PROMPT.md)

不得仅凭本README开始实现；完整需求以上述文档为准。
