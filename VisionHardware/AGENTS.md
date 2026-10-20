# VisionHardware 项目设计

## 0. 文档定位

本文定义 VisionHardware 的海康 MVS 适配、五相机配置、`MV_LC_INTERFACE` 光源控制器、共享 SDK 生命周期、触发采集、图像所有权、状态恢复和当前无设备阶段的验证边界。本文服从根目录和 `VisionCore/AGENTS.md`，不得重新定义 `LocateMode`、`CameraRole`、`ImageFrame`、`ImageFrames` 或统一 `Error`。

VisionHardware 是 x64、C++20、UTF-8 静态库，同时实现 VisionCore 的 `ICamera` 和 `ILight`。第一阶段只适配海康 MVS C SDK、GigE 相机和可由 `MV_CC_EnumInterfaces(MV_LC_INTERFACE, ...)` 枚举的海康光源控制器，不创建线程、锁、SDK 图像回调或异步执行器。

---

## 1. 分层和生命周期

固定采用共享内部状态和两个 Core 端口的结构：

```text
                VisionCore
             ICamera  ILight
                 ↑      ↑
            CameraPort LightPort
                  \    /
             HikHardwareState
                    ↓
          共享的海康 MVS SDK Runtime
              ↙                 ↘
        五台海康相机        MV_LC 光源控制器
```

`HikHardwareState` 是 VisionHardware 内部共享状态，统一拥有 SDK Runtime、五台相机和一个光源控制器。CameraPort 和 LightPort 只允许由同一个公开硬件组合创建入口成对产生，并以共享内部状态实现各自的 Core 接口。禁止分别创建会独立调用 `MV_CC_Initialize()` 或 `MV_CC_Finalize()` 的相机系统和光源系统，也禁止使用全局单例、静态初始化或全局引用计数隐藏所有权。

公开创建结果语义固定为：

```cpp
struct HikHardwarePorts {
    std::unique_ptr<vision::core::ICamera> camera;
    std::unique_ptr<vision::core::ILight> light;
};

using HikHardwareCreateOutcome =
    vision::core::Outcome<HikHardwarePorts>;

struct HikLightControllerConfig;

[[nodiscard]] HikHardwareCreateOutcome createHikHardware(
    HikCameraSystemConfig cameraConfig,
    HikLightControllerConfig lightConfig,
    ICaptureObserver* captureObserver = nullptr,
    vision::core::IDiagnosticSink* diagnostics = nullptr);
```

`HikLightControllerConfig` 在第 3 节定义；声明时可以通过前置声明或调整类型声明顺序满足 C++ 完整类型要求，不得因此改变接口语义。公开同步创建入口按值接收相机和光源配置，接收可选采集观察者和诊断接口，成功时同时返回两个非空端口，失败时只返回统一 `Error`，不得返回部分初始化组合。公开头文件不得暴露 `MV_INTERFACE_INFO`、设备句柄或其他海康 SDK 类型。

`MvCamera` 是私有单设备薄封装。一对象拥有一个设备句柄，只转发打开关闭、连接查询、节点读写、启停取流、清缓存、取还帧、最优包大小和取流策略等 SDK 调用，并原样返回海康 `int` 错误码。它不依赖 VisionCore 或 OpenCV，不认识角色、型号、触发模式和系统配置，绝不初始化、结束或枚举全局 SDK。

光源控制器句柄同样由私有薄封装拥有，只负责 `MV_CC_CreateInterface`、`MV_CC_OpenInterface`、节点访问、`MV_CC_CloseInterface` 和 `MV_CC_DestroyInterface`。它不解释 `LocateMode`，不初始化或结束全局 SDK，不启用官方示例中的 `CardPacketReceived0` 事件通知和回调。

初始化顺序：

```text
相机与光源纯配置校验
→ 全局 MV_CC_Initialize() 一次
→ 记录 SDK 版本
→ MV_CC_EnumInterfaces(MV_LC_INTERFACE)
→ 按序列号创建并打开光源控制器
→ 强制关闭白色、R、G、B 全部输出
→ 写入并回读固定亮度
→ 枚举 GigE 设备
→ 按序列号和型号族映射五个角色
→ 逐台打开并配置
→ 同时返回 CameraPort 和 LightPort
```

算法对象由 VisionApp 在硬件组合前创建；VisionHardware 不创建或持有算法。初始化中途失败时按反向顺序最佳努力关闭已打开的相机和控制器句柄并结束 SDK。任何失败都不得留下可用的单端口对象。

