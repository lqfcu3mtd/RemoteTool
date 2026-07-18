# 远程维护隧道工具测试计划

版本：1.0

## 1. 测试原则

- 单元测试验证纯逻辑；
- 集成测试使用真实loopback Socket；
- 端到端测试启动真实RemoteTool Core、Agent Core和目标服务；
- GUI只做必要的界面行为测试，网络正确性不得依赖人工点击验证；
- 所有超时测试使用可注入时钟或较短测试配置，避免依赖长时间sleep；
- 所有随机测试记录随机种子；
- 测试失败必须返回非0退出码；
- 不允许通过重试隐藏确定性失败。

## 2. 测试层级

| 层级 | 内容 | 每次提交 | Phase完成 | 发布前 |
|---|---|---:|---:|---:|
| Unit | 编解码、状态机、配置、策略 | 是 | 是 | 是 |
| Integration | TCP、Tunnel、Session | 是 | 是 | 是 |
| E2E | RemoteTool-Agent-目标服务 | 核心改动 | 是 | 是 |
| Fault | 断网、半包、超时、拥塞 | 否 | 是 | 是 |
| Soak | 长时间和内存趋势 | 否 | 可选 | 是 |
| Windows Manual | GUI、绿色运行、工具互通 | 否 | Phase 4起 | 是 |

## 3. 单元测试清单

### 3.1 帧编解码

- 官方测试向量编码一致；
- 一次输入完整帧；
- 每次输入1字节；
- 随机分片边界；
- 一次输入多个帧；
- 头部结束处截断；
- Payload中途截断；
- 错误Magic；
- 错误Version；
- 非零Flags；
- PayloadLength刚好上限；
- PayloadLength超过上限；
- SESSION_DATA为0；
- SESSION_DATA为16KiB；
- SESSION_DATA超过16KiB；
- 未知Type；
- Session消息的SessionId为0；
- 控制消息的SessionId非0。

### 3.2 JSON Payload

- 每种消息合法最小对象；
- 缺少每个必填字段；
- 字段类型错误；
- 未知字段；
- device_id为空、过长、非法字符；
- target_port为0、65535、65536；
- target_host为IPv4、IPv6、域名和非法文本；
- connect_timeout边界；
- 非UTF-8；
- 尾随逗号和注释。

### 3.3 状态机

- 每个合法转换；
- 每个非法转换；
- ONLINE前收到OPEN_SESSION；
- 已关闭Session收到迟到CLOSE；
- 未知Session收到DATA；
- OPEN_PENDING超时；
- 半关闭后仍收到允许方向数据；
- 半关闭后发送方再次发送DATA应失败；
- stop操作幂等。

### 3.4 Session ID

- 从1开始；
- 不返回0；
- 并发申请不重复；
- 最近关闭ID不复用；
- 接近uint32上限时要求重建Tunnel。

### 3.5 目标策略

- IPv4 CIDR命中和不命中；
- 回环地址；
- 允许端口和拒绝端口；
- 空白名单全部拒绝；
- 禁止域名；
- IPv6启用和禁用；
- 目标为RemoteTool Agent监听端口时拒绝。

### 3.6 配置

- 默认值；
- 完整合法配置；
- 缺失文件；
- 无读取权限；
- 空文件；
- 损坏JSON；
- 错误schema_version；
- 重复device_id；
- 重复本地端口；
- 原子写入成功；
- 写入失败时旧文件不损坏；
- DPAPI加解密；
- 不同Windows用户不能直接解密另一用户密钥。

## 4. 集成测试组件

测试辅助程序至少包含：

- EchoServer：回显任意字节；
- SlowEchoServer：可控制读取和写出速度；
- HalfCloseServer：验证TCP半关闭；
- HttpTestServer：返回HTML、CSS、JS、图片和并发API请求；
- FaultProxy：按配置分片、延迟、断开或阻塞TCP；
- TestClock：控制心跳和退避时间；
- ProcessHarness：启动、等待、终止RemoteTool Core和Agent进程。

## 5. Phase 1 集成场景

| ID | 场景 | 预期 |
|---|---|---|
| P1-01 | 一个Agent连接 | ONLINE |
| P1-02 | 两个不同Agent连接 | 均ONLINE |
| P1-03 | 重复device_id | 新连接被拒绝 |
| P1-04 | Agent进程终止 | 超时内OFFLINE |
| P1-05 | RemoteTool重启 | Agent退避后重连 |
| P1-06 | 心跳ACK丢失 | Agent主动重连 |
| P1-07 | 半帧后断开 | 无崩溃、资源释放 |
| P1-08 | 超长帧 | 连接断开并记录错误码 |

## 6. Phase 2 集成场景

| ID | 场景 | 数据量 | 预期 |
|---|---|---:|---|
| P2-01 | Echo小数据 | 1 B | 一致 |
| P2-02 | DATA边界 | 16 KiB | 一致 |
| P2-03 | 自动分帧 | 64 KiB | 一致 |
| P2-04 | 随机数据 | 100 MiB | SHA-256一致 |
| P2-05 | 目标拒绝 | - | 明确失败、无泄漏 |
| P2-06 | 目标超时 | - | 在规定时间失败 |
| P2-07 | 目标不允许 | - | Agent拒绝 |
| P2-08 | 本地半关闭 | 1 MiB响应 | 响应完整后关闭 |
| P2-09 | Tunnel中断 | 传输中 | Session全部关闭 |

## 7. Phase 3并发和背压

### 7.1 同一映射多连接

