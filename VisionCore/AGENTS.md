# VisionCore 项目设计

## 0. 文档定位

本文定义 VisionCore 第一阶段的详细方案，包括公共类型、统一错误、硬件和算法抽象接口、同步 Locate 流程、状态、资源所有权、恢复、关闭和测试要求。

本文服从根目录 `AGENTS.md`。VisionCore 的公共类型和接口一旦进入其他项目，修改即属于受控公共接口变更，必须先说明影响并获得确认。

VisionCore 必须配置为 x64、C++20、UTF-8 的静态库，同时提供 Debug 和 Release 配置，并与其他项目使用相同 Runtime Library 和 OpenCV 版本。

---

## 1. 职责与边界

VisionCore 负责：

* 定义 `LocateMode`、`LocateOptions`、`CameraRole`、`ImageFrame`、`ImageFrames`、两类算法输入、算法输出、最终定位结果和核心状态；
* 定义统一 `ErrorCode`、`Error`、`Outcome<T>` 和 `Status`；
* 定义相机、光源和定位算法的同步抽象接口；
* 组织模式选择、开灯、采图、采集完成后关灯、多图一致性校验、算法定位、结果校验和清理流程；
* 使用 ImageFrames 及其逐帧元数据完成输入完整性判断、模式一致性判断、状态记录和诊断关联；
* 将算法有效性转换为最终 OK 或 NG；
* 防御重复 Locate；
* 保存最近一次稳定错误；
* 管理单次视觉事务清理、Faulted 状态恢复和有序关闭；
* 通过窄诊断接口输出结构化诊断事件。

VisionCore 不负责：

* Qt 对象、信号槽、事件循环或线程创建；
* TCP、JSON、RequestId、SessionId、连接代次或上位机报文；
* 相机、光源、算法配置类型及其 JSON 解析；
* 厂商 SDK、OpenCV 图像处理、标定或模板匹配实现；
* 抓边、Mark 点识别、几何判断、标定、多相机位姿融合，以及 X、Y、R 的坐标计算、单位、坐标系、原点、方向或角度归一化；
* 算法超时配置、计时、外部中断或整体 Locate 超时；
* 日志文件、spdlog、UI 图像转换或调试图像落盘。

VisionApp 必须先在视觉线程使用 VisionHardware 的稳定配置类型创建并配置具体 Hardware，并使用算法主配置文件路径构造具体 Algorithm；VisionAlgorithm 在构造期间自行完成内部配置与资源加载。VisionApp 只将已经完成配置和初始化的端口对象注入 VisionCore，VisionCore 不提供具体实现配置或配置路径转发接口。

---

## 2. 依赖与目录

VisionCore 只允许依赖：

* C++ 标准库；
* OpenCV Core 头文件与 API。

当解决方案统一提供的 OpenCV 二进制只有 `opencv_world` 时，允许 VisionCore 和 VisionCore 测试链接该 world 库作为 Core API 的二进制载体。依赖边界按源码实际包含的头文件和调用的 API 判断；链接 world 库不表示允许使用其中的非 Core 能力。

禁止依赖 Qt、VisionApp、VisionHardware、VisionAlgorithm、VisionTcp、JSON、spdlog、厂商 SDK，禁止包含或调用 OpenCV Imgproc、Calib3d、Features2d、Highgui、Imgcodecs 等非 Core 头文件/API。

计划文件结构：

```text
VisionCore
├─ include
│  └─ VisionCore
│     ├─ Error.h
│     ├─ ImageFrame.h
│     ├─ LocateTypes.h
│     ├─ Ports.h
│     ├─ Diagnostics.h
│     └─ VisionCore.h
├─ src
│  └─ VisionCore.cpp
└─ AGENTS.md
```

第一阶段不建立 Common、Utils、Manager、异步包装器或额外公共基类。

所有公共类型放入 `vision::core` 命名空间。头文件只暴露完成公共契约所需的最小依赖。

---

## 3. 统一错误与结果类型

### 3.1 ErrorCode

第一阶段固定使用以下稳定错误码：

```cpp
enum class ErrorCode {
    NotReady,
    Busy,
    InvalidConfiguration,
    HardwareUnavailable,
    HardwareFailure,
    HardwareTimeout,
    InvalidImage,
    AlgorithmFailure,
    AlgorithmTimeout,
    InvalidResult,
    InternalFailure
};
```

语义：

| ErrorCode | 语义 |
| --- | --- |
| `NotReady` | Core 当前状态不允许执行请求 |
| `Busy` | 已有 Locate 正在执行，拒绝重复任务 |
| `InvalidConfiguration` | App、Hardware 或 Algorithm 初始化时发现配置无效，或 Core 防御性发现必需端口为空、LocateMode 不受支持 |
| `HardwareUnavailable` | 相机、光源未连接或不可用 |
| `HardwareFailure` | 硬件操作或清理失败 |
| `HardwareTimeout` | SDK 自身有限等待到期，不属于算法业务计时 |
| `InvalidImage` | 图像为空、格式错误、不连续或违反公共图像契约 |
| `AlgorithmFailure` | 算法正常边界内的执行失败 |
| `AlgorithmTimeout` | VisionAlgorithm 自身配置的算法执行超时 |
| `InvalidResult` | 算法输出 X、Y、R 包含 NaN、无穷或违反稳定结果契约 |
| `InternalFailure` | 未预期异常、内部不变量破坏或未知程序错误 |