正常关闭顺序固定为：

```text
LightPort::turnOff()
→ CameraPort::shutdown() 停止残留取流并关闭全部相机
→ LightPort::shutdown() 再次最佳努力关灯并关闭、销毁控制器句柄
→ 共享状态确认两个端口都已停止
→ MV_CC_Finalize() 一次
```

单个 Stop、Close、Destroy 或关灯失败不得短路其他资源清理。两个端口的 `shutdown()` 都必须幂等；全局 SDK 只能在两个端口均不再使用硬件后结束。析构只作为 `noexcept` 最佳努力兜底，正常流程必须通过显式 shutdown 保留稳定错误。

硬件组合、两个端口、全部句柄和共享状态必须在视觉线程创建、使用、关闭和销毁。VisionHardware 不主动创建任何线程，也不使用回调或后台任务绕过同步端口模型。

---

## 2. 五相机和公共配置

第一阶段固定角色和型号：

| 角色 | 型号族 | 全画幅 |
|---|---|---:|
| Center | MV-CH250-90GM | 5120×5120 |
| TopLeft | MV-CS120-10GM | 4024×3036 |
| TopRight | MV-CS120-10GM | 4024×3036 |
| BottomRight | MV-CS120-10GM | 4024×3036 |
| BottomLeft | MV-CS120-10GM | 4024×3036 |

`HikCameraSystemConfig` 必须显式提供五个 `HikCameraDeviceConfig`、`HikTriggerConfig` 和有单位的 `captureTimeout`：

* 五个角色各出现一次，序列号非空且互不重复；
* Center 只能使用 `MvCh25090Gm`，四角只能使用 `MvCs12010Gm`；
* 型号字符串允许已知 V 版本后缀，基础型号、全画幅和必需节点能力仍必须匹配；
* `captureTimeout` 位于 `[1ms, UINT_MAX ms]`，不提供生产默认值；
* 显式曝光必须有限、为正且能由 SDK float 表达；
* 显式增益必须有限且能由 SDK float 表达，单位保持相机 `Gain` 节点原生单位；
* 触发延时必须有限、非负且能由 SDK float 表达；
* 生产触发输入只接受 Line0 或 Line2，不接受 Line1；
* 配置结构错误在进入 SDK 前返回 `InvalidConfiguration`。

曝光或增益未配置时，不写对应 Auto 模式和数值，只做可选诊断读取。显式配置时必须先关闭对应 Auto 节点，再写值并读取设备实际量化值。

`HIKSDK.props` 通过 `HikSdkRoot` MSBuild 属性确定 SDK 根目录；未显式传入时读取 MVS 安装设置的 `MVCAM_COMMON_RUNENV`，项目文件不得固化开发机绝对路径。

---

## 3. 光源控制器配置与模式语义

项目术语统一使用 `RGB` 表示红、绿、蓝三通道；历史材料中的 `RBG` 视为笔误，不进入类型名、配置字段或诊断字段。

### 3.1 稳定配置类型

第一阶段目标配置语义固定为：

```cpp
enum class HikLightChannelRole {
    WhiteBacklight,
    CoaxialRed,
    CoaxialGreen,
    CoaxialBlue
};

struct HikLightChannelConfig {
    HikLightChannelRole role;
    std::string outputSelector;
    double normalizedIntensity;
};

struct HikLightControllerConfig {
    std::string serialNumber;
    std::array<HikLightChannelConfig, 4> channels;
};
```

`normalizedIntensity` 是与设备原始整数或浮点单位解耦的归一化亮度，范围为 `[0.0, 1.0]`。VisionApp 只解析并填充上述稳定配置，VisionHardware 负责把它映射到真机亮度或电流节点的实际类型、最小值、最大值和步长。

进入 SDK 前必须完成：

* 控制器序列号非空；
* `WhiteBacklight`、`CoaxialRed`、`CoaxialGreen`、`CoaxialBlue` 四个角色各出现一次；
* 四个 `outputSelector` 非空且互不重复；
* 每个亮度均为有限浮点数并位于 `[0.0, 1.0]`；
* 白色背光亮度严格大于 `0.0`；
* R、G、B 至少一个通道的亮度严格大于 `0.0`；
* 任一规则不满足时在调用 MVS SDK 前返回 `InvalidConfiguration`。

