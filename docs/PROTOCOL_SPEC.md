# Remote Maintenance Tunnel Protocol Specification

版本：1.0  
协议代号：RMT/1  
状态：MVP冻结候选

## 1. 目的

本文件定义 RemoteTool 与 Agent 的线上协议。实现不得根据语言、平台或框架改变字节格式。

RMT/1只负责：

- Agent注册与在线状态；
- 心跳；
- 创建、转发和关闭TCP Session；
- 协议级错误通知。

RMT/1不解析SSH、HTTP或其他上层协议，所有业务数据均作为不透明字节转发。

## 2. 传输层

- 底层：TCP；
- 最终安全层：TLS 1.2 PSK；
- 一条Agent连接同时承载控制消息和多个Session；
- TCP keepalive只能作为补充，在线判断以RMT心跳为准；
- 禁止使用UDP；
- 禁止在最终发布版中自动降级为明文TCP。

Phase 1～3允许通过编译选项生成仅供本机或隔离局域网测试的明文构建。明文构建文件名必须包含 `insecure-dev`，且Release发布流程必须拒绝打包该构建。

## 3. 整数与字符串编码

- 所有多字节整数使用网络字节序（big-endian）；
- 字符串使用UTF-8，不带NUL结尾；
- JSON控制Payload必须为UTF-8对象；
- 布尔值必须使用JSON `true`/`false`；
- 不接受NaN、Infinity、注释或尾随逗号；
- IP目标在MVP中只接受字符串形式IPv4或IPv6字面量，不解析域名。

## 4. 帧格式

每个RMT帧由固定16字节头部和Payload组成：

| 偏移 | 字段 | 类型 | 大小 | 说明 |
|---:|---|---|---:|---|
| 0 | Magic | uint32 | 4 | 固定 `0x524D5431`，ASCII `RMT1` |
| 4 | Version | uint8 | 1 | 固定 `1` |
| 5 | Type | uint8 | 1 | 消息类型 |
| 6 | Flags | uint16 | 2 | V1必须为0 |
| 8 | SessionId | uint32 | 4 | 控制消息为0，Session消息非0 |
| 12 | PayloadLength | uint32 | 4 | Payload字节数 |

硬性限制：

- 头部必须完整读取后再解析Payload；
- `PayloadLength > 1,048,576` 时立即发送可发送的协议错误并断开；
- `SESSION_DATA` 单帧Payload不得超过16,384字节；
- V1收到非0 Flags必须视为协议错误；
- Magic或Version不正确必须直接断开；
- 不能通过扫描数据流寻找下一个Magic来“恢复同步”；
- 所有解析长度计算必须检查整数溢出。

## 5. 消息类型

| 值 | 名称 | SessionId | Payload |
|---:|---|---:|---|
| `0x01` | `HELLO` | 0 | JSON |
| `0x02` | `HELLO_ACK` | 0 | JSON |
| `0x03` | `HEARTBEAT` | 0 | JSON |
| `0x04` | `HEARTBEAT_ACK` | 0 | JSON |
| `0x05` | `PAIR_PROVISION` | 0 | JSON |
| `0x06` | `PAIR_PROVISION_ACK` | 0 | JSON |
| `0x10` | `OPEN_SESSION` | 非0 | JSON |
| `0x11` | `SESSION_OPENED` | 非0 | JSON |
| `0x12` | `SESSION_OPEN_FAILED` | 非0 | JSON |
| `0x13` | `SESSION_DATA` | 非0 | 原始字节 |
| `0x14` | `SESSION_HALF_CLOSE` | 非0 | JSON |
| `0x15` | `CLOSE_SESSION` | 非0 | JSON |
| `0x20` | `PROTOCOL_ERROR` | 0或相关ID | JSON |

配对使用临时TLS-PSK身份流程。长期密钥只能在该临时TLS连接内通过 `PAIR_PROVISION` 下发，不存在明文密钥消息。

## 6. 控制Payload Schema

下列字段均为必填，除非明确标记为可选。实现必须拒绝类型不正确、缺失必填字段或出现未知字段的消息。

### 6.1 HELLO

方向：Agent → RemoteTool。

```json
{
  "device_id": "SITE001",
  "agent_version": "0.1.0",
  "platform": "windows-x86_64",
  "protocol_version": 1,
  "instance_nonce": "dGVzdC1ub25jZQ"
}
```

规则：

- `device_id`：1～64字符，只允许ASCII字母、数字、`-`、`_`、`.`；
- `agent_version`：1～32字符；
- `platform`：1～32字符；
- `protocol_version`：必须为1；
- `instance_nonce`：Agent每次进程启动生成16字节随机数，使用无填充Base64URL编码。