不得增加与某个厂商错误码一一对应的公共枚举。只有 UI、TCP、恢复动作或业务处理确实需要区分时，才允许增加稳定 `ErrorCode`。

### 3.2 Error

```cpp
struct Error {
    ErrorCode code;
    std::string message;
    std::string diagnostic;
};
```

规则：

* `code` 是唯一允许参与业务判断的字段；
* `message` 是稳定、简洁、可读的说明，但 TCP 不得依赖其文本内容判断类别；
* `diagnostic` 保存厂商错误码、异常文本、操作名称和必要上下文，只用于日志和诊断；
* `diagnostic` 不得携带密码、密钥或其他敏感信息；
* 不得通过比较、搜索或解析 `message`、`diagnostic` 判断错误类型；
* 不得把厂商错误枚举或第三方异常类型加入公共接口。

### 3.3 Outcome 与 Status

第一阶段使用标准库类型，不引入第三方 expected 实现：

```cpp
template<typename T>
using Outcome = std::variant<T, Error>;

using Status = std::variant<std::monostate, Error>;
```

具体别名：

```cpp
using CaptureOutcome = Outcome<ImageFrames>;
using AlgorithmOutcome = Outcome<AlgorithmOutput>;
using LocateOutcome = Outcome<LocateResult>;
```

规则：

* `T` 表示正常完成；
* `Error` 表示操作未正常完成；
* `std::monostate` 表示无返回值操作成功；
* OK 和 NG 都属于 `LocateResult`，不属于 `Error`；
* 公共接口不得同时使用返回码、输出参数和异常表达同一个失败；
* Hardware 和 Algorithm 返回的稳定 `Error` 原则上由 Core 原样传播，不再包装为第二层错误类型。

---

## 4. 公共数据类型

### 4.1 LocateMode 与 LocateOptions

```cpp
enum class LocateMode {
    Edge,
    MarkPoint
};

struct LocateOptions {
    LocateMode mode;
};
```

规则：

* `LocateMode` 只表达本次定位使用抓边还是四角抓点；
* 模式必须由每次 `locate(options)` 显式传入，不得固定在 VisionCore 构造配置中；
* `LocateOptions` 第一阶段只包含 `mode`，不包含算法超时、整体超时、硬件参数、配方对象或协议字段；
* `Edge` 使用一张中心相机图像；
* `MarkPoint` 使用四张角相机图像；
* 两种模式共享相同的最终 `LocateResult`、错误、状态和清理语义。

### 4.2 ImageFrame 与 ImageFrames

```cpp
enum class CameraRole {
    Unknown,
    Center,
    TopLeft,
    TopRight,
    BottomRight,
    BottomLeft
};

struct ImageFrame {
    cv::Mat image;
    std::chrono::steady_clock::time_point capturedAt;
    std::uint64_t frameNumber;
    std::string cameraId;
    CameraRole cameraRole;
};

struct ImageFrames {
    std::vector<ImageFrame> frames;
};
```

公共图像不隐藏 OpenCV。VisionHardware 的单一相机端口根据 `LocateMode` 返回 `ImageFrames`，不为中心和四角相机分别增加 Core 端口。`cameraId` 表示具体相机标识，`cameraRole` 表示该图像在定位系统中的稳定业务角色。

VisionCore 对每个 `ImageFrame` 负责防御性校验：

* `image.empty()` 必须为 false；
* `image.type()` 必须为 `CV_8UC1`；
* `image.isContinuous()` 必须为 true；
* `capturedAt` 不得为默认构造的空时间点；
* `cameraId` 必须非空；
* `cameraRole` 不得为 `Unknown`；
* 图像像素必须由返回的 `cv::Mat` 独立持有，不能引用厂商临时缓冲；
* 下一帧采集、SDK 缓冲复用或 Hardware 内部对象变化不得使旧帧失效。

VisionCore 还必须校验集合一致性：

* `Edge` 模式：`frames.size() == 1`，唯一角色为 `Center`；
* `MarkPoint` 模式：`frames.size() == 4`，并且 `TopLeft`、`TopRight`、`BottomRight`、`BottomLeft` 各出现一次；
* 同一集合内 `cameraId` 必须互不重复；
* 集合顺序不构成角色语义，Core 必须按 `cameraRole` 选择图像；
* 任一帧非法、数量错误、角色缺失、角色重复、角色与模式不符或相机标识重复时，返回 `InvalidImage`，不得调用 Algorithm；
* `MarkPoint` 的四帧必须由同一次 `capture(mode)` 返回；端口不接受部分成功。

VisionCore 使用 `capturedAt` 判断逐帧采集元数据是否有效，使用 `frameNumber`、`cameraId` 和 `cameraRole` 关联本次状态与诊断。第一阶段允许 `frameNumber == 0`，不要求不同相机的帧号相同，也不假设不同相机 SDK 的帧号全局递增。

