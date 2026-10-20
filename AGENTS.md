# VisionSoftware 项目开发约束

## 0. 文档定位

本文件是 QtVisionApp 仓库的解决方案级开发约束，适用于全部生产项目、测试项目和非项目目录。

本文件只规定项目目标、业务边界、项目划分、依赖方向、统一线程模型、跨项目数据与错误语义、生命周期、测试、修改和交付要求。

各项目内部的接口、文件结构、SDK 使用、实现细节、性能指标、超时数值和测试用例，必须写入对应子目录的 `AGENTS.md`。子目录规则可以补充本文件，但不得改变或绕过本文件。

发生规则冲突时，必须先停止相关修改，明确冲突和影响范围，经开发者确认后统一更新文档和代码。

本文件描述彻底重构后的目标架构。旧目录、旧接口和旧实现仅作为功能参考，不构成必须兼容的架构约束。

---

## 1. 项目目标

QtVisionApp 是面向激光加工设备的工业视觉软件，负责接收上位机视觉命令，控制相机和光源，完成图像采集、视觉定位、标定和坐标转换，输出 X、Y、R 等定位结果，并提供运行状态、配置、日志、诊断和 UI。QtVisionApp 负责与工控机中的上位机软件和激光打标软件进行视觉结果协同，可以根据既定协议发送视觉结果、运行状态和打标触发请求，但不负责激光轨迹规划、激光器底层控制、设备运动控制或生产调度。

当前视觉流程由业务预先定义，不提供用户自由拖拽算法节点或动态编排视觉流程的能力。第一阶段固定支持两种定位模式：

* `Edge`：使用中心相机采集一张图像，由 VisionAlgorithm 完成四边识别、几何有效性处理、标定转换和最终位姿计算；
* `MarkPoint`：使用四角相机形成一次多图采集，由 VisionAlgorithm 完成四个 Mark 点识别、各相机标定转换、多点位姿融合和最终位姿计算。

两种模式最终都只向 Core 返回 `double` 类型 X、Y、R 和 `valid`，由 Core 形成 OK 或 NG。模式只决定本次 Locate 的采集和算法输入形态，不改变线程、取消、结果或错误语义。

开发优先级：

1. 定位结果准确、稳定、可重复；
2. 硬件、线程和资源生命周期安全；
3. 异常可识别、可追踪、可恢复；
4. 算法执行具有简单明确的协作式超时检测，硬件和关闭操作使用有限等待；
5. 模块职责和依赖方向清晰；
6. 核心功能可独立测试；
7. 实现简单、可维护、不过度封装；
8. 在不增加当前负担的前提下保留合理扩展空间。

不得为了通用性提前引入线程池、消息总线、插件框架、流程编排器或当前业务不需要的抽象层。

---

## 2. 业务边界

QtVisionApp 负责视觉设备连接与配置、相机采图、光源控制、图像定位、标定和坐标转换、结果判定、视觉任务状态、TCP 通信、UI、视觉日志和诊断，以及按既定协议向上位机或激光打标软件发送视觉结果、状态和打标触发请求。

QtVisionApp 不负责激光轨迹规划、激光器底层控制、设备运动控制、PLC 生产节拍、上位机生产调度和与视觉无关的工艺逻辑。

当功能归属不明确时，优先判断它是否直接服务于视觉采集、定位、标定或结果输出。不属于视觉核心职责的功能，不得因实现方便放入 VisionCore。

---

## 3. 解决方案划分

第一阶段基线架构由六个项目组成：

```text
QtVisionApp
├─ VisionApp
├─ VisionCore
├─ VisionHardware
├─ VisionAlgorithm
├─ VisionTcp
└─ VisionTest
```

允许存在 `third_party/`、`config/`、`logs/` 或 `tools/` 等非项目目录。

第一阶段不单独建立 VisionController、VisionInfra、Vendor 业务项目、Common、Utils、Helper 或多个细碎测试项目。

任何项目的新增、删除或拆分都属于解决方案架构变更。只有出现明确、稳定且被多个项目复用的能力时，才允许评估新增公共项目。新增项目前必须说明唯一职责、必要性、依赖方向、替代方案和长期维护成本，并经开发者确认。

---

## 4. 项目职责

### 4.1 VisionApp

VisionApp 是唯一应用程序项目和组合根，负责 `main()`、Qt 事件循环、ApplicationController、VisionWorker、唯一视觉线程、对象组合、命令准入、TCP/UI/视觉任务协调、应用状态、配置、日志、启动和安全关闭。

无 UI 阶段可使用 `QCoreApplication`，UI 接入后使用 `QApplication`。ApplicationController 和 VisionWorker 属于 VisionApp，不建立独立项目。

VisionApp 不实现具体图像算法、厂商 SDK 适配、TCP 底层协议或 VisionCore 内部视觉流程。

VisionApp 使用 Qt Widgets 提供统一工业 UI。详细模式与监控模式只属于 UI 展示状态，不得改变 TCP 任务准入、相机触发或视觉业务流程。生产 Locate 统一由 TCP 发起，不提供本地维护 Locate。

应用允许在进程不退出的前提下受控应用配置。相机、光源、算法、标定或模板相关变更只能在任务空闲时执行：拒绝新任务、在视觉线程关闭旧运行时、按新配置重新创建，并在失败时尝试恢复旧配置。该流程不构成 Locate 期间热写设备节点，也不得增加线程或强制取消任务。

### 4.2 VisionCore

