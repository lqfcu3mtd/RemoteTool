# 多 Agent 开发流程

版本：1.0
日期：2026-07-18
维护者：RemoteTool 团队

本文档定义 RemoteTool 项目的多 agent 协作开发流程。任何 agent/模型接手后按此流程推进。

## 1. 角色分工

| 角色 | 模型 | 职责 | 工具 |
|---|---|---|---|
| **主线程**（协调者） | GLM-5.2 | 需求分析、任务拆解、架构设计、代码审查、提交、状态管理 | 全部工具（TaskCreate/Agent/Read/Edit/Bash/Git） |
| **子线程**（执行者） | 编码能力强的模型 | 实现具体代码、写测试、修 bug | Agent 工具生成，可选 model: reasoning |

### 主线程职责（不外包）

1. **读规格** — 理解需求，确定实现范围
2. **拆任务** — 把 Phase 拆成原子任务（1-3 文件，有明确验收标准）
3. **写 prompt** — 为每个子线程写自包含的任务描述
4. **派任务** — 用 Agent 工具生成子线程
5. **审代码** — 读子线程产出的 diff，对照 CODING_STANDARDS.md 检查
6. **跑测试** — `bash tools/devcheck.sh` + `bash tools/msvc-check.sh`
7. **提交** — 按提交粒度（§13）拆小提交，git commit + push
8. **更状态** — 更新 STATUS.md

### 子线程职责（被分配）

1. **读 prompt** — 理解任务范围和验收标准
2. **读上下文** — 读指定文件（spec / 已有代码）
3. **写代码** — 按规范实现
4. **跑测试** — 用 devcheck.sh 自验
5. **回报** — 返回做了什么、改了哪些文件、测试结果

## 2. 任务拆解原则

### 粒度

| 粒度 | 范围 | 模型选择 | 示例 |
|---|---|---|---|
| **小** | 1 文件，接口清晰 | default | "实现 `error_code.h`，按 PROTOCOL_SPEC §7 定义稳定错误码" |
| **中** | 2-3 文件，含测试 | reasoning | "实现 `target_whitelist.h/cpp` + `target_whitelist_test.cpp`，CIDR + 端口匹配" |
| **大** | 跨模块，状态机 | reasoning | "实现 `TunnelConnection` 状态机 + 重连退避 + 测试" |

- 太小（改一行）→ 主线程自己做，不派子线程
- 太大（整个 Phase）→ 先拆成中/小任务，再派

### 自包含 prompt 模板

子线程没有对话上下文，prompt 必须自包含：

```text
你在开发 RemoteTool 项目（C++17 反向 TCP 隧道工具）。

## 任务
实现 <具体功能>，文件路径：<文件>

## 必读上下文
1. docs/CODING_STANDARDS.md — 代码规范
2. <相关 spec 文件及章节>
3. <已有代码文件路径>

## 验收标准
1. <具体的接口/行为要求>
2. 编译通过：bash tools/devcheck.sh
3. 测试通过：<具体测试要求>

## 禁止
- 不实现文档未要求的功能
- 不添加 fallback / 静默降级
- 不修改本任务范围外的文件
```

## 3. 执行流程

```
主线程                          子线程
  │
  ├─ 1. 读规格，拆任务
  ├─ 2. TaskCreate（任务清单）
  ├─ 3. 写 prompt
  │
  ├─ 4. Agent(派任务) ──────────→ 5. 读 prompt + 上下文
  │                                 6. 写代码
  │                                 7. 跑 devcheck.sh 自验
  │                                 8. 回报结果
  ├─ 9. 审查 diff ←──────────────
  ├─ 10. 跑 devcheck.sh + msvc-check.sh
  ├─ 11. git commit（按提交粒度）
  ├─ 12. 更新 STATUS.md
  │
  └─ 重复 3-12 直到 Phase 完成
```

## 4. 并行与串行

| 场景 | 策略 | 示例 |
|---|---|---|
| **独立模块** | 并行（单消息多个 Agent 调用） | target_whitelist + config_schema + log_interface |
| **有依赖** | 串行（前一个完成后才派下一个） | frame（已有）→ session（依赖 frame）→ tunnel（依赖 session） |
| **审查阻塞** | 等审查通过再派下一个 | 不信任的子线程产出，必须先审 |

## 5. 质量门

每个子线程产出后，主线程必须过这道门：

1. [ ] **编译** — `bash tools/devcheck.sh` 通过（MinGW）
2. [ ] **MSVC** — `bash tools/msvc-check.sh` 通过（如涉及平台无关代码）
3. [ ] **规范** — 对照 CODING_STANDARDS.md 检查命名/命名空间/头文件/RAII
4. [ ] **测试** — 新增测试覆盖核心路径和边界
5. [ ] **无假实现** — 没有 fallback / 空实现 / 永远成功 / 吞错误
6. [ ] **安全** — 不打印密钥 / SESSION_DATA；所有输入已验证

不过门 → 打回子线程修，或主线程自己修。

## 6. 模型选择建议

| Agent 工具 model 参数 | 适用场景 |
|---|---|
| `reasoning` | 复杂算法、状态机、协议逻辑、并发设计 |
| `default` | 简单实现、配置解析、工具函数、测试编写 |
| `lite` | 文件搜索、简单查找、格式化 |

> 注意：当前 Agent 工具的 model 选项为 default / lite / reasoning。若需要其他更强编码模型（如 Claude / GPT-4），需在 WorkBuddy 设置中切换默认模型或使用外部 IDE。

## 7. 提交节奏

按 IMPLEMENTATION_PLAN §13，每个 Phase 拆 5 个小提交：

```text
phaseN: add interfaces and tests     ← 子线程产出
phaseN: implement happy path          ← 子线程产出
phaseN: implement error handling      ← 子线程产出
phaseN: add integration tests        ← 子线程产出
phaseN: update docs and status       ← 主线程做
```

每个提交必须可编译，禁止超大提交。主线程负责 git commit + push，子线程只负责写代码。

## 8. 状态同步

- **STATUS.md** 是进度唯一真实来源，每个提交后更新
- **Git 提交历史** 是代码变更记录
- **TaskCreate/TaskUpdate** 是任务跟踪工具
- 三者保持一致：STATUS.md 说的 = Git 里的 = TaskList 里的