VisionCore 校验完成后构造只包含 `const cv::Mat` 引用的算法输入。`capturedAt`、`frameNumber`、`cameraId` 和 `cameraRole` 不进入算法端口。

VisionCore 不对正常图像进行额外 `clone()`。独立缓冲责任属于 VisionHardware。

### 4.3 AlgorithmInput 与 AlgorithmOutput

```cpp
struct EdgeAlgorithmInput {
    std::reference_wrapper<const cv::Mat> center;
};

struct MarkPointAlgorithmInput {
    std::reference_wrapper<const cv::Mat> topLeft;
    std::reference_wrapper<const cv::Mat> topRight;
    std::reference_wrapper<const cv::Mat> bottomRight;
    std::reference_wrapper<const cv::Mat> bottomLeft;
};

using AlgorithmInput =
    std::variant<EdgeAlgorithmInput, MarkPointAlgorithmInput>;
```

输入规则：

* `EdgeAlgorithmInput` 只包含 Center 图像；
* `MarkPointAlgorithmInput` 按稳定角色提供四张角相机图像，不依赖 `ImageFrames` 的容器顺序；
* 输入只在同步端口调用期间有效；
* Core 不再向 Algorithm 额外传递 `LocateMode`；输入 variant 的具体类型即表示算法形态；
* Algorithm 不得获得或依赖采集元数据。

```cpp
struct AlgorithmOutput {
    double x;
    double y;
    double r;
    bool valid;
};
```

语义：

* X、Y、R 是 VisionAlgorithm 完成全部图像处理、标定和坐标转换后的最终数据；
* VisionCore 不理解或转换坐标系、单位、原点、方向和角度范围；
* `valid` 只表示 Algorithm 最终输出是否合法；边、几何、Mark 点、标定和融合过程中的具体处理均由 Algorithm 内部完成；
* `valid == true` 时 Core 校验 X、Y、R 有限并形成 OK；
* `valid == false` 时 Core 忽略 AlgorithmOutput 中的 X、Y、R，统一形成 X=0.0、Y=0.0、R=0.0 的 NG；
* 只有 `valid == true` 时算法 X、Y、R 才必须是有限 `double`；
* NG 结果中的 X、Y、R 固定作为协议占位，不得用于设备运动或打标；
* 算法分数、残差、特征数量等实现细节默认进入结构化诊断，不进入第一阶段 Core 公共结果；只有形成稳定业务需求后才评估增加公共字段。

### 4.4 LocateResult

```cpp
enum class LocateDecision {
    Ok,
    Ng
};

struct LocateResult {
    double x;
    double y;
    double r;
    LocateDecision decision;
};
```

规则：

* `LocateResult` 只表示算法正常返回且 Core 完成结果校验；
* `decision == Ok` 时 X、Y、R 可以进入上位机正常结果流程；
* `decision == Ng` 时 X、Y、R 必须严格等于 `0.0`，上位机和 VisionApp 不得使用它们驱动设备动作；
* 算法超时、硬件超时、异常、图像错误和无效浮点结果不得构造 `LocateResult`。

### 4.5 CoreState 与状态快照

```cpp
enum class CoreState {
    Ready,
    Locating,
    Faulted,
    ShuttingDown,
    Stopped
};

struct CoreStatus {
    CoreState state;
    std::optional<Error> lastError;
};
```

`CoreStatus` 是按值返回的只读快照，不暴露内部可变状态。

---

## 5. 抽象端口

所有端口均为同步纯 C++ 接口，不提供 callback、Future、协程、取消令牌或线程控制。

### 5.1 ICamera

```cpp
class ICamera {
public:
    virtual ~ICamera() = default;

    virtual CaptureOutcome capture(LocateMode mode) = 0;
    virtual Status recover() = 0;
    virtual Status shutdown() = 0;
};
```

要求：

* 具体相机对象在注入前已经完成配置和初始化；
* `capture(mode)` 是唯一采集入口，返回满足公共契约并独立持有像素的 `ImageFrames`；
* `Edge` 返回一张 Center 图像，`MarkPoint` 返回四张角色完整的角相机图像；
* `MarkPoint` 采集作为一个整体返回，任一必要相机失败时返回 Error，不返回部分成功集合；
* 具体实现负责把厂商设备或序列号稳定映射到 `CameraRole`；
* `recover()` 执行相机实现允许的有限恢复动作；
* `shutdown()` 必须有限返回且可重复调用；
* SDK 返回码和异常必须在 VisionHardware 内转换为统一 `Error`。

### 5.2 ILight

```cpp
class ILight {
public:
    virtual ~ILight() = default;

    virtual Status turnOn(LocateMode mode) = 0;
    virtual Status turnOff() = 0;
    virtual Status recover() = 0;
    virtual Status shutdown() = 0;
};
```

要求：