- 同时建立32个连接；
- 每个连接使用不同随机数据流；
- 校验每个连接数据只回到自身；
- 随机关闭其中一半；
- 其余连接继续正常传输；
- 再补足到32个连接。

### 7.2 多映射

- 至少4个映射；
- 总计128个Session；
- 每个映射目标不同；
- 停止一个映射，仅对应Session关闭；
- 其他映射心跳和数据不中断。

### 7.3 慢消费者

- Session A目标停止读取；
- Session B持续echo；
- 心跳持续正常；
- A达到高水位后暂停源读取；
- B延迟不得出现数量级恶化；
- A恢复读取后缓冲下降并继续。

### 7.4 上限

- 第33个单映射连接被拒绝；
- 第129个单Agent连接被拒绝；
- 拒绝不影响已有连接；
- UI计数正确；
- 日志错误码正确。

## 8. Web验证

HttpTestServer页面包含：

- 1个HTML；
- 2个CSS；
- 3个JS；
- 20张小图片；
- 10个并发API请求；
- 一个1 MiB下载；
- keep-alive与新建连接混合。

验证：

- Chrome和Edge通过 `http://127.0.0.1:<port>` 加载成功；
- 所有资源状态正确；
- 多次刷新可用；
- 同时打开至少3个窗口可用；
- 一个窗口关闭不影响另外窗口；
- 不要求RemoteTool理解HTTP内容。

HTTPS目标同样作为透明TCP测试；证书验证由浏览器处理。

## 9. SSH验证

使用真实OpenSSH Server或隔离测试容器：

- 一个客户端登录并执行命令；
- 三个客户端同时登录；
- 每个客户端连续执行100条命令；
- 一个客户端执行持续输出命令；
- 随机关闭其中一个客户端；
- 其他客户端保持可用；
- SFTP不属于MVP验收，但通过同一SSH映射工作不应被主动阻止。

RemoteTool和Agent日志不得出现SSH认证内容。

## 10. 安全测试

- 无PSK连接失败；
- 错误PSK连接失败；
- 一个设备PSK不能冒充另一device_id；
- 禁用设备连接失败；
- 删除设备后旧PSK失效；
- 短码过期；
- 短码使用一次后失效；
- 连续失败达到上限后失效；
- 捕获流量搜索已知SESSION_DATA明文，结果不存在；
- 捕获流量搜索device_key，结果不存在；
- 篡改TLS数据连接失败；
- 配置中不出现明文长期PSK；
- 日志中不出现PSK或SESSION_DATA；
- Release包不存在 `insecure-dev` 开关或二进制；
- 目标白名单不可由RemoteTool绕过。

## 11. 故障注入

- TCP每1字节分片；
- 随机1～500毫秒延迟；
- 传输中立即RST；
- 只关闭写方向；
- RemoteTool进程强制终止；
- Agent进程强制终止；
- 目标服务重启；
- Windows网卡禁用后恢复；
- 系统睡眠后恢复；
- 本地端口被其他程序占用；
- 配置目录只读；
- 模拟磁盘写入失败；
- 系统时间向前/向后调整，超时仍基于单调时钟。

每个故障场景要求：无崩溃、无死锁、句柄最终释放、界面状态准确、错误码明确。

## 12. 性能与资源基线

Demo不是性能产品，但需要建立回归基线。建议在普通Windows x64电脑记录：

- 空闲RemoteTool内存；
- 空闲Agent内存；
- 1、32、128 Session内存；
- 单Session吞吐；
- 32 Session总吞吐；
- 心跳延迟；
- 8小时句柄数和内存趋势。

硬性要求：

- 空闲时不得忙轮询单核；
- Session关闭后Socket和计时器数量回落；
- 8小时测试不出现持续线性内存增长；
- 控制心跳不能因单一大流量Session超时。

具体吞吐数值在首次稳定实现后记录为后续回归基线，不在编码前虚构指标。

## 13. Windows人工验收

Windows 10 x64和Windows 11 x64分别执行：

1. 解压RemoteTool绿色包；
2. 双击启动，无管理员提示；
3. 添加设备并生成配对信息；
4. 解压Agent包并配置；
5. 双击Agent，显示ONLINE；
6. 添加SSH和Web映射；
7. 用系统或第三方SSH客户端建立三个连接；
8. 用Chrome/Edge打开并多次刷新Web；
9. 断开现场网络再恢复；
10. 确认Agent自动重连；
11. 重启RemoteTool，配置恢复；
12. 删除设备，旧Agent不能再次上线；
13. 普通用户账户重复验证。

## 14. 发布阻断条件

出现以下任一情况不得发布：

- 已知远程代码执行、认证绕过或明文密钥问题；
- Release可退回明文；
- 多Session数据串线；
- 关闭一个Session导致其他Session异常；
- Web并发资源无法稳定加载；
- Agent掉线后RemoteTool残留不可清理连接；
- 配置损坏被静默覆盖；
- 需要管理员权限才能正常启动；
- Windows 10或11核心验收未执行；
- 第三方许可证未确认。

## 15. 测试报告模板

```markdown
# Test Report

- Version/Commit:
- OS:
- Compiler:
- Build type:

## Automated
- Unit: pass/fail
- Integration: pass/fail
- E2E: pass/fail

## Manual
- SSH multi-connection: pass/fail
- Web multi-window: pass/fail
- Reconnect: pass/fail
- Green package: pass/fail

## Soak
- Duration:
- Peak sessions:
- Initial/final memory:
- Initial/final handles:

## Security
- TLS capture check:
- Invalid PSK:
- Target policy:

## Known issues
- ...
```

