# VisionApp 项目设计

## 0. 文档定位

本文定义 VisionApp 的组合根、Qt 双线程协调、ApplicationController、VisionWorker、配置与日志、TCP/UI 边界、海康硬件组合创建、任务准入、状态映射、启动和安全关闭。本文服从根目录以及 `VisionCore/AGENTS.md`、`VisionHardware/AGENTS.md`、`VisionAlgorithm/AGENTS.md` 和 `VisionTcp/AGENTS.md`，不得重新定义各库内部接口、算法流程、厂商节点或 TCP 报文格式。

VisionApp 是解决方案中唯一的 x64、C++20、UTF-8 可执行程序和唯一组合根。无 UI 阶段使用 `QCoreApplication`，接入 Qt Widgets 后使用 `QApplication`。ApplicationController、VisionWorker、应用配置、诊断桥和日志组合均属于 VisionApp，不拆分新的业务项目。

当前配置事务与工程一致性修正允许修改 Debug 工程元数据，以统一 x64、`/MDd` 和配置相关依赖；本轮仍不构建 Debug，也不得声明 Debug 已通过。Debug 正式验收以提供匹配的 OpenCV Debug 二进制为前置条件。

---

## 1. 职责与边界

VisionApp 负责：

* `main()`、Qt 应用对象和事件循环；
* 启动配置解析、日志初始化和进程级退出码；
* ApplicationController、VisionWorker 和唯一视觉线程的创建与所有权；
* VisionTcp、UI、视觉服务和任务状态的协调；
* 网络命令准入、会话代次关联、Busy 拒绝和结果发送；
* 在视觉线程创建 VisionAlgorithm、海康硬件组合和 VisionCore；
* 汇聚 Core、Hardware、Algorithm 和 TCP 诊断并写入统一 spdlog 后端；
* 应用启动、初始化重试、业务恢复和非阻塞安全关闭；
* 将稳定 `ErrorCode` 映射为 UI 和 TCP 对外状态。
* 在视觉线程组织标定维护会话：按角色加载严格两列理论点、复用 Core 标定采集、接收 P1/P2 索引、调用三阶段标定算法，并以可回滚文件事务重载运行时。

VisionApp 不负责：

* 图像处理、标定、坐标转换或 X/Y/R 计算；
* 海康相机、光源控制器句柄、节点读写和 SDK 错误转换；
* TCP 拆组包、JSON 完整性和 socket 底层收发；
* VisionCore 内部 Locate、清理、恢复或错误优先级；
* 激光轨迹、设备运动、PLC 生产节拍或上位机生产调度；
* 通过 Qt timer、线程终止或后台任务中断同步 Locate。

---

## 2. 对象图和线程归属

生产代码只主动管理两个线程：

```text
Qt 主线程
├─ QCoreApplication / QApplication
├─ ApplicationController
├─ VisionTcpServer、QTcpServer、QTcpSocket、QTimer
├─ UI 与只读状态快照
└─ spdlog 汇聚和对外消息映射

视觉工作线程
├─ VisionWorker
├─ DiagnosticBridge
├─ VisionAlgorithm 具体对象
├─ VisionHardware 的共享海康硬件组合
│  ├─ CameraPort
│  └─ LightPort
└─ VisionCore
```

ApplicationController 在主线程拥有 `QThread` 和 VisionWorker 的生命周期协调权。VisionWorker 可以在主线程构造后立即移动到尚未开始业务的视觉线程，但任何 Core、Hardware、Algorithm 和厂商 SDK 对象必须由 `VisionWorker::initialize()` 在视觉线程中创建。禁止在主线程预先创建这些对象再移动。

跨线程通信只使用 Qt queued signal/slot，并按值传递命令、上下文、状态快照、`LocateOutcome` 和稳定 `Error`。所有跨线程自定义类型必须在第一次 queued 连接或投递前注册为 Qt 元类型。

不得跨线程传递或共享：

* QTcpSocket、UI 控件或 QTimer；
* 海康句柄、接口信息、SDK 对象或节点包装；
* VisionCore、相机端口、光源端口或算法对象；
* 可变 `cv::Mat`、厂商图像缓冲或所有权不明的裸指针。

需要向 UI 显示图像时，由视觉线程形成独立只读快照，并在 VisionApp 边界转换为具有独立存储的 `QImage`。标定 UI 只接收按值的 QImage、圆心、点表、矩阵和质量快照，标定 `cv::Mat` 始终留在视觉线程会话中。