VisionCore 是非 Qt 的同步 C++ 视觉业务静态库，负责定义定位模式、稳定业务类型、公共多图帧类型、统一结构化错误类型以及硬件和算法抽象接口，组织完整视觉定位流程，按模式校验 `ImageFrames` 并构造对应算法输入，管理业务状态、单次视觉事务清理和恢复动作，并形成最终定位结果或稳定错误。

VisionCore 还提供仅供 VisionApp 标定维护使用的窄同步采集入口 `captureForCalibration(LocateMode)`。该入口复用 Locate 的模式照明、采集、始终关灯、帧校验和错误优先级，但不调用生产定位算法，也不形成定位结果；标定维护互斥、理论点文件、标定计算和文件事务仍属于 VisionApp 与 VisionAlgorithm 的职责。

OpenCV 是经批准的解决方案级基础依赖。VisionCore 可以依赖标准库和 OpenCV Core 头文件/API，并允许公共图像帧使用 `cv::Mat` 作为像素载体。当统一提供的 OpenCV 二进制只包含 `opencv_world` 时，VisionCore 及其测试允许链接该 world 库作为 Core API 的二进制载体；这不授权 VisionCore 包含或调用 OpenCV 图像处理、标定、特征、文件读写或 UI API。VisionCore 不依赖 Qt、TCP、JSON、UI、厂商 SDK 或其他具体第三方算法库，不创建线程，不提供 Future、回调或协程式 Locate，也不提供外部取消当前 Locate 的能力。

### 4.3 VisionHardware

VisionHardware 是硬件适配静态库，负责实现 VisionCore 定义的相机和光源接口，封装中心相机与四角相机的厂商 SDK，管理设备连接、角色映射、配置、按定位模式采集、释放、厂商错误转换和图像缓存所有权。相机端口保持一个模式感知的采集入口，统一返回 `ImageFrames`；不得为中心相机和四角相机分别增加 Core 相机端口。

VisionHardware 负责识别厂商 SDK 的本地错误，并将其直接转换为 VisionCore 定义的统一 `Error`。厂商原始错误码和详细上下文只进入 `Error::diagnostic` 或日志，不得成为业务判断所依赖的公共类型。

VisionHardware 不负责业务流程、图像算法、TCP、UI、任务调度和上位机协议。

### 4.4 VisionAlgorithm

VisionAlgorithm 是基于 OpenCV 的视觉算法静态库，负责图像预处理、模板构建和加载、粗定位、精定位、亚像素计算、标定转换、算法质量指标计算，以及其他经批准的第三方视觉算法封装。

VisionAlgorithm 实现 VisionCore 定义的算法抽象，负责定义算法配置及算法执行超时，使用 `std::chrono::steady_clock` 在算法实现内部完成计时和截止时间检查，并负责识别算法库和第三方算法的本地错误，将其直接转换为 VisionCore 定义的统一 `Error`。VisionCore 不设置、分配或强制中断算法超时。

VisionAlgorithm 的定位端口支持两种强类型输入形态：中心相机单图抓边输入，以及按四角相机角色组织的四图 Mark 点输入。两种输入都只包含同步调用期有效的只读 `cv::Mat` 引用，不包含采集时间、帧号、相机标识或硬件配置。

VisionAlgorithm 完成两种模式下的全部图像处理、算法有效性处理、标定、坐标转换和多相机位姿融合，输出 `double` 类型的 X、Y、R 以及 `valid`。X、Y、R 的具体坐标系、单位、原点、方向、角度范围和转换方法均由 VisionAlgorithm 及其配置负责。`valid` 只表示算法最终输出是否合法；算法内部负责处理边、几何、Mark 点和融合细节。算法有效时，VisionCore 只校验 X、Y、R 的浮点有限性并形成 OK；算法无效时，VisionCore 忽略算法坐标并统一形成 X=0.0、Y=0.0、R=0.0 的 NG。

VisionAlgorithm 不负责硬件控制、TCP、UI、线程调度和上位机任务状态。

### 4.5 VisionTcp

VisionTcp 是基于 `QtCore + QtNetwork` 的非 UI TCP 静态库，负责 TCP Server、单客户端连接、Qt 事件驱动收发、报文拆组包、完整性校验、半包粘包、JSON、心跳、断线、网络命令和通信诊断。

VisionTcp 不创建网络线程，不依赖 VisionCore、VisionHardware、VisionAlgorithm 或 QtWidgets，不直接执行视觉任务，也不提供中止当前视觉任务的协议命令。

### 4.6 VisionTest

VisionTest 是统一测试项目，负责各生产项目单元测试、ApplicationController 协调测试和双线程集成测试。

第一阶段保留一个 VisionTest 逻辑项目或解决方案目录；如果 Qt、厂商 SDK、运行环境或测试入口不同，允许生成多个测试可执行目标，但不得因此拆分出新的业务项目。

---

## 5. 依赖方向

允许的主要项目依赖：

```text
VisionApp       → VisionCore
VisionApp       → VisionHardware
VisionApp       → VisionAlgorithm
VisionApp       → VisionTcp
VisionHardware  → VisionCore
VisionAlgorithm → VisionCore
VisionTest      → 所有被测试项目
```

允许的基础依赖：

```text
VisionCore      → C++ 标准库 / OpenCV Core 头文件与 API（可由 opencv_world 二进制承载）
VisionHardware  → 厂商 SDK / OpenCV Core
VisionAlgorithm → OpenCV Core / Imgproc / Calib3d / 其他经批准模块
VisionTcp       → QtCore / QtNetwork / nlohmann::json
VisionApp       → Qt / spdlog / nlohmann::json（仅在确有需要时）
```

