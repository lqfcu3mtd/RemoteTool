# 远程维护隧道工具实现计划

版本：1.0  
覆盖范围：Phase 0～6（Windows MVP）

## 1. 目标与完成定义

本计划把产品规格转为可提交、可测试的开发任务。任何开发工具完成一个Phase时，必须同时满足：

- 该Phase代码已提交到工程；
- 工程在支持环境中编译；
- 本Phase自动化测试通过；
- 没有用空实现、始终成功或吞掉错误的方式绕过测试；
- README包含构建和运行命令；
- `STATUS.md`更新已完成项、测试结果和已知限制。

## 2. 固定技术基线

### 2.1 构建环境

- C++17；
- CMake 3.24或更高；
- Visual Studio 2022 / MSVC x64；
- Windows SDK随Visual Studio安装；
- Release默认使用 `/O2`；
- 开发构建启用高警告级别 `/W4`；
- 新代码不得新增编译警告。

### 2.2 依赖原则

- GUI：Win32 API；
- Socket与定时器：standalone Asio；
- Windows实现使用Asio封装的Winsock，不再另外建立一套直接Winsock异步模型；
- 所有Tunnel、Listener、Session和Timer使用同一Asio `io_context`体系；
- GUI通过线程安全事件队列与网络层通信，不在GUI线程运行 `io_context`；
- JSON：轻量、固定版本的单头文件库，或项目内严格JSON实现；
- TLS：mbedTLS，版本固定并记录许可证；
- 测试：CTest加轻量C++测试框架；
- 第三方依赖必须锁定版本并生成 `THIRD_PARTY_NOTICES.md`；
- 默认构建不能在每次配置时从网络获取浮动最新版依赖。

如果代码仓库不能包含第三方源码，使用包管理器时必须提交锁文件并记录离线构建方法。

Phase 0必须在 `cmake/Dependencies.cmake` 中固定实际依赖版本，不允许使用无版本约束的 `main`、`master` 或 `latest`。安全库版本应选择实现时仍受支持的稳定版本，并在 `STATUS.md` 记录版本、来源和选择日期。

## 3. 目标目录

```text
RemoteMaintenance/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── STATUS.md
├── THIRD_PARTY_NOTICES.md
├── cmake/
├── apps/
│   ├── remote_tool/
│   │   ├── main.cpp
│   │   ├── gui/
│   │   └── resources/
│   └── agent_windows/
│       ├── main.cpp
│       └── gui/
├── src/
│   ├── common/
│   ├── protocol/
│   ├── tunnel/
│   ├── session/
│   ├── mapping/
│   ├── security/
│   ├── config/
│   └── platform/
├── include/rmt/
├── tests/
│   ├── unit/
│   ├── integration/
│   └── helpers/
├── docs/
└── packaging/
    ├── remote_tool/
    └── agent_windows/
```

核心库不得依赖 `apps/*/gui`。

## 4. 核心接口建议

命名可根据项目风格微调，但职责不得合并为单个巨型类。

### 4.1 协议

```cpp
struct FrameHeader {
    std::uint8_t version;
    MessageType type;
    std::uint16_t flags;
    std::uint32_t session_id;
    std::uint32_t payload_length;
};

struct Frame {
    FrameHeader header;
    std::vector<std::uint8_t> payload;
};

class FrameDecoder {
public:
    DecodeResult consume(ByteSpan bytes);
    void reset();
};

std::vector<std::uint8_t> encode_frame(const Frame& frame);
```

`FrameDecoder`必须增量消费任意分段输入，一次调用可产生0、1或多个帧。

### 4.2 Tunnel连接

```cpp
class TunnelConnection {
public:
    void start();
    void stop(DisconnectReason reason);
    void send_control(Frame frame);
    void send_session_data(SessionId id, ByteSpan bytes);
    TunnelState state() const;
};
```

同一Tunnel所有写操作必须串行化，禁止多个线程直接并发写同一个Socket。

### 4.3 Session

```cpp
class SessionManager {
public:
    OpenResult open(SessionId id, TargetEndpoint target);
    void on_remote_data(SessionId id, ByteSpan bytes);
    void on_half_close(SessionId id);
    void close(SessionId id, CloseReason reason);
    SessionStats stats(SessionId id) const;
};
```

### 4.4 Mapping

