# 代码规范

版本：1.0
日期：2026-07-18
维护者：RemoteTool 团队

本文档是 RemoteTool 项目的代码规范，所有贡献者（含 AI agent）必须遵守。

## 1. 命名约定

| 元素 | 约定 | 示例 |
|---|---|---|
| 文件名 | `snake_case.h` / `snake_case.cpp` | `frame.h`、`frame.cpp`、`error_code.h` |
| 类 / 结构体 / 枚举 | `PascalCase` | `FrameDecoder`、`FrameHeader`、`MessageType` |
| 函数 / 方法 / 变量 | `snake_case` | `encode_frame()`、`session_id` |
| 常量 / 枚举值 | `kPascalCase` / `UPPER_SNAKE` | `kHeaderSize`、`SESSION_DATA` |
| 命名空间 | `snake_case` | `rmt::protocol` |
| 宏 | `UPPER_SNAKE` | `RMT_PROTOCOL_FRAME_H`（`#pragma once` 优先） |

## 2. 命名空间结构

```text
rmt                  — 顶层
├── rmt::common      — 错误码、日志、类型定义、工具
├── rmt::protocol    — RMT/1 帧编解码、协议常量
├── rmt::config      — 配置 schema、原子写入、JSON 校验
├── rmt::security    — SecretStore 接口、DPAPI 实现
├── rmt::tunnel      — TunnelConnection、连接状态机
├── rmt::session     — SessionManager、背压、半关闭
├── rmt::mapping     — MappingListener、端口映射
└── rmt::platform    — Win32 / DPAPI 平台隔离层
```

- 每个命名空间对应 `include/rmt/<name>/` 和 `src/<name>/` 目录
- 公共 API 在 `include/rmt/` 下，私有实现细节在 `src/` 下
- **核心库 `rmt_core` 不得依赖 `apps/*/gui`**

## 3. 头文件规则

- 使用 `#pragma once`（不使用 include guard）
- 最小 include 原则：能用前置声明就不 `#include`
- 公共头文件只放 `include/rmt/`，私有头放 `src/`
- 头文件中不写 `using namespace`，不定义非模板函数
- `#include` 顺序：对应头 → 标准库 → 第三方 → 项目内（按字母序）

## 4. 平台隔离

- Win32 API / DPAPI 调用**仅在** `src/platform/` 和 `include/rmt/platform/` 层
- 核心层通过接口抽象访问平台能力：

```cpp
// include/rmt/security/secret_store.h
class SecretStore {
public:
    virtual ~SecretStore() = default;
    virtual Result<ProtectedSecret> protect(std::span<const std::uint8_t> plaintext) = 0;
    virtual Result<std::vector<std::uint8_t>> unprotect(const ProtectedSecret& stored) = 0;
};

// 生产实现：DPAPI
// 开发实现：文件（明文，仅用于无 DPAPI 环境的单元测试）
```

- `#ifdef _WIN32` **只在** `platform/` 层使用，不得出现在核心层
- 核心层（`protocol`、`config`、`session` 等）必须能在 MinGW 和 MSVC 下编译

## 5. 错误处理

- 公共 API 使用 `ErrorCode`（稳定错误码），不抛异常到公共接口
- 安全或协议不变量失败时**立即失败**（断开连接 / 拒绝请求）
- 错误必须包含稳定错误码和可理解上下文
- **禁止**：空实现、永远成功、吞掉错误、放宽测试来通过验收
- 详见 `include/rmt/common/error_code.h` 和 [PROTOCOL_SPEC.md §7](PROTOCOL_SPEC.md)

## 6. 资源管理（RAII）

- 所有 Socket、Timer、线程、Windows 句柄使用 RAII（构造获取、析构释放）
- **禁止** `detach` 线程
- **禁止** 在异步回调中捕获可能失效的裸 `this`
- **禁止** 跨线程直接操作 Win32 控件（通过线程安全事件队列）
- **禁止** 忙轮询
- **禁止** 通过无限队列隐藏背压问题
- Tunnel 所有写操作必须串行化，禁止多线程直接并发写同一个 Socket

## 7. 编译警告

| 编译器 | 标志 | 要求 |
|---|---|---|
| MSVC | `/W4` | 新代码零新增警告 |
| MinGW | `-Wall -Wextra` | 新代码零新增警告 |
| Release | `/O2` (MSVC) / `-O2` (MinGW) | 去除调试符号 |

## 8. 日志安全

- **禁止**在日志中记录 PSK、配对密钥、SESSION_DATA 内容
- 日志可记录元数据（SessionId、字节数、时间戳、错误码）
- 日志支持 INFO / WARN / ERROR 级别

## 9. 输入验证

- 所有外部长度、端口、地址、JSON 字段必须验证
- JSON 解析必须严格：拒绝注释、尾逗号、未知字段、错误类型
- 配置文件损坏时明确报错，不自动覆盖
- 目标白名单：CIDR + 端口双匹配，空白名单 = 全拒绝，禁止环回

## 10. Git 提交规范

### 10.1 提交粒度

每个 Phase 拆为小提交（来自 IMPLEMENTATION_PLAN §13）：

```text
phaseN: add interfaces and tests
phaseN: implement happy path
phaseN: implement error handling
phaseN: add integration tests
phaseN: update docs and status
```

- 每个提交必须可编译
- 禁止把整个 Phase 堆在一个无法审查的超大提交中

### 10.2 提交信息格式

```text
phaseN: <动词短语描述变更>

<可选正文：为什么、影响范围>
```

- `phaseN` 对应 Phase 编号（phase0, phase1, ...）
- 动词短语用英文：add / implement / fix / test / update / refactor

### 10.3 分支策略

- `main`：始终可编译、测试通过
- 开发在 `dev` 或 `phaseN-<feature>` 分支上进行
- 每个 Phase 完成后合并到 `main`

## 11. 测试要求

- 每个 Phase 完成后运行该 Phase 要求的全部测试（见 IMPLEMENTATION_PLAN）
- 测试使用 CTest + 轻量 C++ 测试框架（自写 `include/rmt/test.h`）
- 单元测试在 `tests/unit/`，集成测试在 `tests/integration/`
- 测试必须在两个编译器下均通过

## 12. 第三方依赖

- 依赖版本在 `cmake/Dependencies.cmake` 中固定
- **禁止**使用无版本约束的 `main`、`master` 或 `latest`
- 生成 `THIRD_PARTY_NOTICES.md` 记录许可证
- 默认构建不从网络获取浮动最新版依赖
