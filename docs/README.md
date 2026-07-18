# 远程维护隧道工具文档索引

本目录是项目的完整、工具无关开发依据。接手者不需要阅读历史聊天记录。

## 必读顺序

1. [DEVELOPMENT_SPEC.md](DEVELOPMENT_SPEC.md)：产品目标、范围、最终形态和非目标。
2. [PROTOCOL_SPEC.md](PROTOCOL_SPEC.md)：精确通信协议、状态机、安全与并发规则。
3. [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)：模块边界、阶段任务和完成定义。
4. [CONFIG_SPEC.md](CONFIG_SPEC.md)：文件位置、JSON字段、默认值和密钥落盘。
5. [TEST_PLAN.md](TEST_PLAN.md)：测试矩阵和发布验收。
6. [AI_HANDOFF_PROMPT.md](AI_HANDOFF_PROMPT.md)：交给代码大模型时使用的统一提示词。

## 已确认的关键决策

| 项目 | 决定 |
|---|---|
| RemoteTool平台 | Windows 10/11 x64 |
| 第一版Agent | Windows x64 |
| 后续Agent | Linux/OpenWrt，按CPU和SDK分别构建 |
| 连接方向 | Agent主动连接RemoteTool |
| 访问方式 | 工程师使用已有SSH客户端和浏览器 |
| 本地映射 | `127.0.0.1:端口`，不创建虚拟网卡 |
| 协议 | 单条加密长连接中的多路TCP Session |
| 并发 | 一个映射允许多个SSH/Web TCP连接 |
| GUI | RemoteTool使用Win32；Agent仅最小状态窗口 |
| 安全 | TLS-PSK、长期设备密钥、动态会话密钥 |
| 首次配对 | Demo允许一次性短码；正式部署应使用高熵引导密钥或PAKE |
| 非目标 | 内置终端、内置浏览器、VPN、云中继、文件管理、UDP |

## 开发执行规则

- 一次只推进一个Phase；
- 每个Phase先测试再标记完成；
- 不因后续Phase尚未实现而添加假实现或静默fallback；
- 不改变协议字段、默认端口或安全规则来绕过失败测试；
- 每次交付说明已完成、测试结果、剩余范围和已知限制；
- 任何模型开始编码前都必须读取上述四份规范文件。

## 环境准备

- 工具链安装、构建与测试验证见 [INSTALL.md](INSTALL.md)。
- 开发期使用免管理员 MinGW-w64（D 盘）验证平台无关核心层；生产构建需 VS2022 + MSVC。