### 6.2 HELLO_ACK

方向：RemoteTool → Agent。

```json
{
  "accepted": true,
  "server_version": "0.1.0",
  "heartbeat_interval_ms": 10000,
  "heartbeat_timeout_ms": 30000,
  "max_sessions": 128
}
```

`accepted=false`时，增加：

```json
{
  "accepted": false,
  "server_version": "0.1.0",
  "heartbeat_interval_ms": 10000,
  "heartbeat_timeout_ms": 30000,
  "max_sessions": 0,
  "error_code": "DEVICE_DISABLED",
  "message": "device is disabled"
}
```

Agent收到拒绝后必须断开。认证失败不得进入HELLO阶段。

### 6.3 PAIR_PROVISION

方向：RemoteTool → Agent。只允许出现在使用一次性配对PSK建立的临时TLS连接中。

```json
{
  "device_id": "SITE001",
  "device_key": "base64url-encoded-32-random-bytes",
  "issued_unix_ms": 1783872000000
}
```

规则：

- `device_key`解码后必须正好32字节；
- RemoteTool生成长期密钥后必须先持久化成功，再发送该消息；
- Agent必须使用DPAPI持久化成功后才能回复ACK；
- 任意一端持久化失败均视为配对失败；
- 该消息不得写入日志。

### 6.4 PAIR_PROVISION_ACK

方向：Agent → RemoteTool。

```json
{
  "stored": true
}
```

RemoteTool收到 `stored=true` 后使一次性配对码失效、关闭临时TLS连接。Agent随后使用长期设备密钥重新连接。`stored=false`时配对码是否继续有效由失败计数和过期时间决定，但已生成且未确认的长期密钥必须作废。

### 6.5 HEARTBEAT

方向：Agent → RemoteTool。

```json
{
  "sequence": 42,
  "sent_unix_ms": 1783872000000,
  "active_sessions": 3
}
```

### 6.6 HEARTBEAT_ACK

方向：RemoteTool → Agent。

```json
{
  "sequence": 42,
  "received_unix_ms": 1783872000012
}
```

### 6.7 OPEN_SESSION

方向：RemoteTool → Agent。

```json
{
  "mapping_id": "map-site001-ssh",
  "target_host": "192.168.1.1",
  "target_port": 22,
  "connect_timeout_ms": 10000
}
```

规则：

- `mapping_id`：1～64字符；
- `target_host`：IPv4或IPv6字面量；
- `target_port`：1～65535；
- `connect_timeout_ms`：1000～30000；
- Agent必须在建立连接前执行本地目标策略检查；
- 同一个SessionId不能再次OPEN。

### 6.8 SESSION_OPENED

方向：Agent → RemoteTool。

```json
{
  "mapping_id": "map-site001-ssh",
  "connected_host": "192.168.1.1",
  "connected_port": 22
}
```

### 6.9 SESSION_OPEN_FAILED

方向：Agent → RemoteTool。

```json
{
  "error_code": "TARGET_CONNECTION_REFUSED",
  "message": "target refused the connection"
}
```

### 6.10 SESSION_DATA

Payload是原始字节，不使用JSON。长度必须为1～16384。零长度DATA是协议错误。

### 6.11 SESSION_HALF_CLOSE

方向：任意。

```json
{
  "direction": "write"
}
```

含义：发送方不会再向该Session发送DATA；接收方应对目标Socket执行写半关闭，但仍可反向发送剩余数据。

### 6.12 CLOSE_SESSION

方向：任意。

```json
{
  "reason": "NORMAL",
  "message": "local peer closed"
}
```

收到后应幂等地释放该Session。对于已关闭但最近存在的Session，可忽略一次重复CLOSE；未知且从未存在的SessionId是协议错误。

### 6.13 PROTOCOL_ERROR

方向：任意。

```json
{
  "error_code": "INVALID_PAYLOAD",
  "message": "target_port is missing"
}
```

若错误破坏连接级状态，发送后立即关闭Tunnel；仅Session级错误可以只关闭相关Session。

## 7. 错误码

连接级：

- `UNSUPPORTED_VERSION`
- `INVALID_FRAME`
- `FRAME_TOO_LARGE`
- `INVALID_PAYLOAD`
- `UNEXPECTED_MESSAGE`
- `DEVICE_DISABLED`
- `DUPLICATE_DEVICE_CONNECTION`
- `HEARTBEAT_TIMEOUT`
- `INTERNAL_ERROR`

Session级：