VisionCore 只能依赖标准库、自身公共抽象和 OpenCV Core 头文件/API。VisionCore 使用 OpenCV 仅限公共图像承载和基础矩阵类型，不得包含或调用图像处理、标定、特征提取、文件读写或 UI 相关 OpenCV 功能。若统一 OpenCV 分发只提供 `opencv_world`，允许在链接阶段使用该库；依赖边界仍按源码实际包含的头文件和调用的 API 判断，不能因链接 world 库而扩大 VisionCore 职责。

禁止：

```text
VisionCore      → VisionApp / VisionTcp / VisionHardware / VisionAlgorithm / Qt
VisionTcp       → VisionCore / VisionHardware / VisionAlgorithm / QtWidgets
VisionHardware  → VisionApp / VisionTcp
VisionAlgorithm → VisionApp / VisionTcp
生产项目        → VisionTest
```

不得通过相互包含头文件、全局单例、隐式注册、静态初始化或回调反向引用绕过依赖方向。

---

## 6. 统一线程模型

生产代码只允许主动管理两个线程：

```text
1. Qt 主线程
2. 视觉工作线程
```

除上述两个线程外，生产代码不得主动创建临时线程、长期线程、线程池或异步执行器。

Qt、OpenCV、厂商 SDK、第三方算法库或日志库内部线程不计入应用主动管理线程数量，但其用途、生命周期、回调线程、资源所有权和关闭行为必须在对应项目文档中说明。不得使用无法明确关闭行为的第三方后台线程规避本线程模型。

### 6.1 Qt 主线程

Qt 主线程负责 `QCoreApplication` 或 `QApplication`、UI、ApplicationController、VisionTcp、socket、timer、连接状态、命令准入、应用状态、任务投递、结果接收和上位机回复。

Qt 主线程不得执行相机采图、光源控制、视觉算法、同步等待视觉任务、阻塞式 TCP 等待或长时间文件处理。

### 6.2 视觉工作线程

视觉线程负责 VisionWorker、VisionCore、VisionHardware 和 VisionAlgorithm 对象，以及视觉业务初始化、准备、同步 Locate 和资源释放。

可能具有厂商 SDK 线程亲和性的对象，必须优先在视觉线程创建、使用和销毁。

同一时刻只允许一个视觉任务。第一阶段不建立视觉线程池、多任务并行或通用任务队列。

### 6.3 跨线程通信

跨线程通信只在 VisionApp 内部发生，优先按值传递或使用不可变共享数据。

允许传递轻量命令、配置标识、按值的 LocateOptions、LocateOutcome、状态快照和稳定错误。

禁止跨线程共享可变 `cv::Mat` 或其他可变图像缓存，也禁止跨线程共享 QTcpSocket、UI 控件、厂商 SDK 对象、算法内部可变状态和所有权不明的裸指针。需要向 UI 传递图像时，必须先创建独立快照，并在 VisionApp 中完成 `QImage` 等 UI 类型转换。

已开始执行的视觉任务不支持跨线程取消，也不得通过 Qt queued signal、线程终止或其他方式尝试中止当前同步 Locate。

---

## 7. Locate 统一语义

VisionCore 的 Locate 是带定位模式的同步业务调用：

```cpp
LocateOutcome locate(const LocateOptions& options);
```

VisionCore 统一使用：

* `LocateMode`：本次定位模式，第一阶段只包含 `Edge` 和 `MarkPoint`；
* `LocateOptions`：本次同步调用参数，第一阶段只包含 `LocateMode mode`，不包含整体超时或算法超时；
* `LocateResult`：正常执行完成后的视觉结果，包含 `double` 类型 X、Y、R 和 OK 或 NG 判定；
* `Error`：VisionCore 定义的统一结构化错误，包含稳定 `ErrorCode`、可读说明和诊断上下文；
* `LocateOutcome`：`LocateResult` 或 `Error` 的明确结果类型，第一阶段使用基于 `std::variant` 的 C++20 类型表达。

Locate 一旦被接受并开始执行，就必须运行到以下任一结果：

1. 正常完成并返回 OK；
2. 正常完成并返回 NG；
3. 算法执行超过 VisionAlgorithm 自身配置的算法超时，或硬件 SDK 自身的有限等待到期，并返回 Error；
4. 某个硬件、算法或业务阶段失败并返回 Error。

算法业务超时、硬件 SDK 自身返回的超时和内部执行失败都属于 Error，不得伪装成 NG 或低质量成功结果。硬件超时不属于 Locate 的业务计时，只表示具体硬件调用的有限等待已经到期。

ApplicationController 完成：

```text
TcpCommand
→ 检查命令准入
→ 投递视觉线程
→ 将已确定的业务模式转换为 LocateOptions
→ VisionCore 同步 locate(options)
→ LocateOutcome
→ TcpMessage
```

定位模式进入 VisionCore 后使用稳定的 `LocateMode` 表达。网络标识、连接上下文和其他上位机协议字段只存在于 VisionApp 与 VisionTcp；本次文档变更不定义或修改 TCP 报文字段。

Busy 状态下新的 Locate 不排队，必须立即返回 Busy；QueryStatus 在 Busy 状态下仍可执行。

---

## 8. 算法业务超时与有限返回