* 具体光源对象在注入前已经完成控制器、通道和固定亮度配置；
* Core 只把稳定的 `LocateMode` 原样传给 `turnOn(mode)`，不认识白色、R/G/B 通道、亮度或厂商节点；
* `turnOn(LocateMode::Edge)` 由 Hardware 解释为白色背光配方，`turnOn(LocateMode::MarkPoint)` 由 Hardware 解释为 RGB 同轴光配方；
* `turnOn(mode)`、`turnOff()` 必须有限返回；
* `turnOff()` 必须关闭该实现管理的全部光源输出，并尽量设计为幂等操作；
* `recover()` 必须恢复固定配置并使实现回到全部输出关闭、可以安全执行下一次 `turnOn(mode)` 的状态；
* `shutdown()` 必须有限返回且可重复调用。

### 5.3 ILocateAlgorithm

```cpp
class ILocateAlgorithm {
public:
    virtual ~ILocateAlgorithm() = default;

    virtual AlgorithmOutcome locate(const AlgorithmInput& input) = 0;
};
```

要求：

* 具体算法对象在注入前已经完成配置、模板加载和算法超时设置；
* `input` 只在同步调用期间有效；
* 实现根据 `AlgorithmInput` 的具体类型进入抓边或抓点流程；
* 实现不得保存输入中的 `const cv::Mat` 引用、`image.data` 或由其派生的共享视图供调用结束后使用；
* 实现不得修改任一输入图像像素；
* 需要原地算法时，必须先 `clone()` 到独立工作图像；
* 抓边的四边识别、几何处理和标定均在 VisionAlgorithm 内；
* 抓点的四个 Mark 识别、各相机标定和最终位姿融合均在 VisionAlgorithm 内；
* Algorithm 负责算法业务计时和 `AlgorithmTimeout`；
* `cv::Exception`、标准异常和第三方错误必须在 VisionAlgorithm 边界转换为统一 `Error`。

端口接口不得出现 VisionHardwareConfig、VisionAlgorithmConfig、JSON、Qt 或厂商 SDK 类型。

---

## 6. 对象所有权与线程约束

VisionApp 是组合根。VisionWorker 在视觉线程中：

```text
创建并配置具体 Algorithm
→ 创建并配置共享 SDK 生命周期的 Hardware 组合
→ 从 Hardware 组合取得 Camera 和 Light 端口 unique_ptr
→ 构造 VisionCore 并转移端口所有权
```

VisionCore 使用 `std::unique_ptr<ICamera>`、`std::unique_ptr<ILight>` 和 `std::unique_ptr<ILocateAlgorithm>` 独占端口对象，避免裸指针悬空和销毁顺序不明确。

公共构造形式：

```cpp
VisionCore(
    std::unique_ptr<ICamera> camera,
    std::unique_ptr<ILight> light,
    std::unique_ptr<ILocateAlgorithm> algorithm,
    IDiagnosticSink* diagnostics = nullptr);
```

规则：

* camera、light、algorithm 不得为空；
* 空端口属于组合错误，App 必须在构造 Core 前防止该错误；一旦发现空端口，不得启动 TCP；
* 若空端口仍被传入，VisionCore 必须防御性进入 `Faulted`，保存并报告 `InvalidConfiguration`，后续业务操作不得因空指针崩溃；
* `diagnostics` 是可选非拥有指针，若非空必须比 VisionCore 活得更久；
* VisionCore、端口对象及其具体实现必须在视觉线程创建、调用、关闭和销毁；
* VisionCore 不是线程安全类型，不使用 mutex；
* 不得从 Qt 主线程直接调用 Core；
* 销毁前必须调用 `shutdown()`；
* 即使显式 shutdown 失败，所有析构函数也必须 `noexcept`，并执行实现允许的最佳努力清理；
* 不得在线程事件循环停止后依赖 `deleteLater()` 清理 Core 或端口对象。

---

## 7. 诊断接口

VisionCore 定义不依赖日志库的窄接口：

```cpp
enum class DiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct FrameDiagnosticContext {
    std::chrono::steady_clock::time_point capturedAt;
    std::uint64_t frameNumber;
    std::string cameraId;
    CameraRole cameraRole;
};

struct DiagnosticEvent {
    DiagnosticSeverity severity;
    std::string component;
    std::string operation;
    std::string message;
    std::vector<FrameDiagnosticContext> frames;
};

class IDiagnosticSink {
public:
    virtual ~IDiagnosticSink() = default;
    virtual void submit(const DiagnosticEvent& event) noexcept = 0;
};
```

规则：

* 诊断只用于观察，不得成为业务控制流；
* sink 写入失败不得改变 Locate 结果；
* sink 实现不得把异常传播回 Core；
* 与采集帧有关的事件必须在 `frames` 中携带全部相关帧的 capturedAt、frameNumber、cameraId 和 cameraRole；
* Edge 帧事件通常携带一个上下文，MarkPoint 集合级或融合级事件可以携带四个上下文；
* 只与某一帧有关的校验错误可以只携带该帧上下文；
* 非帧事件使用空 `frames`；
* VisionApp 负责添加墙钟时间、线程信息、格式化并写入 spdlog；
* 发生“主要错误 + 清理错误”时，返回清理错误，主要错误必须作为诊断事件保留。

---

## 8. VisionCore 公共操作

第一阶段只提供：

```cpp
LocateOutcome locate(const LocateOptions& options);
CoreStatus status() const;
Status recover();
Status shutdown();
```

