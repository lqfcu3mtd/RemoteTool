# Remote Maintenance Tunnel Tool

轻量化反向TCP隧道工具。公司工程师通过本机SSH客户端或浏览器访问RemoteTool的回环端口，RemoteTool经Agent反向连接转发到现场设备。

当前仓库处于 Phase 0 开发期：平台无关核心层（RMT/1 帧编解码）已实现并通过单元测试（49/49，MinGW + MSVC 双端验证）；Win32 GUI / DPAPI / mbedTLS 待后续 Phase。

开发者或代码大模型请从以下文件开始（按顺序）：

0. [STATUS.md](STATUS.md)：**当前进度**（进度的唯一真实来源）
1. [docs/INSTALL.md](docs/INSTALL.md)：环境准备与构建安装说明
2. [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md)：环境验证记录
3. [docs/README.md](docs/README.md)：文档索引
4. [docs/DEVELOPMENT_SPEC.md](docs/DEVELOPMENT_SPEC.md)：产品规格
5. [docs/PROTOCOL_SPEC.md](docs/PROTOCOL_SPEC.md)：协议规范
6. [docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md)：实施计划
7. [docs/CONFIG_SPEC.md](docs/CONFIG_SPEC.md)：配置规范
8. [docs/TEST_PLAN.md](docs/TEST_PLAN.md)：测试计划
9. [docs/CODING_STANDARDS.md](docs/CODING_STANDARDS.md)：代码规范
10. [docs/AI_HANDOFF_PROMPT.md](docs/AI_HANDOFF_PROMPT.md)：AI 交接提示词

> 接手后先跑 `bash tools/devcheck.sh` + `bash tools/msvc-check.sh` 验证环境。

不得仅凭本README开始实现；完整需求以上述文档为准。