控制器必须通过 `MV_CC_EnumInterfaces(MV_LC_INTERFACE, ...)` 枚举，并按配置序列号精确匹配，禁止按枚举下标绑定。枚举得到的型号、显示名和序列号只用于能力核验、诊断和日志，不进入 Core 公共接口。

### 3.2 初始化与亮度回读

控制器打开后必须先保持所有输出关闭，再逐通道：

```text
选择 outputSelector
→ 查询真正亮度或电流节点的类型、范围、步长和访问权限
→ 将 normalizedIntensity 映射到最近的设备合法值
→ 写入设备节点
→ 回读设备实际量化值
→ 验证回读符合该节点的步长和量化规则
```

控制器缺少必需的输出选择、开关或真实亮度节点，节点不可写，或回读不满足设备能力时返回 `InvalidConfiguration`。节点访问过程中发生 SDK 通信失败时按连接状态返回 `HardwareFailure` 或 `HardwareUnavailable`。

海康示例中的 `TimerDuration` 和 `TimerDelay` 表示定时脉冲参数，不得被当作亮度或电流节点。第一阶段不配置脉冲持续时间、延迟或触发源，也不启用控制器事件回调。真实亮度节点尚未由实物资料确认前，只能完成接口枚举和能力记录，不得猜测节点名进入生产实现。

### 3.3 ILight 模式行为

第一阶段只提供持续照明，不在 Locate 期间动态调色或修改亮度：

```text
turnOn(LocateMode::Edge)
→ 最佳努力关闭 White/R/G/B 全部输出
→ 只开启 WhiteBacklight

turnOn(LocateMode::MarkPoint)
→ 最佳努力关闭 White/R/G/B 全部输出
→ 按启动时固定亮度开启 R/G/B 中亮度大于 0.0 的通道

turnOff()
→ 最佳努力关闭 White/R/G/B 全部输出
```

模式切换前的全关操作用于避免白色背光和 RGB 同轴光同时点亮。任一 `turnOn(mode)` 步骤失败时必须继续尝试关闭所有输出，然后返回最先发生的稳定错误；额外清理错误进入 diagnostic。`turnOff()` 必须尝试全部四个输出，单通道失败不得短路其余通道。

`recover()` 在共享 SDK Runtime 保持初始化的前提下重新枚举控制器、按原序列号重建句柄、强制关闭全部输出、重新写入并回读固定亮度，成功时停留在全关状态。恢复不得切换配方、重新解析配置或循环调用 `MV_CC_Finalize()`/`MV_CC_Initialize()`。

收到不属于 `Edge` 或 `MarkPoint` 的枚举值属于防御性 `InvalidConfiguration`。正常流程应由 VisionCore 在调用 Hardware 前拒绝未知模式。

---

## 4. 相机节点配置

首次打开和断线重连统一执行 `configureSlot()`：

```text
TriggerMode=Off
→ GigE 最优包大小
→ Offset=0、Binning=1、Decimation=1
→ Width/Height=节点当前最大值
→ 校验型号全画幅
→ ReverseX/ReverseY=false
→ PixelFormat=Mono8
→ 可选的曝光和增益配置
→ AcquisitionMode=Continuous
→ TriggerSelector=FrameStart
→ TriggerSource 和硬触发输入节点
→ 可选关闭 TriggerCache/帧率控制
→ OneByOne 取流策略
→ TriggerMode=On
→ 关键节点回读和启动诊断
```

必需节点缺失、不可满足要求或精确回读不一致返回 `InvalidConfiguration`；节点访问查询或 SDK 通信失败按连接状态返回 `HardwareFailure` 或 `HardwareUnavailable`。可选节点缺失、查询或写入失败只输出 Warning，不改变 Ready 结果。

软件触发只把 `TriggerSource` 配成 Software。硬触发支持 Line0/Line2、上升沿/下降沿和触发延时；Line2 还必须配置为输入。Line1、相机输出光源控制和 MV-CS Global Reset 写入不属于当前阶段。光源只通过独立的 `MV_LC_INTERFACE` 控制器端口操作，不自动回退到相机 Line 输出。

---

## 5. 采集端口和触发语义

唯一端口保持：

```cpp
CaptureOutcome capture(LocateMode mode) override;
```

`Edge` 只选择 Center；`MarkPoint` 以 TopLeft、TopRight、BottomRight、BottomLeft 的稳定执行顺序选择四角。顺序只用于确定性调用和诊断，结果语义由每帧 `CameraRole` 表达。