```cpp
struct MappingConfig {
    std::string id;
    std::string device_id;
    std::string name;
    std::uint16_t local_port;
    std::string target_host;
    std::uint16_t target_port;
    bool enabled;
};

class MappingListener {
public:
    StartResult start(const MappingConfig& config);
    void stop();
    MappingRuntimeState state() const;
};
```

### 4.5 Device仓库

```cpp
struct DeviceRecord {
    std::string id;
    std::string display_name;
    bool enabled;
    ProtectedSecret protected_psk;
};

class DeviceRepository {
public:
    Result add(DeviceRecord device);
    Result remove(std::string_view id);
    std::optional<DeviceRecord> find(std::string_view id) const;
    Result save();
};
```

## 5. Phase 0：工程初始化

### 任务

- 创建目标目录；
- 创建顶层CMake工程和两个空壳可执行程序；
- 创建 `rmt_core` 静态库；
- 配置 `/W4` 和Release优化；
- 实现日志接口、稳定错误码、范围退出清理工具；
- 实现JSON文件原子写入：临时文件写完并flush后替换；
- 创建CTest入口；
- 编写Windows本地构建说明；
- 创建 `STATUS.md`。

### 必须测试

- 配置文件不存在返回明确错误；
- 合法配置可读取；
- 非法JSON、错误字段类型、未知必填版本被拒绝；
- 日志不会崩溃且支持INFO/WARN/ERROR。

### 完成定义

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

三条命令成功，两个程序可以启动并正常退出。

## 6. Phase 1：基础TCP、帧协议与在线状态

### RemoteTool任务

- 启动Agent TCP监听器；
- 接受多个Agent连接；
- 增量读取和解码帧；
- 处理HELLO、HEARTBEAT；
- 维护设备在线状态和最后活动时间；
- 重复device_id时拒绝新连接；
- 心跳超时后关闭连接。

### Agent任务

- 读取server、port、device_id；
- 主动连接；
- 发送HELLO；
- 处理HELLO_ACK；
- 发送心跳；
- 断线后按规范退避重连；
- 用户退出时停止重连。

### 必须测试

- 协议测试向量；
- 每字节分片输入；
- 多帧一次输入；
- 最大合法帧；
- 超长、错误Magic、错误Version、非零Flags；
- HELLO顺序错误；
- 心跳正常、丢ACK、超时；
- 同device_id重复连接；
- 重连退避上下限。

### 完成定义

至少运行两个Agent进程，RemoteTool准确显示两台设备上线；终止其中一个后在超时内显示离线；重启后自动上线。

## 7. Phase 2：单Session端口转发

### 任务

- RemoteTool创建一个固定MappingListener；
- 接受一个本地TCP连接；
- 分配SessionId；
- 发送OPEN_SESSION；
- Agent检查目标策略并异步连接目标；
- 实现SESSION_OPENED/FAILED；
- 双向转发SESSION_DATA；
- 实现正常关闭、错误关闭和半关闭；
- 统计双向字节数。

### 必须测试

- 使用本地echo server进行双向转发；
- 0字节、1字节、16KiB、超过16KiB输入分帧；
- 目标拒绝、超时、不在白名单；
- 本地先关闭、目标先关闭；
- Tunnel中途断开；
- Session结束后资源释放。

### 完成定义

通过RemoteTool本地端口访问Agent侧echo server，并完成至少100 MiB随机数据双向校验，内容哈希一致。

## 8. Phase 3：多Session和多映射

### 任务

- 实现Session表和唯一ID分配；
- 一个MappingListener接受多个本地连接；
- 同一Agent支持多个映射；
- 控制队列优先和Session轮询数据队列；
- 实现高低水位背压；
- 实现单映射、单Agent并发上限；
- 界面数据模型提供活动连接数；
- Agent掉线统一关闭活动Session。

### 必须测试

- 同映射32个并发echo连接；
- 多映射总计128个连接；
- 一个慢消费者不阻塞其他Session心跳；
- 一个Session关闭不影响其他Session；
- 到达上限的新连接被拒绝；
- 达到缓冲上限触发暂停和恢复；
- 停止映射关闭其Session但不影响其他映射。

### 完成定义

- 三个SSH客户端可同时连接同一映射；
- 主流浏览器可通过Web映射正常加载包含多资源请求的测试页；
- 30分钟并发测试无崩溃、死锁和持续内存增长。

## 9. Phase 4：RemoteTool GUI与配置持久化

### RemoteTool GUI任务