- `SESSION_LIMIT_REACHED`
- `DUPLICATE_SESSION_ID`
- `MAPPING_NOT_FOUND`
- `TARGET_NOT_ALLOWED`
- `TARGET_CONNECT_TIMEOUT`
- `TARGET_CONNECTION_REFUSED`
- `TARGET_NETWORK_UNREACHABLE`
- `TARGET_IO_ERROR`
- `LOCAL_PEER_CLOSED`
- `AGENT_DISCONNECTED`
- `BUFFER_LIMIT_REACHED`
- `NORMAL`

日志可以保留平台原始错误码，但线上协议只发送上述稳定错误码及不含敏感数据的说明。

## 8. 连接状态机

### 8.1 Agent Tunnel

```text
DISCONNECTED
→ CONNECTING
→ TLS_HANDSHAKE
→ WAIT_HELLO_ACK
→ ONLINE
→ DISCONNECTED
```

额外终态：`STOPPING → STOPPED`。

规则：

- TCP连接后必须先完成TLS；
- TLS完成后Agent在5秒内发送HELLO；
- RemoteTool在5秒内回复HELLO_ACK；
- 未进入ONLINE前不允许任何Session消息；
- 一个device_id默认只允许一个ONLINE连接；
- 新连接与旧连接冲突时默认拒绝新连接，不抢占旧连接；
- Agent重连退避：1、2、4、8、16、30秒封顶，并加入±20%随机抖动；
- 认证错误进入30秒固定等待，避免快速重试；
- 用户主动退出不再重连。

### 8.2 Session

RemoteTool侧：

```text
LOCAL_ACCEPTED → OPEN_PENDING → RELAYING → HALF_CLOSED → CLOSING → CLOSED
                         ↘ OPEN_FAILED → CLOSED
```

Agent侧：

```text
OPEN_RECEIVED → TARGET_CONNECTING → RELAYING → HALF_CLOSED → CLOSING → CLOSED
                         ↘ OPEN_FAILED → CLOSED
```

OPEN_PENDING超过 `connect_timeout_ms + 2000` 未收到结果时，RemoteTool关闭本地连接并发送CLOSE。

## 9. Session ID

- 由RemoteTool生成；
- uint32，0保留给连接级消息；
- 在同一Tunnel生命周期中不得重复；
- 从1递增，溢出前必须重建Tunnel，不允许回绕复用；
- Session表删除后仍保留最近关闭ID的短期记录60秒，用于识别迟到CLOSE。

## 10. 心跳和在线判断

- Agent每10秒发送HEARTBEAT；
- RemoteTool立即返回相同sequence的ACK；
- 收到任意有效帧可以更新最后接收时间，但不能替代HEARTBEAT_ACK序列检查；
- 连续30秒没有收到有效帧，RemoteTool判定Agent离线；
- Agent连续30秒没有收到任何有效服务端帧，主动断开并重连；
- 系统睡眠恢复后按当前单调时钟重新判断，不批量补发心跳。

超时必须使用单调时钟；日志时间使用系统墙钟。

## 11. 并发、缓冲与背压

默认限制：

| 项目 | 默认值 |
|---|---:|
| 单映射活动Session | 32 |
| 单Agent活动Session | 128 |
| 单个SESSION_DATA | 16 KiB |
| 单Session待发送数据 | 256 KiB |
| 单Tunnel全部待发送数据 | 8 MiB |
| 控制消息队列 | 1024条 |

实现要求：

- 控制帧与数据帧分开排队；
- 每次写出前优先处理控制帧；
- 数据帧按Session轮询，禁止某Session长期独占Tunnel；
- 单Session达到256 KiB高水位后暂停对应源Socket读取；
- 降至128 KiB低水位后恢复读取；
- 全局达到8 MiB时暂停所有Session读取，并记录告警；
- 缓冲区限制持续30秒仍无法下降时关闭占用最大的Session，错误为 `BUFFER_LIMIT_REACHED`；
- 禁止通过丢弃Payload解决拥塞；
- GUI更新频率不高于每秒4次，避免连接计数高频刷新阻塞UI。

## 12. 映射监听行为

- 映射只监听 `127.0.0.1`；
- 一个映射对应一个本地监听端口和一个固定目标；
- 同一端口不能被两个映射同时占用；
- 映射启动时若端口被占用，状态进入ERROR并保留配置；
- Agent离线时映射可保持LISTENING，但新接入连接必须立即关闭，并在界面显示“设备离线”；
- Agent上线后无需重新创建映射即可接收新连接；
- 停止映射时停止accept，并关闭该映射所有活动Session；
- 编辑运行中的目标时，先停止映射；新配置只影响之后创建的Session。

## 13. 目标访问策略

Agent必须在本地执行目标白名单，RemoteTool不能绕过。

建议Agent配置：