`LocateOptions` 第一阶段只携带本次定位模式。运行配方、相机参数、光源参数、算法参数和算法超时仍在对象注入 Core 前完成配置。

### 8.1 locate()

`locate(options)` 是同步调用，不支持外部取消。完整流程：

```text
检查 CoreState
→ 校验 options.mode 是 Edge 或 MarkPoint
→ Ready 改为 Locating
→ light.turnOn(options.mode)
→ camera.capture(options.mode)
→ camera.capture 返回后立即无条件尝试 light.turnOff()
→ 仅在采集成功且关灯成功时继续处理
→ 校验 ImageFrames 中每帧契约
→ 按 options.mode 校验集合数量、角色和相机标识一致性
→ 按角色构造 EdgeAlgorithmInput 或 MarkPointAlgorithmInput
→ 调用一次 Algorithm
→ valid=true 时校验 X/Y/R 有限性并形成 OK
→ valid=false 时忽略算法坐标并形成 0.0/0.0/0.0 的 NG
→ 更新状态和 lastError
→ 返回 LocateResult 或 Error
```

具体规则：

1. `Ready` 才允许开始 Locate；
2. `Locating` 返回 `ErrorCode::Busy`，不得排队；
3. `Faulted`、`ShuttingDown` 或 `Stopped` 返回 `ErrorCode::NotReady`；
4. 不受支持的 `LocateMode` 在任何 Hardware 端口调用前返回 `InvalidConfiguration`，不得开灯、关灯、采图或调用算法；
5. 合法模式的状态必须在任何端口调用前切换为 `Locating`；
6. Core 必须把本次 `LocateMode` 原样传给 `ILight::turnOn(mode)` 和 `ICamera::capture(mode)`；
7. 开灯失败时跳过采集，但仍最佳努力调用一次 `turnOff()`；采集返回成功或 Error 后也必须立即调用一次 `turnOff()`；
8. 采集成功且关灯成功后，Core 才允许校验图像并调用算法；关灯失败时不得进入图像校验后的算法处理阶段；
9. 图像校验、算法失败、算法超时、NG 或结果校验发生时，灯光已经关闭，不得因此再次调用 `turnOff()`；
10. Core 必须防御性校验 `ImageFrames` 的逐帧契约和模式一致性，不信任具体 Hardware 一定正确；
11. Core 根据模式和相机角色构造强类型只读算法输入，只调用一次 Algorithm，不复制正常像素；
12. `AlgorithmOutput::valid == true` 时，Core 使用 `std::isfinite()` 校验 X、Y、R 并形成 OK；
13. `AlgorithmOutput::valid == false` 时，不检查或传播算法坐标，最终 NG 坐标固定为 `0.0`；
14. OK 和 NG 都表示完整流程正常结束；
15. 已接受 Locate 的执行错误必须保存到 `lastError`，成功返回 OK 或 NG 时清空；
16. Busy 和 NotReady 属于准入拒绝，不得覆盖 Faulted 状态中原有的 `lastError`；
17. 公共方法不得向调用方传播异常。

### 8.2 采集错误与关灯错误优先级

关灯是采集阶段的结束动作，并且发生在图像校验和算法之前。单次 Locate 的采集阶段可能同时发生开灯/采集错误和关灯错误，第一阶段采用以下确定规则：

| 开灯与采集 | 关灯 | 后续处理 | 对外返回 |
| --- | --- | --- | --- |
| 成功 | 成功 | 执行图像校验和算法 | `LocateResult` 或处理阶段原 `Error` |
| Error | 成功 | 不执行 | 原开灯或采集 `Error` |
| 成功 | Error | 不执行 | 关灯 `Error` |
| Error | Error | 不执行 | 关灯 `Error`，开灯或采集 Error 写入诊断 |

原因是关灯失败代表当前硬件安全状态不确定，必须优先暴露并进入 `Faulted`，同时不能在灯光状态未知时继续执行图像校验后的算法阶段。图像或算法处理只会在关灯成功后发生，因此处理阶段错误不再触发第二次关灯。

### 8.3 异常兜底

Hardware 和 Algorithm 应在自身边界转换异常。VisionCore 仍必须在 `locate()`、`recover()` 和 `shutdown()` 最外层兜底捕获：

* 已知 `std::exception` 转换为 `InternalFailure`，`what()` 进入 diagnostic；
* 未知异常转换为 `InternalFailure`；
* 开灯或采集阶段发生异常后仍尝试执行一次关灯清理；
* 图像校验或算法阶段发生异常时灯光已经关闭，不再重复关灯；
* 清理异常同样不得逃出 Core；
* 不得吞掉异常后返回 OK 或 NG。

---

## 9. 状态机

状态转换：

```text
构造完成 → Ready
构造缺少必需端口 → Faulted，并保存 InvalidConfiguration

Ready → locate(不受支持的 mode) → Faulted，并保存 InvalidConfiguration，不调用任何端口
Ready → locate(受支持的 mode) → Locating
Locating → OK/NG → Ready
Locating → AlgorithmFailure/AlgorithmTimeout/InvalidResult → Ready
Locating → HardwareUnavailable/HardwareFailure/HardwareTimeout/InvalidImage → Faulted
Locating → InternalFailure/关灯失败 → Faulted

Faulted → recover() 成功 → Ready
Faulted → recover() 失败 → Faulted

Ready/Faulted → shutdown() → ShuttingDown → Stopped
Stopped → shutdown() → Stopped
```

