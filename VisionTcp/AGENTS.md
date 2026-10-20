# VisionTcp 项目设计

## 1. 职责和依赖

VisionTcp 是运行在 Qt 主线程中的非 UI 静态库，只依赖 QtCore 和 QtNetwork。它负责单客户端 TCP Server、换行分隔 UTF-8 JSON、半包粘包、协议校验、心跳、连接代次和通信诊断。

VisionTcp 不依赖 VisionCore、VisionHardware、VisionAlgorithm 或 QtWidgets，不判断视觉 Ready/Busy，不执行或取消视觉任务。ApplicationController 负责命令准入、状态、结果确认重发和 Core 错误映射。

## 2. 传输和会话

* 视觉软件是服务端，上位机是唯一客户端并维持长连接；
* 每条报文是一个紧凑 UTF-8 JSON 对象，以 `\n` 结尾，协议版本固定为 `1`；
* 单帧默认上限为 16 KiB，超过上限回复 `MESSAGE_TOO_LARGE` 后断开；
* 第二客户端收到 `SINGLE_CLIENT_ONLY` 后关闭，不影响当前客户端；
* 每次接受连接生成非零递增 `ConnectionContext::generation`；
* 所有业务回复必须携带原连接代次，旧代次消息必须丢弃；
* 不支持的协议版本回复错误后断开，其他单条格式错误回复 `protocolError` 并保持会话；
* 通信诊断只用于显示和日志，不参与协议解析或业务控制流。

## 3. 正式报文

上位机可以发送：

* `prepareLocate`：包含 1 至 20 位非零十进制字符串 `taskId` 和 `EDGE`/`MARK_POINT`；
* `resultAck`：确认同一 `taskId` 的最终 OK、NG 或 ERROR；
* `heartbeat`：包含 uint32 `sequence`；
* `queryStatus`：包含 uint32 `sequence`；
* `recover`：包含 uint32 `sequence`。

视觉软件可以发送：

* `locatePrepared`、`prepareRejected`、`locateResult`；
* `heartbeatAck`、`statusReply`、`recoverResult`；
* `protocolError`。

NG 的 X/Y/R 固定为 `0.0`。ERROR 不发送坐标，只发送稳定错误码和可读 message；diagnostic、厂商错误码和第三方异常文本不得进入协议。

## 4. 心跳

上位机每秒发送一次 heartbeat，VisionTcp 收到合法报文后立即回显 sequence 并重置接收计时。默认连续三秒没有合法 heartbeat 就关闭当前客户端。心跳处理不依赖视觉任务状态，在定位、恢复和等待结果确认时都必须保持响应。

## 5. 测试

VisionTcp 测试按 Codec、Session 和 Replies 三类组织。Codec 覆盖拆包、粘包、空行/CRLF、非法 UTF-8/JSON、所有合法输入命令、taskId/mode/sequence 字段矩阵、版本和最大报文边界；Session 覆盖 start/stop 幂等、监听失败、重配置边界、单客户端、连接代次、显式断开和心跳重置/超时；Replies 覆盖全部稳定响应、可选字段、OK/NG/Error 契约、所有旧代次发送拒绝和公开枚举 `protocolName()`。

测试使用共享的通用 Qt 事件循环与 JSON Lines 客户端、本地回环和有界等待，不创建网络线程，不启动真实视觉任务。协议构造、业务断言和模块 Fixture 仍属于 VisionTcp TestSupport，不进入跨模块公共层。多客户端业务和未批准协议扩展仍暂缓。
