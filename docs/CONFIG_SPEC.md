# 远程维护隧道工具配置规范

版本：1.0  
适用范围：Windows MVP

## 1. 基本原则

- 采用绿色目录，不写Windows注册表；
- 所有路径以可执行文件所在目录为基准，不以当前工作目录为基准；
- JSON使用UTF-8且不带BOM；
- 每个文件必须包含 `schema_version`；
- 未知字段、错误类型、越界值和重复ID必须明确拒绝；
- 配置损坏时不得自动覆盖或恢复默认值；
- 保存使用同目录临时文件加原子替换；
- 长期PSK不得以明文写入JSON；
- 配置文件中不得包含SSH账号、SSH密码或Web密码。

## 2. RemoteTool目录

```text
RemoteTool/
├── RemoteTool.exe
├── config/
│   ├── remote_tool.json
│   ├── devices.json
│   └── mappings.json
├── logs/
│   └── remote_tool.log
└── README.txt
```

首次启动：

- `config/`不存在时创建；
- `remote_tool.json`不存在时创建默认文件；
- `devices.json`和`mappings.json`不存在时创建空集合；
- 文件存在但内容损坏时停止加载相关功能并显示错误，不覆盖原文件；
- `logs/`不可写时程序仍可启动，但GUI必须显示日志不可用警告。

## 3. remote_tool.json

完整默认示例：

```json
{
  "schema_version": 1,
  "agent_listener": {
    "bind_host": "0.0.0.0",
    "port": 4433
  },
  "mapping_listener": {
    "bind_host": "127.0.0.1"
  },
  "heartbeat": {
    "interval_ms": 10000,
    "timeout_ms": 30000
  },
  "limits": {
    "max_sessions_per_mapping": 32,
    "max_sessions_per_agent": 128,
    "max_session_queue_bytes": 262144,
    "max_tunnel_queue_bytes": 8388608
  },
  "logging": {
    "level": "info",
    "max_file_bytes": 5242880,
    "retained_files": 3
  }
}
```

验证规则：

| 字段 | 规则 |
|---|---|
| `schema_version` | 必须为1 |
| `agent_listener.bind_host` | IPv4/IPv6字面量；MVP默认`0.0.0.0` |
| `agent_listener.port` | 1～65535，默认4433 |
| `mapping_listener.bind_host` | MVP必须为`127.0.0.1`，不允许配置为其他值 |
| `heartbeat.interval_ms` | 1000～60000 |
| `heartbeat.timeout_ms` | 必须至少为interval的2倍，最大180000 |
| `max_sessions_per_mapping` | 1～1024 |
| `max_sessions_per_agent` | 不小于单映射值，最大4096 |
| `max_session_queue_bytes` | 65536～4194304 |
| `max_tunnel_queue_bytes` | 不小于单Session值，最大67108864 |
| `logging.level` | `debug`、`info`、`warn`、`error` |

Release默认日志级别为 `info`，不得记录Session数据。

## 4. devices.json

示例：

```json
{
  "schema_version": 1,
  "devices": [
    {
      "id": "SITE001",
      "display_name": "客户A-机柜1",
      "enabled": true,
      "device_key_dpapi": "base64-encoded-dpapi-blob",
      "created_unix_ms": 1783872000000,
      "updated_unix_ms": 1783872000000
    }
  ]
}
```

规则：

- `id`全局唯一，1～64字符，只允许ASCII字母、数字、`-`、`_`、`.`；
- `display_name`为1～128个Unicode字符；
- `enabled=false`时拒绝Agent认证，但保留映射配置；
- `device_key_dpapi`是当前Windows用户DPAPI密文的Base64；
- 新建但尚未完成配对的设备允许 `device_key_dpapi` 为空字符串；
- 完成配对后必须为非空；
- 删除设备前GUI必须确认；删除后同时停止其映射并关闭Session；
- 不提供导出明文长期密钥功能。

DPAPI作用域使用当前用户，不使用Local Machine。结果是配置目录复制到另一台电脑或另一Windows用户后不能直接解密，这属于预期安全行为。

## 5. Pending pairing状态

一次性配对码只保存在RemoteTool内存中，不写入磁盘。

运行时记录：

```text
device_id
pairing_code
created_monotonic_time
expires_monotonic_time
failed_attempts
source_attempt_counters
```

RemoteTool重启后所有待配对短码失效，用户需要重新生成。GUI只在生成时显示短码，关闭对话框后允许在有效期内重新显示，但不得写日志或剪贴板历史。

## 6. mappings.json

示例：