系统不提供外部取消当前视觉任务的能力。一次 Locate 不设置整体超时或整体截止时间，业务只统计算法执行阶段的耗时。算法执行超时由 VisionAlgorithm 的稳定配置定义，由具体算法实现在算法调用内部识别并返回 `ErrorCode::AlgorithmTimeout`。VisionCore、VisionApp 和 Qt timer 不对正在执行的算法进行外部计时中断。

相机连接、光源控制、采图、事务清理和硬件关闭不计入算法业务超时，也不消耗算法超时预算。但“不计入业务超时”不等于允许无限阻塞；为了资源和关闭安全，硬件、SDK 调用及释放操作仍必须使用 SDK 原生有限等待或其他可验证的有限返回机制。

统一原则：

* 时间类型统一使用 `std::chrono`；
* 算法耗时和截止时间判断统一基于 `std::chrono::steady_clock`；
* 不得用 Qt timer 假定可以强制打断正在执行的同步函数；
* 不得用无单位整数表达超时；
* 算法超时属于 VisionAlgorithm 配置，不进入 `LocateOptions`，也不由 VisionCore 分配剩余时间；
* 算法业务计时范围从 VisionAlgorithm 开始处理本次算法输入起，到算法端口返回算法结果或统一 `Error` 为止；抓边模式覆盖单图处理、几何处理和标定，抓点模式覆盖四图 Mark 识别、各相机标定和位姿融合；相机采图、光源控制以及 VisionCore 的结果判定不计入该时间；
* 算法采用协作式超时检测，只要求在主要阶段边界检查截止时间，不要求把检查深入每个内部循环、OpenCV 调用或第三方算法内部；
* 同步算法阶段一旦开始便自然运行到返回，超过截止时间后在下一个检查点识别超时；算法配置表示业务超时预算，不是强制中断时刻或函数返回时间硬上限；
* VisionAlgorithm 不向 OpenCV 或第三方算法传递剩余时间，也不为每个内部调用建立第二层超时；
* 算法实现检测到超时后必须直接返回 `ErrorCode::AlgorithmTimeout`，VisionCore 不进行二次错误类型转换；
* 相机连接、采图、光源控制、资源释放和 SDK 关闭必须各自具有明确的有限返回方式；其耗时不改变算法超时，SDK 自身有限等待到期时返回 `ErrorCode::HardwareTimeout`；
* 算法超时配置和主要检查边界写入 `VisionAlgorithm/AGENTS.md`；硬件及关闭操作的有限等待配置写入 `VisionHardware/AGENTS.md`。

VisionCore 不维护整体截止时间，不向硬件或算法分配剩余时间，也不因为采图或光源阶段耗时较长而缩短算法超时。业务层不把完整 Locate 的总耗时作为超时判定依据。

当前阶段不要求根据各算法内部步骤推导完整 Locate 或关闭流程的理论最坏耗时。关闭安全依赖算法在阶段边界协作检查并自然返回，以及硬件和 SDK 操作使用自身有限等待。

算法业务超时和硬件 SDK 自身超时均由对应实现项目识别，并直接转换为 `ErrorCode::AlgorithmTimeout` 或 `ErrorCode::HardwareTimeout`。VisionCore 原样传播稳定错误，不在外部重复计时或建立第二层错误类型。

---

## 9. 状态与生命周期

应用状态、视觉服务状态、任务状态和 TCP 连接状态必须分别建模，不得混用。

统一原则：

* 每类状态有唯一所有者；
* UI 只显示状态，不拥有业务状态；
* TCP 已连接不等于视觉服务 Ready；
* ApplicationController 和 VisionCore 都要防御重复任务；
* 状态转换不得依赖字符串比较；
* 失败状态必须保存可诊断错误；
* 恢复操作必须有明确入口；
* 已开始的视觉任务不支持外部取消；
* 应用关闭时只允许拒绝新任务并等待当前任务自然返回。

启动顺序：

```text
创建 Qt 应用
→ 初始化配置和日志
→ 启动视觉线程
→ 在视觉线程创建并配置具体硬件和算法对象
→ 将已配置的硬件和算法端口注入 VisionCore
→ 初始化 VisionCore
→ VisionCore Ready
→ 启动 TCP
→ 应用进入 Ready
```

关闭必须采用非阻塞状态流程：

```text
应用进入 ShuttingDown
→ 拒绝新的 Locate 和其他业务命令
→ 停止接受新的 TCP 连接或任务命令
→ 当前 Locate 如正在执行则继续运行到正常、NG、错误或超时返回
→ 在视觉线程执行硬件关闭和对象释放
→ VisionWorker 发出 shutdownCompleted
→ 主线程请求视觉线程退出
→ 视觉线程 finished
→ 销毁 UI 和应用对象
```

Qt 主线程不得通过同步等待当前 Locate 的方式阻塞事件循环。不得在视觉线程事件循环停止后再依赖 `deleteLater()` 完成核心对象清理。

VisionCore 管理单次视觉事务的流程清理和恢复动作；VisionHardware 和 VisionAlgorithm 负责自身资源的 RAII 生命周期；VisionApp 负责对象图和线程生命周期。

不得强制终止视觉线程。关闭期间等待当前同步算法在后续检查点自然返回，再执行硬件关闭、SDK 释放和线程退出；当前阶段不要求提供应用最大关闭等待时间的精确数值。

---

## 10. 数据、坐标与所有权

公共业务数据优先使用标准 C++ 类型。

统一规则：