通用准备流程为 Start 全部目标相机后 Clear 全部目标缓存。任一步失败都停止所有已经 Start 的相机并返回整体 Error，不返回部分帧。

软件触发流程：

```text
Start all → Clear all → 建立共同 deadline
→ 每台发送一次 TriggerSoftware
→ 在同一 deadline 内顺序 Get/Copy/Free
→ Stop all
```

任一 TriggerSoftware 失败后不再触发或取帧，不自动重发。

硬触发流程：

```text
Start all → Clear all → onPrepared(mode)
→ 建立共同 deadline → 等待外部脉冲和目标帧
→ Get/Copy/Free → Stop all
```

硬触发必须提供非拥有型 `ICaptureObserver`。Prepared 表示本次目标相机均已进入触发模式、开始取流并清空旧缓存；准备失败时不通知，每次 `capture()` 最多通知一次。观察者只允许快速转发 VisionWorker 的 Qt 信号，不得执行 TCP、等待或硬件操作。Hardware 不等待 Prepared 网络报文实际发出。

四角取帧共享一次 `steady_clock` 截止窗口，每次 Get 使用剩余毫秒；未触发或未收齐返回 `HardwareTimeout`。任何采集失败都丢弃已复制帧并进入 Faulted。Stop、Free 等清理错误优先作为最终 Error，原始错误追加到 diagnostic。

---

## 6. ImageFrames 所有权

每个成功返回的 `ImageFrame` 必须满足：

* SDK 像素格式为 Mono8，公共图像为非空、连续的 `CV_8UC1`；
* Center 尺寸严格为 5120×5120，四角严格为 4024×3036；
* `capturedAt` 使用 `steady_clock` 接收时刻，`frameNumber` 使用 SDK 帧号；
* `cameraId` 使用配置序列号，`cameraRole` 使用稳定角色；
* 在 `FreeImageBuffer` 前通过 `clone()` 深复制到独立存储；
* 同一结果中各帧不共享可变像素缓冲；
* 下一次采集、恢复或 SDK 缓冲复用不得使旧帧失效。

不得将 SDK 临时缓冲、`MV_FRAME_OUT`、设备句柄或其他海康类型暴露到公共头文件或 VisionCore。

---

## 7. 状态、错误、诊断和恢复

共享生命周期与两个端口的活动子状态分别建模：

```text
共享生命周期：Initializing → Ready → Faulted → Ready after recover
               Ready/Faulted → ShuttingDown → Stopped
Camera 子状态：Idle → Capturing → Idle
Light 子状态： Off → EdgeOn/MarkPointOn → Off
```

Capturing 时重复调用采集防御性返回 `Busy`；共享生命周期非 Ready 时调用采集或开灯返回 `NotReady`。正常 Locate 中 Camera 必须能够在 Light 为 EdgeOn 或 MarkPointOn 时采集。Core 保证串行调用开灯、采集和关灯，Hardware 状态仍必须防御非法重入。LightPort 成功 `turnOn(mode)` 后记录当前照明模式，但该记录不替代设备回读或安全关灯。

稳定错误映射：

```text
结构、型号、尺寸、角色、通道映射、节点能力或参数非法 → InvalidConfiguration
相机或控制器未枚举、无法打开或已断开             → HardwareUnavailable
SDK 明确报告的有限等待或取帧超时                 → HardwareTimeout
Trigger/Start/Clear/Stop/Free、控制器节点读写、回读、关闭或销毁失败
                                                   → HardwareFailure
帧格式、尺寸、长度或缓冲非法                     → InvalidImage
未预期异常                                       → InternalFailure
```

厂商错误码只以十六进制进入 `Error::diagnostic`。相机错误记录 operation、role、cameraId 和 model；光源错误记录 operation、controllerSerial、controllerModel、channelRole 和 nodeName。业务控制流不得解析 diagnostic。

CameraPort 恢复先最佳努力停止残留取流；仍连接设备执行 Start/Clear/Stop 清除事务状态；断开设备销毁旧句柄，重新枚举并按原序列号匹配，只重开和完整配置断开的角色。不得复用旧 `MV_CC_DEVICE_INFO`。

LightPort 恢复先最佳努力关闭全部输出；控制器断开时销毁旧接口句柄，重新执行 `MV_CC_EnumInterfaces(MV_LC_INTERFACE, ...)` 并按原序列号匹配，打开后保持全关并重新应用固定亮度。任一端口恢复都不得循环 Finalize/Initialize 全局 SDK；只有两个恢复部分都成功，共享状态才返回 Ready。