```json
{
  "target_policy": {
    "allowed_cidrs": ["127.0.0.0/8", "192.168.0.0/16"],
    "allowed_ports": [22, 80, 443, 8080],
    "allow_ipv6": false
  }
}
```

MVP规则：

- 目标必须同时匹配CIDR和端口；
- 空白名单表示全部拒绝，不表示全部允许；
- 不接受域名；
- 禁止连接RemoteTool自身Agent监听端口，避免明显环路；
- Windows Gateway Agent由部署配置明确允许现场网段；
- Linux直连Agent正式默认仅允许 `127.0.0.0/8`。

## 14. TLS-PSK与密钥

最终Tunnel使用TLS 1.2 PSK，目标密码套件为：

```text
TLS_PSK_WITH_AES_128_GCM_SHA256
```

如所选TLS库或目标设备不支持该套件，必须在实现安全层前记录阻塞并确认替代套件，不能自动选择弱算法。

长期设备密钥：

- 最少32随机字节（256 bit）；
- Base64URL无填充编码保存；
- PSK identity使用 `device_id`；
- RemoteTool按identity查找对应PSK；
- 每次TLS握手动态派生会话密钥；
- 禁止所有设备共享同一个PSK。

Windows静态保存：

- RemoteTool和Windows Agent使用Windows DPAPI保护长期设备密钥；
- JSON只保存DPAPI密文的Base64，不保存明文PSK；
- 日志、错误提示和崩溃信息不得包含密钥。

Linux/OpenWrt后续版本：

- 优先使用设备已有安全存储；
- 无安全存储时密钥文件权限必须为0600；
- 进程内密钥不用后尽量清零；
- 不允许通过命令行参数传递密钥。

## 15. 一次性短码配对边界

6位或8位数字不具备长期PSK所需熵，只能用于Demo的受信任局域网首次配对。

Demo短码要求：

- 默认8位；
- 有效期5分钟；
- 只允许成功使用一次；
- 单设备连续5次失败后失效；
- 单来源每分钟最多5次尝试；
- 配对成功后生成32字节长期PSK，并要求Agent使用长期PSK重新建立Tunnel；
- UI明确标记为“局域网配对”；
- 禁止将短码模式用于公网部署。

Demo精确握手流程：

```text
1. RemoteTool创建device_id和8位pairing_code
2. Agent使用TLS-PSK identity = "pair:" + device_id
3. 临时TLS PSK = pairing_code的8个ASCII字节
4. TLS成功后Agent发送HELLO
5. RemoteTool验证设备、有效期、失败次数和连接来源限制
6. RemoteTool生成并持久化32字节长期device_key
7. RemoteTool发送PAIR_PROVISION
8. Agent通过DPAPI持久化并发送PAIR_PROVISION_ACK
9. RemoteTool使短码失效并关闭临时TLS连接
10. Agent使用identity = "device:" + device_id、PSK = device_key重新连接
11. 正常HELLO/HELLO_ACK后进入ONLINE
```

正常连接不能使用 `pair:` identity；配对连接不能创建Session或进入ONLINE。RemoteTool必须在TLS握手阶段按identity前缀选择临时或长期PSK，未知前缀直接拒绝。

短码作为临时TLS-PSK仍可能遭受离线字典攻击，因此正式跨公网版本必须选择以下之一：

1. 由RemoteTool导出包含32字节高熵bootstrap secret的Agent配置；
2. 使用经过审计的PAKE协议；
3. 使用服务器证书/公钥固定后，在认证加密通道内提交短码。

MVP不得自行发明密钥交换算法。

## 16. 协议日志和隐私

允许记录：

- device_id；
- mapping_id；
- SessionId；
- 本地及目标IP/端口；
- 稳定错误码；
- 字节计数；
- 状态转换时间。

禁止记录：

- PSK、配对密钥或DPAPI明文；
- SSH密码；
- HTTP正文；
- SESSION_DATA内容；
- 完整TLS内部密钥材料。

## 17. 兼容性规则

- RMT/1不承诺与未来RMT/2兼容；
- V1实现只接受Version=1；
- 未知消息类型直接返回 `UNEXPECTED_MESSAGE` 并断开；
- 不实现协议自动探测；
- 更改帧格式必须提升Version并同步更新协议测试向量。

## 18. 协议测试向量

空Payload、Type=`HEARTBEAT`、SessionId=0的头部十六进制应为：

```text
52 4D 54 31 01 03 00 00 00 00 00 00 00 00 00 00
```

Type=`SESSION_DATA`、SessionId=1、Payload=`61 62 63`的完整帧：

```text
52 4D 54 31 01 13 00 00 00 00 00 01 00 00 00 03 61 62 63
```

实现必须用这些向量验证编码器和解码器。