* 时间使用 `std::chrono`；
* 不用无单位整数表达超时；
* 浮点结果必须校验有限性；
* 公共枚举不得依赖隐式整数语义；
* 最终 X、Y、R 固定使用 `double` 承载，不附加坐标系、单位或角度元数据。

最终定位结果包含 `double` 类型 X、Y、R 和 OK/NG 判定。VisionAlgorithm 负责完成全部坐标和角度计算，并保证有效输出满足当前设备与上位机既定约定；VisionCore、VisionApp 和 VisionTcp 不解释坐标系、单位、原点、正方向、角度归一化范围或绝对/相对位姿。算法有效时，VisionCore 必须使用 `std::isfinite()` 校验 X、Y、R，NaN 或无穷值返回 `ErrorCode::InvalidResult`；算法无效时属于正常 NG，Core 不使用算法坐标，最终 X、Y、R 固定为 `0.0`。

只有 OK 结果中的 X、Y、R 可以进入设备动作或打标流程。NG 结果固定携带 `0.0`、`0.0`、`0.0` 作为协议占位，不得驱动设备动作。

### 10.1 公共多图帧类型

OpenCV 是经批准的解决方案级基础依赖。VisionHardware 通过 VisionCore 定义的 `ImageFrames` 统一返回本次采集的全部图像，集合元素为 `ImageFrame`，其中像素载体固定使用 `cv::Mat`。`ImageFrame` 除采集元数据外还必须携带稳定的 `CameraRole`，用于说明图像来自中心相机或哪个角相机。VisionCore 使用定位模式、集合数量、相机角色、采集时间、帧号和相机标识进行完整性与一致性判断、状态记录和诊断关联；校验完成后，只以强类型输入中的 `const cv::Mat` 引用将像素传给 VisionAlgorithm，不传递采集元数据。

第一阶段固定相机角色为：

* `Center`；
* `TopLeft`；
* `TopRight`；
* `BottomRight`；
* `BottomLeft`。

`ImageFrame` 至少包含：

* `cv::Mat image`；
* 采集时间戳；
* 帧号；
* 相机标识；
* 相机角色。

`ImageFrames` 至少包含一个 `std::vector<ImageFrame>`。结构一致性契约：

* `Edge` 模式必须恰好包含一帧，角色必须为 `Center`；
* `MarkPoint` 模式必须恰好包含四帧，并且四个角相机角色各出现一次；
* 同一集合内 `cameraId` 必须非空且互不重复；
* 不依赖集合元素顺序判断相机角色；
* 不要求不同相机的 `frameNumber` 相同或全局递增；
* 任一帧缺失、角色重复、角色与模式不匹配或帧契约失败时，Core 不调用算法并返回 `InvalidImage`；
* 四角相机采集是一次同步硬件端口调用和一个完整结果，VisionHardware 不得以部分成功的 `ImageFrames` 代替 Error。

公共图像契约：

* 生产视觉流程的图像格式固定为 `CV_8UC1`，表示单通道 8 位无符号灰度图；
* 公共图像必须非空且连续存储，`image.isContinuous()` 必须为 true；
* VisionHardware 必须在图像离开硬件边界前完成格式校验或转换；
* VisionHardware 返回的 `cv::Mat` 必须拥有独立、有效且生命周期明确的像素存储；
* 不得返回引用厂商临时缓冲、回调缓冲或下一帧会覆盖缓冲的 `cv::Mat`；必要时必须在 VisionHardware 内部使用 `clone()` 或等价方式取得独立存储；
* VisionAlgorithm 的两类算法输入都使用 `const cv::Mat` 引用，只在同步调用期间读取，不得保存引用或像素裸指针供调用结束后使用；
* VisionAlgorithm 不得修改输入像素；需要原地处理时，必须先使用 `clone()` 或等价方式创建独立工作图像；
* 同一 `ImageFrames` 中各帧必须拥有彼此独立的有效像素缓冲；`cv::Mat` 的浅拷贝不得被用来绕过只读和所有权约束；
* 跨线程不得共享可变 `cv::Mat`；传递给 UI 的显示图像必须是独立快照；
* 算法内部创建的 ROI、非连续矩阵和其他格式中间图像可以存在，但不得作为公共输入或公共输出跨越模块边界。

如果性能测试证明从厂商缓冲复制到独立 `cv::Mat` 不可接受，必须先提交方案，明确缓冲租约、释放线程、并发只读规则、SDK 回收时机和异常清理后，才允许引入共享不可变缓冲或内存池。

除明确批准的 `cv::Mat` 外，公共接口不得暴露 Qt、JSON、厂商 SDK 或其他第三方模板匹配类型。其他第三方类型必须限制在实现项目内部，并转换为项目自有类型。

---

## 11. 错误边界

跨项目错误只分为本地实现错误和 VisionCore 统一稳定错误，不再建立第二层错误类型或重复映射。

### 11.1 本地实现错误

* VisionHardware 识别厂商 SDK、相机、光源以及图像缓冲转换过程中的本地错误；
* VisionAlgorithm 识别 OpenCV、第三方算法和算法实现的本地错误；
* VisionTcp 识别连接、报文和协议错误；
* VisionApp 识别组合、线程协调、展示和对外消息映射错误。

本地错误不得直接跨越项目公共接口。

### 11.2 VisionCore 统一稳定错误

VisionCore 定义一套跨硬件、算法和核心流程使用的稳定错误契约：