规则：

* `Busy` 和 `NotReady` 不改变当前状态，也不覆盖已有 `lastError`；
* 算法失败、算法超时和无效算法结果发生前已经确认关灯成功，不默认破坏硬件状态，因此回到 Ready；
* 硬件错误、非法公共图像、内部错误或关灯失败进入 Faulted；
* Faulted 必须保存 `lastError`；
* 成功恢复或下一次成功 Locate 后清除 `lastError`；
* 状态只能由 VisionCore 修改，外部只读取快照。

---

## 10. recover()

`recover()` 只用于 `Faulted`：

```text
检查 Faulted
→ light.recover()
→ light.turnOff()
→ camera.recover()
→ 全部成功后进入 Ready
```

规则：

* 任一恢复步骤失败时保持 Faulted，并保存、返回该 `Error`；
* 即使某个恢复步骤失败，也必须继续尝试后续安全恢复步骤；
* 恢复过程中发生多个错误时，优先返回光源无法恢复或无法关灯的错误，其余错误写入诊断；
* `recover()` 不重新解析配置，不切换配方，不重建线程；
* `Ready` 调用 `recover()` 可以直接返回成功，保持幂等；
* `Locating` 返回 Busy；
* `ShuttingDown` 或 `Stopped` 返回 NotReady；
* 每个恢复端口调用必须有限返回。

---

## 11. shutdown()

VisionApp 必须在当前 Locate 返回后，在视觉线程调用：

```text
状态改为 ShuttingDown
→ light.turnOff()
→ camera.shutdown()
→ light.shutdown()
→ 状态改为 Stopped
```

规则：

* 第一阶段算法对象依赖 RAII 清理，不增加无明确需求的算法 shutdown 端口；
* 即使前一步失败，也必须继续尝试后续关闭；
* 多个关闭错误中优先返回光源无法关闭或无法关灯的错误，其余错误写入诊断；
* `shutdown()` 可重复调用，Stopped 状态再次调用直接成功；
* `Locating` 状态调用 shutdown 返回 Busy，不改变状态；正常关闭由 App 在当前 Locate 返回后发起；
* `shutdown()` 不强制中止 Locate；
* 所有关闭步骤必须有限返回；
* 返回 Error 时仍进入 Stopped，表示 Core 不再接受业务请求；错误由 App 记录并进入关闭诊断。

---

## 12. 算法超时边界

VisionCore 不创建 deadline，不读取算法超时，不测量算法耗时，也不缩减算法剩余时间。

算法调用：

```cpp
AlgorithmOutcome result = algorithm_->locate(input);
```

其中：

* 计时从 VisionAlgorithm 开始处理 `AlgorithmInput` 起；
* Edge 算法计时包含四边识别、几何处理、标定和最终输出；
* MarkPoint 算法计时包含四张图像的 Mark 识别、各相机标定、位姿融合和最终输出；
* VisionAlgorithm 使用自身配置的 `std::chrono` 超时；
* 算法检测到超时后返回 `ErrorCode::AlgorithmTimeout`；
* Core 原样传播该 Error；
* 采图、光源控制、图像校验、结果校验和关灯不计入算法超时；
* Core 不使用后台线程或 Qt timer 包装算法调用；
* VisionAlgorithm 只在主要阶段边界协作检查截止时间，配置时长不构成同步算法调用的强制中断时刻；
* 当前阶段不要求 Core 或集成测试推导完整 Locate 的理论最坏耗时。

---

## 13. 图像只读与所有权

第一阶段采用简单的逻辑只读方案，不引入自定义不可变缓冲：

```cpp
virtual AlgorithmOutcome locate(const AlgorithmInput& input) = 0;
```

约束：

* 两种输入形态中的每个图像都以 `const cv::Mat` 引用提供；
* Algorithm 不得通过 `const_cast`、浅拷贝 Mat 头或保存 `data` 指针绕过只读；
* 需要修改时必须 `clone()`；
* Core 不在生产路径计算图像哈希或执行额外深拷贝；
* 只读正确性由接口 const、代码评审和自动测试共同保证；
* Hardware 独立缓冲正确性由适配器测试保证；
* 如果将来需要编译期强不可变缓冲，属于公共 ImageFrame 和所有权模型变更，必须先提交方案。

---

## 14. 错误转换规则

### 14.1 VisionHardware

```text
未连接/设备不存在      → HardwareUnavailable
SDK 普通失败           → HardwareFailure
SDK 有限等待到期       → HardwareTimeout
格式转换或缓冲不合规   → InvalidImage
未预期标准/未知异常    → InternalFailure
```

### 14.2 VisionAlgorithm

```text
算法输入不满足前置条件 → AlgorithmFailure
OpenCV/第三方执行失败  → AlgorithmFailure
算法业务超时           → AlgorithmTimeout
未预期标准/未知异常    → InternalFailure
```

### 14.3 VisionCore

