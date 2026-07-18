# 通用AI开发交接提示词

下面的提示词可以复制给 Codex、Claude Code、Cursor、Gemini CLI 或其他具备仓库读写和命令执行能力的代码模型。

## 首次开始项目

```text
你正在开发 Remote Maintenance Tunnel Tool。

在修改任何代码之前，按顺序完整阅读：
1. docs/README.md
2. docs/DEVELOPMENT_SPEC.md
3. docs/PROTOCOL_SPEC.md
4. docs/IMPLEMENTATION_PLAN.md
5. docs/CONFIG_SPEC.md
6. docs/TEST_PLAN.md

这些文件是完整需求来源。不要依赖历史聊天，不要自行扩展产品范围。

开发规则：
- 严格按 IMPLEMENTATION_PLAN.md 的 Phase 顺序推进；
- 当前只实施我指定的Phase；
- 先检查仓库现状和已有改动，不覆盖用户已有内容；
- 实现功能后运行该Phase要求的测试；
- 不添加fallback、旧协议兼容、静默降级或未要求功能；
- 不以空实现、永远成功、忽略错误或放宽测试来通过验收；
- 协议线上格式必须完全符合 PROTOCOL_SPEC.md；
- 所有Socket、线程、Timer和Windows句柄必须可靠释放；
- 不在日志中记录PSK、配对密钥或Session数据；
- 如果文档存在冲突，按 docs/DEVELOPMENT_SPEC.md 中的优先级处理；
- 只有会改变产品行为且无法由文档解决的问题才询问用户；其他实现细节自行作出保守、简单、可测试的决定并记录。

每完成一个Phase：
1. 运行构建和全部相关测试；
2. 更新 STATUS.md；
3. 报告已完成内容、实际测试命令和结果、已知限制、下一Phase；
4. 保持仓库处于可继续开发的状态。

现在先检查仓库，然后实施 Phase 0。不要开始 Phase 1，直到Phase 0的完成定义全部满足。
```

## 自动连续推进Phase 0～6

```text
你正在开发 Remote Maintenance Tunnel Tool。先完整阅读 docs 目录中的：
README.md、DEVELOPMENT_SPEC.md、PROTOCOL_SPEC.md、IMPLEMENTATION_PLAN.md、CONFIG_SPEC.md、TEST_PLAN.md。

在不需要产品决策的情况下，自动依次推进 Phase 0 到 Phase 6。每个Phase必须：
- 先检查前一Phase完成定义；
- 实现当前Phase；
- 运行当前Phase测试和已有回归测试；
- 修复失败后再次验证；
- 更新 STATUS.md 并形成清晰检查点；
- 通过后才进入下一Phase。

不要因为自动推进而跳过测试、合并Phase或降低验收标准。不要实现Phase 7或非目标功能。

以下情况暂停并提出一个具体问题：
- 规范存在无法按优先级解决的矛盾；
- 需要用户提供Windows签名证书、实际设备SDK或外部凭据；
- 继续操作会覆盖用户未提交的冲突改动；
- 安全库不支持规范要求的密码套件，且替代会改变安全属性；
- 当前环境无法进行Windows实机验收。此时先完成所有可在当前环境验证的工作，并明确列出待Windows执行的命令与用例。

不要在一般实现细节上等待用户。选择最简单、可测试、符合规范的实现，并在 STATUS.md 记录决定。
```

## 继续未完成项目

```text
请先读取 docs/README.md、全部规范文件、STATUS.md、git状态和最近提交。
确定最后一个真正满足完成定义的Phase，以及当前工作区未完成的改动。

从未完成的最早Phase继续，不要重写已经通过测试的模块，不要假设STATUS.md声明完成就一定完成；用构建和测试验证。

完成当前Phase后运行回归测试并更新STATUS.md。若无产品级阻塞，继续下一Phase，最多推进到Phase 6。
```

## 代码审查提示词

```text
请依据 docs/DEVELOPMENT_SPEC.md、docs/PROTOCOL_SPEC.md、docs/IMPLEMENTATION_PLAN.md、docs/CONFIG_SPEC.md 和 docs/TEST_PLAN.md 审查当前实现。

优先寻找：
1. 协议字节格式和状态机偏差；
2. Session数据串线、竞态、生命周期和半关闭错误；
3. 无界队列、缺少背压、队头阻塞；
4. 认证绕过、明文降级、密钥泄漏和目标白名单绕过；
5. Socket、Timer、线程、Windows句柄泄漏；
6. GUI线程直接执行网络或被工作线程直接访问控件；
7. 测试缺口或通过降低断言掩盖缺陷；
8. 实现了非目标功能导致复杂度上升。

先输出按严重度排序、带文件和行号的发现；再给最小修复方案。不要只做风格评价，不要在没有明确授权时大规模重构。
```

## Windows验证提示词

```text
当前目标是Phase 6 Windows验证。请先读取 docs/TEST_PLAN.md 的Windows人工验收和发布阻断条件。

执行：
- Windows x64 Debug/Release配置、构建和CTest；
- RemoteTool与Agent绿色目录打包；
- 多SSH连接；
- Web多窗口和多次刷新；
- 断网恢复与进程重启；
- 配置持久化和普通用户运行；
- TLS抓包明文检查；
- 8小时稳定性测试（如果当前运行时允许持续监控）。

记录真实命令和结果。无法执行的项目必须标记为“未验证”，不能写成通过。最终按 TEST_PLAN.md 模板生成测试报告。
```