* `ErrorCode`：稳定错误类别，至少覆盖 NotReady、Busy、InvalidConfiguration、HardwareUnavailable、HardwareFailure、HardwareTimeout、InvalidImage、AlgorithmFailure、AlgorithmTimeout、InvalidResult 和 InternalFailure；
* `Error`：包含 `ErrorCode code`、稳定可读的 `message` 和仅用于诊断的 `diagnostic`；
* `Outcome<T>`：第一阶段使用 `std::variant<T, Error>` 表达成功值或错误；
* `Status`：使用 `std::variant<std::monostate, Error>` 表达无返回值操作的成功或错误；
* `LocateOutcome`：`Outcome<LocateResult>`。

VisionHardware 和 VisionAlgorithm 必须在各自模块边界捕获或识别本地错误，并直接转换为上述统一 `Error`。VisionAlgorithm 的公开同步创建入口也属于模块边界：配置路径、参数、标定、模板资源缺失、格式错误或彼此不兼容时返回 `InvalidConfiguration`，创建过程中的未预期失败返回 `InternalFailure`，不得返回部分初始化对象。VisionCore 对端口返回的稳定错误原则上原样传播，在自身发现 NotReady、Busy、InvalidImage、InvalidResult 或 InternalFailure 时创建对应的新 `Error`；当构造时缺少必需端口，或收到不属于 Edge/MarkPoint 的 `LocateMode` 时，VisionCore 还必须防御性创建 `InvalidConfiguration`。VisionApp 仍负责在组合阶段防止空端口进入正常启动流程，并根据 `ErrorCode` 映射 UI 和 TCP 对外消息。

厂商原始错误码、`cv::Exception::what()`、第三方异常文本和详细调用上下文只能进入 `diagnostic` 或结构化日志。`message` 用于稳定的人类可读说明；业务控制流只能判断 `ErrorCode`，不得解析 `message` 或 `diagnostic`。

统一禁止：

* 将厂商错误码直接返回上位机；
* 将 TCP 协议错误放入 VisionCore；
* 通过异常文本判断错误类别；
* 使用一个通用 `int errorCode`、布尔失败标志或字符串表示全部错误；
* 吞掉错误后返回伪成功结果；
* 将超时返回为 NG；
* 依赖第三方错误枚举的隐式整数值。

异常只允许在 Hardware、Algorithm 或 Core 的实现边界内部传播，并必须在公开接口返回前转换为统一 `Error`，不得跨越公共接口。VisionAlgorithm 的私有构造函数和内部配置加载函数可以使用异常简化内部失败传播，但公开创建入口必须完成统一转换。OK 和 NG 是正常业务结果，不属于错误。

---

## 12. 配置、诊断、日志与文件

第一阶段不建立独立 VisionInfra 项目。

配置归属：

* 应用启动配置和 JSON 解析由 VisionApp 管理；
* TCP 配置类型由 VisionTcp 定义；
* 相机和光源配置类型由 VisionHardware 定义；
* 算法配置文件格式、路径引用规则、内部配置类型和解析校验由 VisionAlgorithm 定义并保持在其实现边界内，其中算法执行超时在解析后使用有单位的 `std::chrono` 类型；VisionAlgorithm 不要求向 VisionApp 公开内部配置类型；
* VisionApp 负责解析应用启动配置并确定算法主配置文件路径，在视觉线程将该路径传给 VisionAlgorithm 的公开同步创建入口；VisionAlgorithm 在创建入口内部同步加载并校验主配置、算法参数、标定和模板资源，创建成功返回已经完成配置和初始化的 `ILocateAlgorithm` 端口，创建失败返回统一 `Error`；
* VisionApp 使用 VisionHardware 定义的稳定配置类型完成硬件配置，并将已经完成配置和初始化的硬件、算法端口注入 VisionCore；
* VisionCore 定义的硬件和算法抽象接口不得引用 VisionHardware 或 VisionAlgorithm 的配置类型，具体配置不得经由 VisionCore 转发；
* VisionCore 只接收由自身定义的业务校验和流程执行所需的稳定业务数据，不接收 JSON、具体实现配置、整体 Locate 超时或算法实现超时；
* 第一阶段 VisionCore 提供 `locate(const LocateOptions&)`，其中只传递本次定位模式；运行配方、硬件参数、算法参数和算法超时必须在对象注入 Core 前完成配置，不通过单次 Locate 调用传递。

日志由 VisionApp 初始化和统一落盘，统一日志后端使用 spdlog。生产库不得各自创建互不关联的全局 logger，也不得通过 `spdlog::get()` 等全局查找方式形成隐式依赖。

VisionCore 定义仅依赖标准库的结构化诊断事件和窄诊断接口，VisionHardware 和 VisionAlgorithm 可通过该接口输出诊断信息。VisionTcp 通过自身 Qt 信号或窄接口输出通信诊断。VisionApp 负责汇聚、格式化、过滤并写入 spdlog。VisionCore 不依赖 spdlog。

诊断接口不得让 VisionCore 依赖具体日志库，不得成为业务控制流，不得因日志写入失败改变正常视觉结果，除非日志本身是明确的法规或追溯必需功能。

文件写入必须明确输出目录、命名、失败处理、容量清理和是否允许阻塞当前线程。

调试图像、日志、缓存、生成目录、本机路径和用户私有配置不得提交到 Git，也不得在源码中写死 SDK 或绝对路径。

---

## 13. TCP 会话与任务准入

VisionTcp 只负责连接和协议，ApplicationController 负责视觉任务准入和网络上下文关联。

第一阶段统一采用以下规则：