---

## 3. 应用配置

VisionApp 负责读取应用启动 JSON，并转换为各项目定义的稳定配置类型。配置解析失败返回 `InvalidConfiguration`，不得启动视觉线程业务初始化或 TCP 监听。

应用配置至少组合：

* VisionTcp 定义的监听配置；
* VisionHardware 定义的五相机、触发和采集有限等待配置；
* VisionHardware 定义的海康光源控制器配置；
* VisionAlgorithm 主配置文件路径；
* 日志目录、级别、单文件容量和保留数量；
* 仅属于 App 的结果确认重试策略。

光源配置固定包含：

* 一个海康控制器序列号；
* WhiteBacklight、CoaxialRed、CoaxialGreen、CoaxialBlue 四个逻辑角色与物理输出选择值的映射；
* 四个 `[0.0, 1.0]` 归一化固定亮度。

VisionApp 只进行 JSON 结构、必填字段和通用数值转换，再把稳定 C++ 配置传给 VisionHardware。设备节点能力、实际亮度范围、步长、写入和回读由 VisionHardware 负责。JSON、QJsonObject、海康 SDK 类型或应用配置不得进入 VisionCore。

UI 可以编辑应用、相机、光源、TCP 和算法主配置路径的草稿。相机、光源和算法相关变更只能在任务空闲时通过受控重新初始化视觉服务生效；Locate 期间不允许直接调光、改曝光、改触发或热写算法状态。第一阶段仍不支持光源 TCP 命令和脉冲时间编辑。

首次初始化失败后的配置应用必须作为完整事务处理。候选视觉运行时、候选 TCP 配置和配置文件持久化全部成功后才允许提交；任一后续阶段失败时，Controller 必须先恢复事务前的 TCP 配置与监听状态，再由视觉线程关闭并销毁候选运行时、恢复旧 `VisionRuntimeConfig` 记录，最后发布 Failed/Faulted。初次故障回滚不得重新创建已知失败的旧运行时，也不得让后续 `retryInitialization()` 使用失败事务中的候选配置。候选运行时清理失败优先作为最终 Error，原始应用错误只追加到 diagnostic。

---

## 4. 生产组合与创建顺序

生产 CoreFactory 在视觉线程内按以下顺序同步创建运行时：

```text
使用算法主配置路径创建 VisionAlgorithm
→ 算法完成参数、模板和标定资源校验
→ 使用相机与光源配置一次性创建海康硬件组合
→ 确认 CameraPort 和 LightPort 均非空且已经初始化
→ 将 CameraPort、LightPort、Algorithm 和 DiagnosticSink 注入 VisionCore
→ 检查 CoreStatus == Ready
→ 向主线程报告初始化成功
```

先创建算法是为了在算法配置确定性失败时避免打开真实硬件。硬件组合创建失败时，VisionHardware 必须完成已经取得资源的反向清理；VisionApp 不补做厂商句柄操作。

生产目标接口直接使用 VisionHardware 一次性返回的 `HikHardwarePorts`。当前 `VisionRuntimeFactory` 中单独注入 `LightFactory` 的形式只是光源适配缺失期间的过渡草稿，后续代码阶段必须移除，不作为稳定扩展点。测试仍可向 VisionWorker 注入返回 Fake Core 的 `CoreFactory`，但生产路径不得分别拼装彼此独立初始化 SDK 的 Camera 和 Light。

生产工厂目标声明固定为：

```cpp
[[nodiscard]] CoreFactory makeProductionCoreFactory(
    ApplicationConfig config);
```

`ApplicationConfig` 已经包含构造 VisionAlgorithm 和完整海康硬件组合所需的稳定配置，因此生产入口不得再接收单独的 CameraFactory、LightFactory 或厂商句柄。

任一创建入口返回 Error、空端口或非 Ready Core 时：

* VisionWorker 不保存部分运行时；
* ApplicationController 将视觉服务置为 Faulted 或 StartFailed 并保存稳定错误；
* TCP 不开始监听；
* UI 可以显示错误并提供明确的初始化重试入口；
* 重试只能在上一轮对象已经在线程内清理完成后开始。

---

## 5. 状态所有权和任务准入

四类状态分别建模：