```text
状态不是 Ready         → NotReady
重复 Locate            → Busy
必需端口为空或 LocateMode 不受支持 → InvalidConfiguration
ImageFrame 或 ImageFrames 契约失败 → InvalidImage
X/Y/R 非有限           → InvalidResult
Core 未预期异常        → InternalFailure
```

Core 不把 HardwareFailure 改名为通用 LocateFailure，也不把 AlgorithmTimeout 改名为 AlgorithmFailure。

---

## 15. 测试要求

VisionCore 单元测试不得依赖 Qt、真实相机、真实光源、真实算法或外部上位机。使用 FakeCamera、FakeLight、FakeLocateAlgorithm 和 FakeDiagnosticSink。

Core 专用 Fake、Fixture、辅助头和测试文件必须放在 `VisionTest/source/VisionCore/`；只有被多个测试模块复用的断言和超时辅助才能放入 `VisionTest/include/TestSupport/`。

`ILocateFrameObserver` 必须有独立回归：未注入时行为不变；只在关灯成功且帧契约完整校验后通知一次；调用顺序为 `LightOff → FrameObserved → AlgorithmLocate`；非法模式、关灯失败、图像或集合校验失败不通知；观察器只获得同步只读帧视图，不得改变算法输入像素。`onFrames()` 的 `noexcept` 使用编译期断言验证，不编写违反类型契约的“观察器抛异常”用例。

### 15.1 正常结果

必须验证：

* Edge 和 MarkPoint 都可以返回 OK 或 NG；
* LocateOptions 中的模式被原样传给 Light 和 Camera；
* FakeLight 记录每次 `turnOn(mode)` 收到的强类型模式；
* Edge 构造 `EdgeAlgorithmInput`，MarkPoint 构造 `MarkPointAlgorithmInput`；
* 每次 Locate 只调用一次 Algorithm；
* valid=true 返回 OK；
* valid=false 返回 NG；
* valid=true 时 X、Y、R 原值传递，不执行坐标转换或角度归一化；
* valid=false 时即使 AlgorithmOutput 携带非零值或非有限值，最终 NG 的 X、Y、R 仍严格为 `0.0`；
* OK 和 NG 都执行一次关灯；
* 成功结果清除 lastError；
* NG 坐标不会被测试代码用于设备动作语义。

### 15.2 状态和准入

必须验证：

* Ready 才接受 Locate；
* Locating 防御性返回 Busy；
* Faulted、ShuttingDown、Stopped 返回 NotReady；
* Busy 不排队；
* 状态转换不依赖字符串；
* status() 返回按值快照。

### 15.3 错误传播

对每个稳定 ErrorCode 至少覆盖一条路径：

* NotReady；
* Busy；
* InvalidConfiguration；
* HardwareUnavailable；
* HardwareFailure；
* HardwareTimeout；
* InvalidImage；
* AlgorithmFailure；
* AlgorithmTimeout；
* InvalidResult；
* InternalFailure。

必须验证 Hardware 和 Algorithm Fake 返回的 `Error` 在不需要改变时被 Core 原样传播，code、message、diagnostic 不丢失。

### 15.4 图像契约

必须验证：

* Edge 只接受恰好一张 Center 图像；
* MarkPoint 只接受恰好四张图像，并包含四个唯一角相机角色；
* 空集合、数量错误、Unknown 角色、角色缺失、角色重复、模式不匹配、重复 cameraId 均返回 InvalidImage；
* ImageFrames 容器顺序变化不改变 MarkPoint 输入中的角色映射；
* 任一帧非法时不调用 Algorithm；
* 空图返回 InvalidImage；
* 非 `CV_8UC1` 返回 InvalidImage；
* 非连续 Mat 返回 InvalidImage；
* 默认构造的空 capturedAt 返回 InvalidImage；
* 空 cameraId 返回 InvalidImage；
* frameNumber 为 0 时仍可正常进入算法；
* Algorithm Fake 只能收到两类输入中的 `const cv::Mat` 引用，不能访问 capturedAt、frameNumber、cameraId 或 cameraRole；
* 合法图像调用算法时各自使用原像素缓冲，不发生 Core 深拷贝；
* 算法调用前后所有输入图像的尺寸、类型和逐字节像素完全一致；
* FakeLocateAlgorithm 不保存输入引用；
* FakeCamera 使用可变的模拟采集源构造独立持有像素的 ImageFrames；在 capture 返回后、Algorithm 读取期间复用或修改模拟源缓冲，算法输入图像仍必须保持有效且逐字节不变；
* 上述用例只验证 Core 链路依赖的独立缓冲前置契约和同步只读传递，不得声称 VisionCore 能识别真实厂商临时缓冲。

### 15.5 多帧诊断

必须验证：

* Edge 集合级事件可以携带 Center 帧上下文；
* MarkPoint 集合级或算法级事件可以同时携带四帧上下文；
* 单帧校验错误至少携带发生错误的帧上下文；
* 帧上下文包含 capturedAt、frameNumber、cameraId 和 cameraRole；
* 非帧事件的 frames 为空；
* FakeDiagnosticSink 可以模拟内部写入失败，但 `submit()` 必须遵守 `noexcept`、吞掉自身失败并正常返回；该失败不得改变 Locate 结果、状态或端口调用。