* 同一时刻只允许一个 Locate；
* Busy 时新的 Locate 不排队，立即返回 Busy；
* QueryStatus 在 Busy 状态下仍可执行；
* 不提供 CancelCommand；
* 客户端断线不会中止已开始的 Locate；
* App 必须保存当前任务对应的连接代次或会话上下文；
* 旧连接产生的结果不得发送给新连接；
* 如果任务完成时原连接已经断开，结果只进入状态和诊断，不向新连接补发；
* 新连接可以查询当前应用和视觉服务状态，但不能接管旧连接的在途任务。

网络 RequestId、SessionId 和连接代次只存在于 VisionApp 与 VisionTcp，不进入 VisionCore。

---

## 14. 工程与编码统一规则

所有生产项目统一使用 x64、C++20、UTF-8、相同 Runtime Library、明确的 Debug/Release 配置，不使用开发机绝对包含路径，不依赖未提交的本机文件。所有使用 OpenCV 的项目必须使用同一 OpenCV 版本、同一架构和匹配的 Debug/Release 库，不得混用不同 OpenCV 安装或不同 Runtime Library。

解决方案级禁止：

* 过度模板化和无必要继承；
* 无边界的 Manager、Helper、Utils；
* 为单一实现建立多层包装；
* 全局可变单例；
* 将所有类改成 QObject；
* 将纯 C++ 项目无必要地 Qt 化；
* 将同步业务无必要地异步化；
* 使用后台线程包装不可中断调用来伪造超时返回；
* 依赖线程强制终止保证退出；
* 在第一阶段同时引入 QtNetwork 和 Asio 两套网络栈；
* 让 nlohmann::json、spdlog 或除 `cv::Mat` 外的第三方类型进入 VisionCore 公共业务接口。

只有需要 Qt 事件循环、信号槽或 QObject 所有权时才使用 QObject。

---

## 15. 测试原则

所有核心功能必须能够在没有真实设备时测试。

```text
VisionCore       → Fake Hardware + Fake Algorithm
VisionAlgorithm  → 固定图像和确定性输入
VisionHardware   → 适配器测试与单独硬件集成测试
VisionTcp        → 协议测试和本地回环测试
VisionApp        → Fake Core/Tcp 的协调测试
Integration      → 双线程完整链路测试
```

统一要求：

* Core 测试不依赖 Qt；
* 算法测试不依赖相机；
* TCP 测试不启动真实视觉任务；
* 单元测试不依赖外部上位机；
* 不使用长时间 `sleep` 作为主要同步手段；
* 异步测试必须有明确超时；
* 失败时必须输出足够诊断；
* 必须测试 OK、NG、NotReady、Busy、InvalidConfiguration、HardwareUnavailable、HardwareFailure、HardwareTimeout、InvalidImage、AlgorithmFailure、AlgorithmTimeout、InvalidResult 和 InternalFailure；
* 必须验证 NG 结果的 X、Y、R 始终为 `0.0`，且算法返回的 NG 坐标不会进入最终结果；
* 必须分别测试 `Edge` 和 `MarkPoint` 模式，并验证 LocateOptions 模式被正确传递到采集端口和算法输入选择；
* 必须验证 Edge 返回恰好一张 Center 图，MarkPoint 返回四个唯一角相机角色；缺帧、重复角色、错误角色、重复 cameraId 和任一非法帧均不得进入算法；
* 必须验证算法计时从 VisionAlgorithm 开始处理对应强类型只读图像输入起算，采图、元数据判断、光源控制和 Core 结果判定不属于算法超时范围；
* 必须验证算法在检查点发现截止时间已到时返回 `AlgorithmTimeout`，而不是 NG、低质量成功或通用错误；
* 必须验证硬件有限等待错误、事务清理和关闭顺序；当前阶段不要求通过测试推导完整 Locate 或关闭流程的理论最坏耗时；
* 必须验证关闭期间拒绝新任务、当前任务自然返回、对象在线程内释放和线程最终退出；
* 必须验证客户端断线后旧任务结果不会发送给新连接；
* 必须验证公共 `ImageFrames` 中每个 `ImageFrame` 均为非空、`CV_8UC1`、连续存储并拥有独立有效缓冲；
* 必须验证 VisionAlgorithm 通过两种强类型输入中的 `const cv::Mat` 引用读取图像、不保存调用期引用且不修改输入图像；
* 必须验证厂商临时缓冲失效或下一次采集后，已返回的全部 `ImageFrame` 仍然有效；
* 必须验证帧相关诊断事件可以关联一帧或四帧元数据，非帧事件不携带帧上下文；
* 必须验证厂商错误、`cv::Exception`、标准异常和图像格式错误会被转换为 VisionCore 统一 `Error`，并保留正确的稳定 `ErrorCode`。

真实硬件测试必须单独标记，不进入默认自动化测试集合。

---

## 16. 子目录 AGENTS.md 分工