```text
ApplicationState   → Starting / Ready / StartFailed / ShuttingDown / Stopped
VisionServiceState → Starting / Ready / Faulted / Recovering / Stopped
TaskState          → Idle / Preparing / Running / AwaitingResultAck
TCP 连接状态       → 由 VisionTcp 和当前 ConnectionContext 表达
```

ApplicationController 是应用、任务和网络上下文状态的唯一所有者；VisionWorker 只拥有视觉线程内的运行时和当前同步命令；VisionCore 是视觉业务状态的唯一所有者。UI 只显示 `ApplicationSnapshot`，不得直接改写状态。

Locate 准入条件固定为：

* 应用和视觉服务均为 Ready；
* TaskState 为 Idle；
* TCP 命令来自当前连接代次，或命令是本地维护入口；
* 应用未进入 ShuttingDown。

Busy 时新 Locate 不排队并立即拒绝；QueryStatus 仍可执行。不提供 CancelCommand。TCP 断线不取消已开始的 Locate，旧连接产生的结果不得发送给新连接。

ApplicationController 将已经确定的网络模式转换为 `LocateOptions`。VisionWorker 只调用一次同步 `core.locate(options)`；Core 负责把 `LocateMode` 同时传给 Light 和 Camera。App 不解释 Edge 使用白色背光、MarkPoint 使用 RGB 的底层通道动作。

---

## 6. 启动流程

`main()` 的目标顺序：

```text
创建 Qt 应用
→ 解析命令行和应用配置路径
→ 同步加载并校验 ApplicationConfig
→ 创建并配置唯一 spdlog logger
→ 注册跨线程 Qt 元类型
→ 创建 ApplicationController、VisionWorker 和视觉线程
→ 建立 queued 信号槽连接
→ 启动视觉线程
→ 投递 VisionWorker::initialize()
→ VisionCore Ready
→ ApplicationController 启动 VisionTcp 监听
→ 应用进入 Ready
→ 进入 Qt 事件循环
```

TCP 必须晚于视觉运行时 Ready。TCP 监听失败不销毁已经 Ready 的视觉运行时；应用保存通信错误并允许独立重试 TCP。视觉初始化失败时不得启动 TCP。

所有启动步骤必须记录稳定阶段名。配置和日志初始化发生在主线程；算法资源加载、相机连接和光源控制器连接发生在视觉线程。主线程不得同步等待视觉初始化完成。

---

## 7. Locate、Prepared 和结果交付

ApplicationController 收到 TCP Locate 命令后：

```text
验证连接代次、模式和任务准入
→ 保存 TcpTaskContext
→ TaskState = Preparing
→ queued 投递 VisionWorker::locate
→ 硬触发模式下接收 onPrepared 对应的快速通知
→ VisionWorker 同步调用 VisionCore::locate
→ 按值返回 VisionTaskCompletion
→ 校验完成上下文仍属于原任务
→ 映射为 TcpMessage 和 UI 结果
```

Prepared 只表示目标相机已经开始取流并清理旧缓存，不表示外部触发一定发生，也不参与光源节点控制。VisionWorker 对 `ICaptureObserver::onPrepared()` 只能快速发出 Qt 信号，不得访问 TCP、等待主线程或执行另一项硬件操作。

只有 OK 的 X/Y/R 可以进入正常结果或打标触发流程。NG 固定发送三个 `0.0`。Error 根据稳定 `ErrorCode` 映射，不得向上位机暴露厂商错误码或解析 diagnostic。

结果确认重试由主线程 QTimer 管理，只重发已经确定的网络结果，不影响视觉任务是否完成，也不重跑 Locate。

---

## 8. 恢复和关闭

恢复入口只在没有活动任务时投递到视觉线程：

```text
ApplicationController 将 VisionServiceState 改为 Recovering
→ VisionWorker 同步调用 VisionCore::recover()
→ Core 依次恢复 Light、强制关灯并恢复 Camera
→ 返回 CoreStatus
→ 成功进入 Ready，失败保持 Faulted
```

App 不重新解析配置、不单独重建光源端口，也不调用 MVS 初始化或结束函数。

关闭必须是主线程非阻塞状态流程：