### 15.6 浮点结果

当 `valid == true` 时，必须分别验证 X、Y、R 为 NaN、正无穷和负无穷会返回 InvalidResult，且不得形成 OK 或 NG。当 `valid == false` 时，这些算法坐标被忽略，Core 必须返回坐标全为 `0.0` 的 NG。

### 15.7 清理优先级

必须覆盖：

* 开灯失败后仍尝试关灯；
* 采图成功、失败或超时返回后都只调用一次关灯；
* 成功路径调用顺序为开灯、采图、关灯、图像处理和算法；
* 算法入口执行时灯光已经关闭；
* 图像校验、算法失败、算法超时、NG 和非法坐标均不得触发第二次关灯；
* 采图成功、关灯失败时不调用算法并返回关灯错误；
* 开灯或采图失败、关灯也失败时返回关灯错误并诊断原错误；
* 关灯失败进入 Faulted。

### 15.8 恢复和关闭

必须验证：

* Faulted 恢复成功进入 Ready；
* 恢复失败保持 Faulted；
* Ready 状态 recover 幂等成功；
* shutdown 依序尝试关灯、关闭相机和关闭光源；
* 某一步关闭失败不阻止后续步骤；
* shutdown 最终进入 Stopped；
* 重复 shutdown 幂等；
* 不使用强制线程终止或长时间 sleep。

### 15.9 算法超时边界

Core 测试只验证：

* Edge 和 MarkPoint 的 AlgorithmTimeout 都被原样传播；
* FakeCamera 和 FakeLight 可以在同步调用中执行不依赖墙钟时间的确定性工作 hook；这些前置工作不改变 Algorithm 返回值，也不产生 Core 自建超时；
* Core 不建立整体 deadline；
* Core 不在后台线程执行算法。

Core 单元测试不得使用 `sleep_for`、墙钟耗时阈值或后台线程模拟算法和硬件超时；这里只验证同步调用顺序、稳定 Error 传播以及 Core 不建立第二层计时。

---

## 16. 性能与有限返回

VisionCore 自身流程只执行状态判断、端口调用、ImageFrames 逐帧与模式一致性校验、算法输入引用组装、三个浮点有限性校验和结果组装，不对生产图像进行不必要复制。

完整流程有限返回依赖：

* ILight 的开关、恢复和 shutdown 有限返回；
* ICamera 的 capture、recover 和 shutdown 有限返回；
* ILocateAlgorithm 根据自身配置在主要阶段边界协作检查并自然返回；
* 诊断 sink 不阻塞或具有可接受的有限耗时。

VisionCore 不承诺整体业务超时或精确最大返回时间，也不要求根据算法内部阶段推导理论最坏耗时。

---

## 17. 禁止事项

未经明确批准不得：

* 在 Core 中加入 Qt、JSON、spdlog、厂商 SDK 或 OpenCV 图像处理模块；
* 在线程、Future、协程或异步回调中执行 Locate；
* 增加外部取消接口；
* 把算法超时放进 Core 配置或 Locate 参数；
* 在 Core 中解释、转换或归一化 X、Y、R；
* 使用异常、整数返回码或字符串替代统一 Error/Outcome；
* 对正常图像执行无依据的 clone；
* 修改算法输入图像；
* 返回引用外部临时像素缓冲的 ImageFrame；
* 通过全局单例查找端口、配置或日志对象；
* 把 OK、NG 与 Error 混为同一布尔状态；
* 因诊断写入失败改变正常 Locate 结果。

---

## 18. 标定维护采集

`captureForCalibration(LocateMode)` 是 VisionApp 标定维护使用的窄同步入口。Edge 必须返回一帧 Center，MarkPoint 必须返回 TopLeft、TopRight、BottomRight、BottomLeft 各一帧。入口执行与 Locate 相同的 Ready/Busy 校验、模式照明、采集、始终关灯、逐帧和集合完整性校验，并保持清理错误覆盖主错误及 Faulted 语义；不得调用 `ILocateAlgorithm`、不得形成定位结果，也不得创建新线程或标定超时。

理论点解析、P1/P2 对应、圆阵检测、矩阵求解、质量评估、文件提交和运行时重载不进入 VisionCore。

## 19. 当前已知限制

* 强类型算法输入中的 `const cv::Mat` 引用表达逻辑只读，不能从 C++ 类型系统绝对阻止恶意 `const_cast` 或数据指针修改；第一阶段通过约束和测试保证；
* 第一阶段只校验 ImageFrames 的数量、角色、标识、逐帧元数据和像素契约；四角相机更严格的曝光时间差阈值必须在硬件同步语义明确后作为受控配置补充；
* X、Y、R 不携带坐标元数据，替换或重新配置算法时必须由 VisionAlgorithm 和上位机集成测试保证有效输出约定兼容；NG 坐标由 Core 固定为零；
* 第一阶段不公开算法分数、残差和特征数量；如未来成为稳定业务字段，需按公共接口变更处理；
* 完整 Locate 没有统一业务超时，关闭依赖算法协作式检查后自然返回，以及硬件端口使用有限等待。