| 文件 | 主要描述内容 |
| --- | --- |
| `VisionApp/AGENTS.md` | Controller、Worker、Qt 信号槽、双线程调度、UI 边界、启动关闭状态机、命令和结果映射、会话代次 |
| `VisionCore/AGENTS.md` | LocateMode/LocateOptions、ImageFrames、相机角色、多图一致性、双形态只读算法输入、统一 Error/Outcome、LocateOutcome、同步流程、状态、资源安全和抽象接口 |
| `VisionHardware/AGENTS.md` | 中心及四角相机实现、相机角色映射、按模式返回 ImageFrames、硬件配置、SDK 生命周期、缓冲所有权、SDK 有限等待、有限关闭和错误转换 |
| `VisionAlgorithm/AGENTS.md` | 抓边与四角抓点流程、双形态只读输入、标定与多相机位姿融合、valid 语义、算法配置与业务超时、OpenCV 边界和测试数据 |
| `VisionTcp/AGENTS.md` | 固定报文、JSON、QTcpServer/QTcpSocket、心跳、连接、会话、Busy 和 Command/Message |
| `VisionTest/AGENTS.md` | `include/` 通用测试头、`source/` 按模块组织的专用 Fake、Fixture 和测试文件、Qt 测试入口、超时、隔离和覆盖范围 |

根目录不得重复记录上述接口级实现细节；子目录也不得重新定义解决方案项目划分、线程模型、取消语义、两种固定定位模式、X/Y/R 由 VisionAlgorithm 负责生成且 Core 只校验有限性的边界，或统一错误归属。

---

## 17. 修改流程

修改前必须确认修改所属项目，以及是否影响公共接口、依赖方向、线程归属、业务结果、通信协议、超时保证、坐标语义和对应 `AGENTS.md`。

下列修改必须先给出方案并等待确认：

* 新增或删除项目；
* 改变项目依赖；
* 改变双线程模型；
* 修改 Locate 公共语义；
* 增加或取消外部任务中止能力；
* 修改坐标和角度定义；
* 修改 TCP 协议；
* 修改硬件或算法公共接口；
* 修改公共 CameraRole、ImageFrame、ImageFrames、`cv::Mat` 格式、集合一致性、连续性、只读语义或所有权模型；
* 修改统一 `Error`、`ErrorCode`、`Outcome<T>` 或 `Status` 公共类型；
* 引入新第三方库，或升级 OpenCV、Qt、spdlog、nlohmann::json、厂商 SDK；
* 改变网络技术栈或引入 Asio；
* 大范围重命名或移动文件；
* 增加任何应用主动管理线程；
* 引入异步框架、线程池或消息总线；
* 引入已知可能永久阻塞且没有安全退出方式的硬件或算法调用。

未经确认不得借当前任务“顺手整理”无关模块。

---

## 18. 禁止事项

未经明确要求不得：

* 恢复旧项目划分；
* 新建 VisionController 或 VisionInfra 项目；
* 在 VisionCore 中使用 Request/Response 或加入 Qt；
* 在 VisionTcp 中执行视觉流程；
* 在 MainWindow 中实现完整业务调度；
* 在主线程执行硬件或算法；
* 创建独立网络线程；
* 创建临时工作线程包装算法调用；
* 强制终止视觉线程；
* 提供 CancelCommand 或外部中止当前 Locate 的接口；
* 除公共图像契约明确批准的 `cv::Mat` 外，暴露第三方内部类型；
* 返回引用厂商临时缓冲、回调缓冲或下一帧会覆盖缓冲的 `cv::Mat`；
* 修改算法端口两种输入形态中的只读 `cv::Mat` 像素内容；
* 跨线程共享可变 `cv::Mat`；
* 在第一阶段引入 Asio 或第二套网络事件循环；
* 修改最终坐标和结果语义；
* 将算法超时或硬件超时判定为 NG；
* 升级 Qt、OpenCV、spdlog、nlohmann::json 或厂商 SDK；
* 修改第三方源码；
* 提交构建产物、日志、缓存或本机配置；
* 声称未实际执行的编译、运行或测试已经通过。

---

## 19. 交付要求

每次修改完成后必须说明：

* 修改文件和目的；
* 实际行为变化；
* 公共接口和依赖变化；
* 线程及对象所有权变化；
* 图像格式、`cv::Mat` 连续性、只读语义和所有权变化；
* 错误和生命周期影响；
* 超时语义影响；
* TCP 会话和任务准入影响；
* 测试内容与真实结果；
* 未完成项；
* 已知风险；
* 需要开发者手动验证的内容。


## 20. 批准的基础技术依赖

* OpenCV 是解决方案级基础依赖；公共图像帧允许使用 `cv::Mat`，生产视觉流程的公共图像格式固定为 `CV_8UC1`。VisionCore 只允许包含和调用 OpenCV Core 头文件/API，VisionAlgorithm 可以依赖经批准的 OpenCV 算法模块。统一分发只提供 `opencv_world` 时允许 VisionCore 链接该二进制库，但不得据此使用任何非 Core API。
* TCP 和网络通信统一使用 QtCore + QtNetwork。第一阶段不使用 Asio；引入 Asio 属于网络基础设施和线程模型变更，必须先提交方案并获得确认。
* JSON 实现允许使用 `nlohmann::json`，但只能存在于 VisionApp 和 VisionTcp 的实现边界。VisionApp 使用 VisionHardware 的稳定 C++ 配置类型初始化硬件；VisionAlgorithm 自行解析其主配置文件及引用的参数、标定和模板资源，VisionApp 只向算法公开同步创建入口提供主配置文件路径。JSON、算法配置路径、具体实现配置和算法超时数值不得进入 VisionCore 公共业务接口。
* 日志后端统一使用 spdlog，由 VisionApp 创建、配置、刷新和关闭；其他生产库不得创建独立全局 logger。
* Qt、OpenCV、spdlog、nlohmann::json 和厂商 SDK 的版本、构建方式与部署位置必须统一管理，任何升级或替换都属于受控变更。