```text
ApplicationState = ShuttingDown
→ 拒绝新 Locate、Recovery 和维护命令
→ 停止接受新的 TCP 连接和任务
→ 当前 Locate 如已开始则自然运行到返回
→ queued 调用 VisionWorker::shutdown()
→ Core 关灯、关闭 Camera、关闭 Light
→ VisionHardware 关闭控制器并在两个端口停止后 Finalize SDK
→ VisionWorker 在视觉线程销毁 Core 和端口对象
→ 发出 shutdownCompleted
→ 主线程请求 visionThread.quit()
→ visionThread.finished
→ 销毁 Worker、Controller、UI 和日志
```

不得调用 `QThread::terminate()`，不得在主线程 `wait()` 当前 Locate，不得在视觉线程事件循环停止后依赖 `deleteLater()` 清理 Core、Hardware 或 Algorithm。Worker 的具体销毁连接必须保证其析构发生在视觉线程退出前；关闭失败要记录，但不能跳过其余资源清理。

---

## 9. 诊断和日志

VisionCore、VisionHardware 和 VisionAlgorithm 通过 `IDiagnosticSink` 在视觉线程同步提交结构化事件。DiagnosticBridge 只负责把事件转换为按值的 AppDiagnostic 并 queued 发送到主线程；提交失败不得改变业务结果。

VisionTcp 通过自身 Qt 信号报告通信诊断。ApplicationController 将视觉和网络诊断统一交给注入的 spdlog logger。生产库不得调用 `spdlog::get()` 查找全局 logger，也不得各自创建日志文件。

日志至少关联：

* 应用、视觉服务、任务和 TCP 连接状态；
* taskId、连接代次和 LocateMode；
* ErrorCode、稳定 message 和 diagnostic；
* 硬件阶段、相机角色或光源通道诊断；
* 启动、恢复和关闭阶段。

日志失败默认不得改变视觉结果。日志目录、滚动策略和失败处理必须由应用配置明确，不得把本机绝对路径写入源码。

---

## 10. x64 工程和测试边界

本轮生产行为只构建和验收 `Release|x64`，同时允许修正 Debug 工程元数据：

* 使用 C++20、UTF-8、x64 和统一动态 Runtime Library；
* Qt、OpenCV、HIKSDK、spdlog 和其他依赖必须与被引用项目架构一致；
* Debug 固定使用 `/MDd` 并只引用匹配的 Qt/OpenCV Debug 依赖；当前缺少 OpenCV Debug 二进制，因此 Debug 构建结果不属于本轮交付；
* 不在工程文件中写入 SDK、日志或配置的开发机绝对路径；
* 不提交日志、缓存、生成图像、可执行文件或本机用户配置。

VisionApp 协调测试必须使用 Fake CoreFactory 和本地回环 TCP，不连接真实相机或光源。至少覆盖：

* 视觉 Ready 后才启动 TCP；
* 算法或硬件创建失败时 TCP 不监听且不保存部分运行时；
* Busy 不排队，QueryStatus 仍可执行；
* Edge 和 MarkPoint 的模式按值进入 VisionWorker；
* 断线后旧任务结果不发送给新连接；
* 当前任务自然返回后再执行视觉线程关闭；
* 硬件和 Core 在线程退出前销毁；
* 关闭期间拒绝新任务且最终发出 shutdownFinished。

当前 VisionTest 按 Lifecycle、TaskDelivery、Recovery 和 Configuration 四类套件覆盖上述契约，并覆盖首次初始化/TCP 失败后重试、初次配置候选 TCP 或持久化失败时销毁候选运行时、回滚后重试继续使用旧配置、候选清理错误优先、ResultAck 校验、结果重发与耗尽、本地/TCP 恢复、配置更新与失败回滚、算法校验 requestId 隔离。`ProtocolMapperTests` 覆盖全部 Core/App/Tcp 枚举与 OK/NG/Error/TaskDisplayResult 语义；`DiagnosticBridgeTests` 覆盖按值拷贝、帧上下文转换、生命周期隔离和单信号提交；AppConfig 覆盖 malformed JSON、schema、枚举、路径、时间/大小边界和 Hardware 配置校验传播。

协调测试继续使用唯一 `QCoreApplication`、Qt 事件循环和有界条件等待，不增加线程或长时间 `sleep`。按已批准的 VisionTest 重写方案，关键 Qt Widgets 操作在独立 Ui 进程验收：中心及四角 5×5 完整标定、配置编辑/撤销/应用、详细与监控窗口切换、未保存配置退出确认。使用真实窗口、Controller、Worker、Core 和标定算法，仅替换硬件与生产 Locate 端口；不做全量外观回归。文件路径允许通过公开入口预置，不注入私有槽或成功快照。默认协调测试仍使用 QCoreApplication，完整边界见 VisionTest/AGENTS.md。