```json
{
  "schema_version": 1,
  "mappings": [
    {
      "id": "map-site001-ssh",
      "device_id": "SITE001",
      "name": "SSH",
      "local_port": 10022,
      "target_host": "192.168.1.1",
      "target_port": 22,
      "enabled": true,
      "connect_timeout_ms": 10000
    },
    {
      "id": "map-site001-web",
      "device_id": "SITE001",
      "name": "Web",
      "local_port": 18080,
      "target_host": "192.168.1.1",
      "target_port": 80,
      "enabled": true,
      "connect_timeout_ms": 10000
    }
  ]
}
```

规则：

- `id`全局唯一，1～64字符；
- `device_id`必须引用已存在设备；
- `name`为1～64个Unicode字符；
- `local_port`为1～65535，并在全部启用映射中唯一；
- 建议GUI默认分配10000～60000范围端口；
- `target_host`必须为IP字面量；
- `target_port`为1～65535；
- `connect_timeout_ms`为1000～30000；
- `enabled`表示RemoteTool启动后应尝试启动监听；
- 端口被占用时不修改enabled，运行状态显示ERROR；
- 目标仍必须通过Agent本地白名单检查。

## 7. Windows Agent目录

```text
WindowsAgent/
├── Agent.exe
├── config/
│   └── agent.json
├── logs/
│   └── agent.log
└── README.txt
```

## 8. agent.json首次配对状态

```json
{
  "schema_version": 1,
  "server": {
    "host": "192.168.1.100",
    "port": 4433
  },
  "device": {
    "id": "SITE001",
    "pairing_code": "58392147",
    "device_key_dpapi": ""
  },
  "target_policy": {
    "allowed_cidrs": ["127.0.0.0/8", "192.168.0.0/16"],
    "allowed_ports": [22, 80, 443, 8080],
    "allow_ipv6": false
  },
  "logging": {
    "level": "info",
    "max_file_bytes": 2097152,
    "retained_files": 2
  }
}
```

配对成功后的状态：

```json
{
  "schema_version": 1,
  "server": {
    "host": "192.168.1.100",
    "port": 4433
  },
  "device": {
    "id": "SITE001",
    "pairing_code": "",
    "device_key_dpapi": "base64-encoded-dpapi-blob"
  },
  "target_policy": {
    "allowed_cidrs": ["127.0.0.0/8", "192.168.0.0/16"],
    "allowed_ports": [22, 80, 443, 8080],
    "allow_ipv6": false
  },
  "logging": {
    "level": "info",
    "max_file_bytes": 2097152,
    "retained_files": 2
  }
}
```

规则：

- `server.host`在MVP中接受IP字面量或普通DNS名称；这是RemoteTool地址，不是Session目标；
- `device.id`遵循协议device_id规则；
- `pairing_code`为空或正好8位数字；
- 配对成功后必须原子写入 `device_key_dpapi` 并清空 `pairing_code`；
- 如果两者都为空，显示“未配置凭据”并停止连接；
- 如果两者都非空，优先长期密钥并警告配置中仍残留短码；不得回退到短码；
- `allowed_cidrs`为空表示全部拒绝；
- `allowed_ports`去重后每项为1～65535；
- Agent运行期间不从RemoteTool接收永久白名单更新。

## 9. 日志轮转

- 日志达到 `max_file_bytes` 后轮转；
- 文件名格式：`remote_tool.log.1`、`.2`或 `agent.log.1`；
- 超过retained数量删除最旧日志；
- 日志写失败不导致网络进程崩溃，但在GUI显示一次持续告警；
- Debug日志也不得包含密钥或SESSION_DATA。

## 10. 配置更新行为

- RemoteTool配置通过GUI保存；
- Agent MVP允许退出程序后手工编辑agent.json；
- 运行期间不热加载配置；
- 修改RemoteTool Agent监听端口需要重启RemoteTool；
- 新建、启停和删除Mapping可运行时生效；
- 编辑运行中的Mapping时先停止原监听，保存成功后再按enabled启动；
- 保存失败时保留原运行配置和原文件，并显示错误；
- 不实现旧schema自动迁移；未来schema升级必须提供显式迁移工具或明确拒绝。

## 11. Web透明转发限制

RemoteTool是TCP转发器，不修改HTTP内容。因此必须在README说明：

- 浏览器发送的 `Host` 可能是 `127.0.0.1:<local_port>`；
- 依赖固定Host虚拟主机的设备Web可能不能正确响应；
- 设备返回到其原始IP的绝对重定向可能绕过隧道；
- HTTPS设备证书通常与 `127.0.0.1` 不匹配，浏览器可能警告；
- Cookie domain、WebSocket来源检查和绝对URL可能影响功能；
- MVP不重写Host、Location、Cookie、HTML或TLS SNI。

遇到此类设备应记录为后续HTTP代理或本地域名映射需求，不能在透明TCP层临时增加内容重写。