- 主窗口；
- 设备列表；
- 映射列表；
- 添加/删除设备；
- 添加/编辑/删除/启动/停止映射；
- 显示在线状态、目标、本地端口、活动连接数和错误；
- 所有网络事件通过线程安全事件队列进入UI；
- UI关闭时执行有序停机。

### Windows Agent GUI任务

- 显示设备ID；
- 显示连接中、在线、认证失败、离线；
- 显示RemoteTool地址；
- 提供退出按钮；
- 不提供目标映射编辑。

### 持久化任务

- 配置版本字段；
- 配置路径、字段和默认值严格遵循 `CONFIG_SPEC.md`；
- 设备与映射原子保存；
- DPAPI保护设备密钥；
- 非敏感配置可读；
- 密钥不写日志；
- 配置损坏时明确报错，不自动覆盖。

### 完成定义

工程师不编辑JSON即可完成设备和映射管理；重启RemoteTool后所有配置恢复；运行中的映射按保存的enabled状态恢复。

## 10. Phase 5：TLS-PSK与首次配对

### 5A 长期密钥安全Tunnel（必须）

- 集成固定版本mbedTLS；
- RemoteTool按device_id查找32字节PSK；
- Agent读取DPAPI保护的PSK；
- 限定TLS版本和密码套件；
- 禁用空加密、匿名套件和弱算法；
- TLS失败明确记录稳定原因但不记录密钥；
- 移除Release中的明文入口。

### 5B 一次性配对（Demo）

- RemoteTool生成8位短码；
- 5分钟失效；
- 失败次数及来源频率限制；
- 配对成功生成32字节长期PSK；
- Agent保存长期PSK；
- 短码失效；
- Agent断开并用长期PSK重新连接。

配对消息和TLS identity必须严格遵循 `PROTOCOL_SPEC.md` 第15节，不得另行设计密钥下发流程。

必须在UI和README说明短码仅限受信任局域网。公网正式配对方案不在MVP中假装完成。

### 必须测试

- 正确PSK连接；
- 错误PSK拒绝；
- 设备禁用；
- 短码过期、重复使用、失败次数超限；
- 抓包中不存在HELLO、SESSION_DATA和密钥明文；
- 修改密文导致TLS连接失败；
- Release构建不能启用明文模式。

### 完成定义

没有有效长期PSK的Agent不能进入ONLINE；Demo配对成功后短码无法再次使用；所有端口转发在TLS内完成。

## 11. Phase 6：发布、稳定性和Windows验收

### 任务

- Windows 10 x64、Windows 11 x64实机验证；
- Debug与Release全量测试；
- 8小时稳定性测试；
- 反复断网/恢复100次；
- Agent和RemoteTool异常退出恢复；
- 端口冲突、配置只读、磁盘满等错误验证；
- Release去除调试符号；
- 生成两个绿色目录包；
- 编写用户README和故障排查说明；
- 生成SHA-256校验值；
- 检查第三方许可证。

### 发布包

```text
dist/
├── RemoteTool-windows-x64-VERSION.zip
├── Agent-windows-x64-VERSION.zip
└── SHA256SUMS.txt
```

### 完成定义

- 不安装额外运行环境即可启动；
- 不需要管理员权限；
- Agent局域网连接、设备上线、多SSH和Web映射均通过；
- 安全测试通过；
- 无已知阻断级和高严重度缺陷；
- 文档中准确列出剩余限制。

## 12. 代码质量约束

- 所有Socket、Timer、线程和句柄使用RAII；
- 禁止detach线程；
- 禁止跨线程直接操作Win32控件；
- 禁止在异步回调中捕获可能失效的裸 `this`；
- 所有外部长度、端口、地址和JSON字段必须验证；
- 不在日志中打印SESSION_DATA；
- 不使用忙轮询；
- 不通过无限队列隐藏背压问题；
- 错误必须包含稳定错误码和可理解上下文；
- 安全或协议不变量失败时立即失败；
- 不实现文档未要求的fallback、旧协议或自动降级。

## 13. 提交粒度建议

每个Phase拆为小提交：

```text
phaseN: add interfaces and tests
phaseN: implement happy path
phaseN: implement error handling
phaseN: add integration tests
phaseN: update docs and status
```

每个提交应可编译；禁止把整个Phase堆在一个无法审查的超大提交中。

## 14. 开发状态报告模板

```markdown
## 当前Phase

## 已完成
- ...

## 测试
- 命令：...
- 结果：...

## 已知限制
- ...

## 下一步
- ...

## 需要用户决定
- 无 / 一个具体问题
```