真实 `MV_LC_INTERFACE`、通道、亮度回读和灯光效果验证属于单独标记的 Hardware 联调，不进入默认 App 自动化测试。

---

## 11. 标定维护后端

标定计算保持三个独立公开阶段：`detect()`、`solve()`、`evaluate()`。VisionWorker 在唯一视觉线程持有 `CalibrationAlgorithm` 与单个 `CalibrationSession`；标定后端创建失败只禁用标定功能，不得破坏已经 Ready 的生产 Locate 运行时。

理论点文件由 UI 按 Center 或四个稳定角相机角色独立选择，文件名和扩展名不参与业务判断。文件为 UTF-8 严格 `X,Y` 两列，行序即 P1～PN，单位隐式为毫米；允许 BOM、空白和空行，禁止表头、注释和附加列。点数必须是边长 3～15 的平方数，坐标必须有限、互不重复且整体非共线。四角文件点数必须一致。

检测后 UI 仅提交命中的原生圆心索引。Worker 再次验证 P1 为四角之一、P2 为两个直接邻点之一，并推导八方向枚举；UI 不拥有或推断业务方向。跨线程标定快照只携带独立 QImage、按值圆心/点表、九个矩阵元素、质量指标、阶段和稳定 Error。

Controller 在采集请求投递前取得标定维护锁，直到应用成功或 Reset。维护期间 Locate、Recovery、算法校验、运行配置应用及另一模式标定均返回 Busy，QueryStatus 保持可用。Center 复用 Edge 采集与白色背光；四角复用 MarkPoint 采集与同轴 RGB；Core 始终负责关灯和帧契约。

标定文件目标由算法主配置 `calibration` 节点解析。App 读取原角色标定文件并仅替换 `image_to_physical`，候选与备份位于目标同目录；Center 提交一份，四角提交四份。候选全部生成和校验后替换目标，再通过 `CalibrationAlgorithm::create()` 和同一 `VisionRuntimeConfig` 重建运行时。任何可捕获失败必须恢复全部旧文件并恢复旧运行时；恢复失败将视觉服务置为 Faulted。本阶段不提供进程崩溃或断电恢复日志。

## 12. 离线文件联调后端

显式使用 `--backend file --image <path>` 时，VisionApp 在现有视觉线程内读取单张本地普通图像文件，解码为连续、独立的 `CV_8UC1` Center 图像，组合无硬件操作的光源端口和真实 `VisionAlgorithm`。图像路径相对于启动工作目录解析，算法配置仍来自应用配置的 `vision.algorithmConfigPath`。`hik` 保持默认，不自动回退，`simulation` 保持原有预设结果语义；`--image` 不允许用于其他后端。

文件后端仅服务明确选择的离线 Edge 联调，不代表真实设备采集。图像在运行时创建时加载，此后每次 capture 克隆独立像素，生成时间戳和递增帧号；源文件变更须重新初始化才能生效。`onPrepared` 表示文件帧已准备好，不表示真实相机硬触发。MarkPoint 返回 `InvalidConfiguration`，不复制同一图像伪造四角采集。解码失败在创建边界返回 `InvalidConfiguration`，不启动 TCP；其他错误沿用统一边界。算法超时、Core 清理、TCP 会话与 ACK、图像快照、恢复和关闭均复用既有链路，不创建额外线程。

文件后端不提供标定真实性保证。离线测试配置可以使用明确标注的单位矩阵，仅将输出按像素解释，不得将该结果标为设备毫米坐标或生产精度验收。日志必须明确记录文件后端和真实算法来源。

## 13. 当前明确不开发

* 可拖拽流程、插件系统、消息总线或通用任务队列；
* 多视觉线程、线程池或并行 Locate；
* CancelCommand、强制取消或进程内看门狗中止；
* Locate 期间热写光源、曝光、触发或算法参数，以及脉冲编辑或独立光源 TCP 协议；
* App 直接访问相机或控制器节点；
* 相机 Line I/O 光源控制和 `MV_LC_INTERFACE` 失败后的自动回退；
* 独立网络线程、Asio 或第二套网络事件循环；
* 在 MainWindow 中实现完整业务调度；
* 未经确认改变现有 TCP 报文、坐标语义或结果确认协议。