诊断通过非拥有型 `IDiagnosticSink` 输出，提交失败不得改变采集结果。Prepared 功能通知与诊断事件分离，诊断不得驱动业务流程。

---

## 8. Release|x64 无设备阶段验证

当前工程一致性修正允许将 Debug 工程元数据统一为 x64、`/MDd` 和匹配依赖，但不构建也不声明 Debug 通过；Debug 正式验收以匹配的 OpenCV Debug 二进制为前置条件。自动化验收只针对 `Release|x64`。当前阶段不模拟真机结果，不伪造海康 C SDK，也不把无设备冒烟测试解释为设备链路验证。默认验证包含：

* VisionHardware `Release|x64` 编译；
* 相机和光源公共配置在进入 SDK 前的全部结构和数值校验；
* 硬触发缺少 Prepared `ICaptureObserver` 时，`createHikHardware()` 在 SDK 初始化前返回 `InvalidConfiguration`；
* `MvCamera` 和 `MvLightController` 不可复制、不可移动的单组编译期约束；
* Core 继续通过 Fake `ICamera` 和 Fake `ILight` 测试，不为 Core 伪造海康 SDK；
* 代码审查确认全局 SDK 只由共享状态初始化和结束一次，成功 Get 均配对 Free，所有已 Start 路径都尝试 Stop，所有关灯路径都尝试四个输出，关闭不因单个设备失败短路。

无设备 SDK 初始化、`MV_LC_INTERFACE` 枚举、不存在的控制器返回 `HardwareUnavailable`、无部分 Camera/Light 端口和最终 Finalize 属于 `DISABLED_HikMvsEnvironmentSmokeTests.MissingControllerReturnsNoPartialPorts`，只能被精确过滤显式执行，不进入默认测试。当前不创建 `HikHardwareDeviceTests` 源文件或占位用例。

相机、光源控制器、交换机和 PLC 到位后的设备联调属于后续独立阶段，必须依据实物固件、节点表、电气连接和网络拓扑重新确认参数与验收范围。当前静态设计只冻结接口和安全边界，不代表 `MV_LC_INTERFACE` 枚举、四通道能力或亮度节点已经通过真机验证。

---

## 9. 光源真机资料门槛

进入控制器节点写入的生产实现前，必须从实物铭牌、MVS 枚举结果、官方型号资料或设备 XML 记录：

* 控制器型号、序列号、显示名和连接方式；
* 控制器能够被当前 MVS 运行时的 `MV_LC_INTERFACE` 枚举；
* `TimerSelector` 或等价输出选择节点的全部符号值及其与白色、R、G、B 接线的对应关系；
* `LineStatus` 或等价输出开关节点存在、可写且能够回读；
* 真正亮度或电流节点的名称、节点类型、访问模式、最小值、最大值和步长；
* 控制器至少提供四个可独立控制的输出通道；
* 控制器相关同步 SDK 调用具有可验证的有限返回行为。

任一事实缺失时，允许完成配置类型、纯校验、枚举和错误转换，但必须停止亮度及通道控制的生产实现。不得用 `TimerDuration`、`TimerDelay` 冒充亮度，不得暗中改用相机 Line I/O，不得通过新增后台线程伪造超时。

---

## 10. 当前明确不开发

* ROI、可配置 Binning 或 Decimation；
* Locate 执行期间动态修改曝光、增益或触发模式；空闲状态下的变更由 VisionApp 关闭并重建完整运行时生效；
* Action Command、SDK 图像回调、后台采集线程和并发任务；
* 自动重发软件触发；
* GevSCPD/GevSCFTD 带宽调度；
* Line1 光源输出和 MV-CS Global Reset 模式写入；
* 相机 I/O 光源控制及 `MV_LC_INTERFACE` 失败后的自动回退；
* `TimerDuration`、`TimerDelay`、频闪、外部触发或其他脉冲照明；
* Locate 期间动态修改白色或 R/G/B 亮度；
* 控制器事件回调、后台监控线程和并发光源操作；
* Locate 期间调光、光源 TCP 命令、TCP Prepared 报文字段和 PLC 业务时序。VisionApp 可以编辑光源配置草稿，并在空闲状态下通过重建完整运行时应用。
