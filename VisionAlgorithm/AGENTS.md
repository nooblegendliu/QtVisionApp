# VisionAlgorithm 项目设计

## 0. 文档定位

本文定义 VisionAlgorithm 的抓边、四角抓点、标定、位姿融合、valid、超时和只读输入规则。本文服从根目录和 `VisionCore/AGENTS.md`，不得重新定义 LocateMode、AlgorithmInput、AlgorithmOutput、LocateResult 或统一 Error。

VisionAlgorithm 是 x64、C++20、UTF-8 的静态库，实现 VisionCore 定义的 `ILocateAlgorithm`。

VisionAlgorithm 通过公开同步创建入口接收算法主配置文件路径，并返回 `Outcome<std::unique_ptr<ILocateAlgorithm>>`。创建成功值必须是已经完成全部配置和资源初始化的算法端口；创建失败必须返回统一 `Error`，不得返回部分初始化对象，也不得让内部异常跨越公开创建边界。具体构造函数属于私有实现。

VisionApp 可以在 UI 中选择新的算法主配置文件并请求校验。配置只允许在任务空闲时通过销毁旧算法并重新创建完整视觉运行时生效；VisionAlgorithm 不提供 Locate 期间热写参数的接口，内部配置类型仍不暴露给 UI。

---

## 1. 统一算法端口

算法端口保持一个同步入口：

```cpp
AlgorithmOutcome locate(const AlgorithmInput& input) override;
```

AlgorithmInput 固定包含两种强类型输入：

* `EdgeAlgorithmInput`：一张 Center 图像；
* `MarkPointAlgorithmInput`：按 TopLeft、TopRight、BottomRight、BottomLeft 命名的四张图像。

输入 variant 的具体类型决定算法流程，不再额外接收 LocateMode。输入只包含 `const cv::Mat` 引用，不包含 capturedAt、frameNumber、cameraId、cameraRole、JSON 或硬件配置。

---

## 2. Edge 抓边流程

Edge 模式使用中心相机图像：

```text
校验算法输入
→ 图像预处理
→ 识别硅片四条边
→ 计算图像位姿和所需几何信息
→ Algorithm 内部处理几何合法性
→ 使用中心相机标定转换
→ 形成最终 X/Y/R/valid
```

四边表达、几何字段、阈值和中间位姿属于 VisionAlgorithm 内部类型，不进入 VisionCore 公共接口。

---

## 3. MarkPoint 四角抓点流程

MarkPoint 模式使用四角相机图像：

```text
校验四个命名输入
→ 分别识别四个图像坐标系中的 Mark 点
→ 使用各自相机标定转换
→ 使用四个物理坐标构造四边、两条对角线和水平中线
→ 使用水平中线中点形成中心并由中线方向形成角度
→ 校验四边、对角线、中心 X/Y 和绝对角度范围
→ 形成整片硅片最终 X/Y/R/valid
```

规则：

* 四张图像的角色由输入字段确定，不依赖容器顺序或 cameraId 字符串；
* Mark 点像素坐标、匹配分数、各相机中间位姿和融合残差不进入 Core；
* 当前生产 Mark 模板限定为完整、居中、对称正方形；单个 Mark 的像素定位点固定定义为当前匹配模板一级 LINE2 特征包围盒的中心；
* 粗匹配和 ICP 精定位必须使用同一个一级特征包围盒中心作为位置参考点；ICP 开启时对该参考点应用 ICP 变换，关闭时直接使用粗匹配得到的参考点；
* 特征包围盒中心允许为整数或半整数，不得通过模板或图像尺寸的奇偶性强制取整，也不得硬编码 `0.5 px` 补偿；
* 若后续支持非对称 Mark 模板，必须重新定义明确的业务参考点及其与 ICP 模型坐标之间的转换，不得继续默认使用特征包围盒中心；
* 每台角相机使用与其稳定角色匹配的标定数据；
* 多相机标定选择和最终位姿融合完全属于 VisionAlgorithm；
* Core 不调用四次独立算法，也不参与融合。
* 水平中线连接左侧两个 Mark 的中点与右侧两个 Mark 的中点，中心取该中线中点，角度归一化到 `[-90°, 90°)`；
* 四边、两条对角线、中心和角度均只作为单次同步调用中的局部中间值，不进入公共输出、类成员或演示快照；
* 任一几何或位姿范围不合格属于正常算法 NG，统一返回 `0.0/0.0/0.0/valid=false`；配置错误、算法异常和超时仍返回 Error。

---

## 4. AlgorithmOutput 与 valid

两种模式统一返回：

```cpp
struct AlgorithmOutput {
    double x;
    double y;
    double r;
    bool valid;
};
```

语义：

* X、Y、R 已完成本模式全部图像处理、标定、坐标转换和必要融合；
* `valid` 只表示最终算法输出是否合法；
* 边、几何、Mark 点和融合过程中的无效情况由 Algorithm 内部处理；
* `valid == true` 时 X、Y、R 必须为可交付的最终 double 数据；
* `valid == false` 时 Core 会忽略 X、Y、R 并形成零坐标 NG；
* 算法异常、算法超时、配置缺失或第三方调用失败必须返回 Error，不得伪装成 `valid == false`；
* 坐标系、单位、原点、方向和角度表示由 Algorithm 配置及实现保证，Core 不参与转换。

---

## 5. 标定配置

Algorithm 配置至少能够表达：

* Center 抓边标定；
* TopLeft、TopRight、BottomRight、BottomLeft 四套角相机标定；
* MarkPoint 多相机位姿融合所需的稳定参数；
* Edge 几何处理和 MarkPoint 有效性处理所需参数；
* 有单位的算法执行超时。

主配置必须使用独立的 `calibration` 节点固定提供校正算法参数文件路径，以及 Center、TopLeft、TopRight、BottomRight、BottomLeft 五个角色的标定文件路径。`edge` 和 `mark` 节点只保存各自算法参数文件路径，`enabled_algorithms` 只控制生产定位流程是否创建，不控制标定路径是否解析。配置不兼容缺少 `calibration.parameters` 的旧格式，也不兼容旧的 `edge.calibration` 和 `mark.*_calibration` 字段。

主配置解析时必须要求参数路径和五个角色路径字段完整、非空并相对于主配置文件解析。生产 `VisionAlgorithm::create()` 只记录校正参数路径且不打开该文件，并只加载当前启用模式使用的标定文件：Edge 使用 Center，Mark 使用四角；校正参数文件或禁用模式对应的标定文件暂时不存在不得阻止生产运行时创建。`CalibrationAlgorithm::create()` 必须独立加载校正参数和全部五份标定资源，任一资源无效时不得返回部分对象。

校正图案为可配置的 `n×n` 规则对称圆阵列，五个相机角色共用规格。校正参数 YAML 顶层必须显式提供整数 `grid_side_points`，合法范围为 `[3,15]`，包含奇数和偶数；内部默认值和示例为 `5`，但缺失字段不得自动补值。检测要求完整 `N=n*n` 点；后续求解使用 N 组对应点进行 RANSAC，最多剔除一点。必填 `quality.minimum_used_points` 仅允许 `N-1` 或 `N`，内部默认值和示例为 `24`。切换规格必须同步修改该门槛，例如 3×3 为 8/9、5×5 为 24/25、7×7 为 48/49；缺失、类型错误、越界或规格与门槛不匹配时，校正公开创建入口返回 `InvalidConfiguration`。旧配置需人工补充字段，不自动迁移用户文件。五个相机角色分别配置 Blob 颜色、阈值范围和步长、最小重复次数、圆心最小距离、面积范围及最小圆度；RANSAC 阈值、最少采用点数、RMS 上限和单点最大残差由五个角色共用。Blob 固定开启颜色、面积和圆度过滤，凸度和惯性过滤暂不开放配置。

阵列规格只表示点数规模，不增加矩形阵列、物理点距或图像缩放配置；变换矩阵仍固定为 `3×3`。公开函数签名和结果结构保持不变，不提供公共配置类型或尺寸查询接口。理论点从物理左下角 P1 开始，每行沿 +X、各行沿 +Y 编号至 PN，八种人工方向枚举保留，规则不依赖中心圆。质量统计覆盖全部 N 点，包括未参与拟合的点；标定算法不修改 UI。

`image_to_physical` 的稳定语义为“畸变校正后的完整图像像素坐标到设备加工平面 XY 毫米坐标”。Edge 在完整校正图像中使用该矩阵；Mark 在校正 ROI 中检测后必须加回 ROI 偏移形成完整校正图像坐标再使用该矩阵。校正理论点、点残差、RANSAC 阈值、RMS 和最大残差统一使用毫米。候选标定文件必须保留图像尺寸、相机内参、畸变参数和新内参，只替换 `image_to_physical`，并在交付前通过与 `CameraGeometry` 相同的加载校验。

VisionAlgorithm 负责定义并解析自身主配置文件、算法参数、标定和模板资源，相关内部配置类型保留在实现边界，不作为公共配置接口暴露。VisionApp 只负责从应用启动配置中确定算法主配置文件路径，并在视觉线程将该路径传给公开同步创建入口。创建入口内部必须同步完成全部配置与资源校验，只有成功返回的端口才允许注入 VisionCore。JSON 类型、算法配置路径和内部配置类型不得进入算法端口或 VisionCore。

MarkPoint 的 `build` 和 `match` 参数不属于生产配置文件，统一使用 VisionAlgorithm 代码中定义的固定值。固定 build 参数为角度范围 `[0.0, 360.0]`、角度步长 `1.0`、尺度范围 `[1.0, 1.0]`、尺度步长 `1.0` 和模板补边 `100 px`；固定 match 参数为相似度阈值 `90.0`、NMS 阈值 `0.3`、最大候选数 `1`、对齐步长 `16`、源图补边 `100 px` 且关闭 ICP。`loadMarkAlgorithmConfig()` 不得从文件读取或覆盖这些值，MarkPoint 参数文件不得包含 `build` 或 `match` 节点。生产模板必须使用当前固定 build 参数生成；固定 build 参数变化时必须重新生成并验证模型。内部第三方兼容差分测试允许直接构造不同的 build 或 match 参数，但该能力不构成生产配置契约。

MarkPoint 的模型路径、模板原图尺寸、Detector 参数、输入图像尺寸、相机 ROI 和质量范围继续由配置文件提供。Detector 参数必须与模板模型保持一致，不得因 build 和 match 固定化而移出配置。

配置主文件、算法参数、标定或模板资源缺失、不可读、格式错误或彼此不兼容时，创建入口返回 `ErrorCode::InvalidConfiguration`；创建过程中的未预期标准异常或未知异常返回 `ErrorCode::InternalFailure`。稳定 `message` 不包含第三方原始文本，文件路径、OpenCV 文本和第三方上下文只进入 `diagnostic`。私有构造函数和内部加载函数允许抛出异常，但异常必须在创建入口内完成分类和转换。

角色对应的标定缺失、重复或不兼容时必须初始化失败；不得在运行时静默使用其他相机标定替代。

MarkPoint 质量配置至少包含四条边和两条对角线的物理长度闭区间、最终中心 X/Y 闭区间和最终绝对角度闭区间。长度及绝对角度范围不得为负，中心坐标范围允许负数；所有上下限必须为有限值且满足 `lower <= upper`。第一阶段示例配置可以使用明确标注的宽松范围跑通流程，取得合格片统计数据后必须按实际坐标单位收紧。

---

### 5.1 CalibrationAlgorithm 可配置圆阵列检测

`CalibrationAlgorithm::detect(cameraRole, rawImage)` 是独立的同步标定计算入口，不属于生产 `locate()`，不设置超时，不负责采图、UI、文件写入或应用事务。当前标定计算实现检测、`solve()` 单应矩阵求解和 `evaluate()` 质量评估。

检测契约：

* 先检查内部资源，再以显式角色分支选择已加载的五套 `CameraGeometry` 和 `CircleDetectionConfig`；缺失内部资源返回 `InternalFailure`，Unknown 或非法角色返回 `InvalidConfiguration`。
* 输入必须非空、二维、连续、`CV_8UC1` 且尺寸与该角色标定一致，否则返回 `InvalidImage`；不自动转换、缩放或修复输入。
* 复用初始化生成的去畸变映射，通过 `CameraGeometry::rectify()` 生成完整校正图像，不裁剪 ROI、不使用旧 `image_to_physical`。输出为独立、连续的同尺寸灰度图，必要时 clone；输入只读且不保留调用期引用。
* 每次调用创建局部 `SimpleBlobDetector`，直接映射已有颜色、阈值、重复次数、距离、面积和最小圆度参数；颜色、面积、圆度过滤开启，凸度、惯性及轮廓收集关闭。其他参数保留 OpenCV 默认值，包括 `maxCircularity`。
* 使用已加载的 `gridSidePoints` 构造 `Size{n,n}`，只调用带网格参数的 `findCirclesGrid(..., Size{n,n}, ..., CALIB_CB_SYMMETRIC_GRID, detector, gridParameters)` 完成 Blob 提取和网格识别。每次创建局部 `CirclesGridFinderParameters`，仅 3×3 显式设置 `minDensity=6`，其他规格保持 OpenCV 默认值；该适配不进入配置文件。不预先调用 detector、不启用 clustering、不自动判断或逐规格重试、不增加应用层重试、额外预处理、补点、圆拟合或角点细化。
* `found=false` 是正常检测结果：保留校正图像，丢弃任何部分点，返回空 `imagePoints` 和 `usableForSolving=false`。
* `found=true` 必须恰好 N 点，坐标有限、在图像半开范围 `[0,width) × [0,height)` 内且互不重复，否则返回 `AlgorithmFailure`。合法点逐点转为 `Point2d`，不取整、不自行排序，保持 OpenCV 原生网格顺序，并设置 `usableForSolving=true`；物理方向映射只属于 `solve()`。
* OpenCV 异常转换为 `AlgorithmFailure`，其他未预期标准异常或未知异常转换为 `InternalFailure`。稳定 message 与 diagnostic 分离；diagnostic 包含角色、阶段、必要尺寸和原始异常文本。
* 不缓存可变检测器、图像或点集；输入释放或修改以及后续检测均不影响已返回结果。调用者自行浅拷贝结果中的 `cv::Mat` 不会自动产生另一份独立像素。

检测验收使用确定性生成的灰度图，覆盖默认 5×5 及边长 3～15 的深/浅圆阵列、平移旋转、缺点与非网格、候选不足以组成配置规格、五角色共用规格且参数/校正隔离、非法输入、只读及输出所有权、连续成功/失败和 OpenCV 原生点序。独立参考调用同样仅对 3×3 明确设置 `minDensity=6`，不复用生产参数转换。配置测试覆盖尺寸字段的缺失、类型、范围和各规格采用点门槛的合法/非法边界。合成圆心功能容差为 1 像素，非零畸变独立对照 OpenCV 去畸变结果；该功能容差不代表生产标定精度已验收。

### 5.2 CalibrationAlgorithm 单应矩阵求解

`solve(detection, physicalPoints, orientation)` 只接受完整可求解的 N 点检测结果。OpenCV 点集直接作为 `n×n` 行优先网格，不按坐标重新排序；八种 `CalibrationGridOrientation` 是唯一方向来源，负责把检测点映射为物理左下角 P1 起始、每行沿 +X、各行沿 +Y 的 P1～PN 顺序。理论点允许使用有序的实测非等距坐标，但必须数量匹配、有限、互异且非共线。

求解固定调用 `findHomography` 的 RANSAC 方法，源坐标为完整校正图像像素，目标坐标和重投影阈值为毫米，迭代次数和置信度使用 OpenCV 默认值。候选矩阵必须为有限、非奇异的 3×3 矩阵并满足与 `CameraGeometry` 相同的 `1e-12` 行列式下限。输出 `CalibrationSolution.points` 按物理编号保存全部 N 组对应点，`used` 来自 RANSAC mask；使用候选矩阵对全部 N 点投影并计算毫米欧氏残差，包括未参与拟合的点。

`solve()` 不检查 `minimum_used_points`、RMS 或最大残差；只要 RANSAC 产生结构合法的候选矩阵，即使采用点不足也返回候选结果，后续由 `evaluate()` 正常返回 `passed=false`。不可用或损坏的检测结构返回 `InvalidResult`，校正图像契约错误返回 `InvalidImage`，非法方向或理论点返回 `InvalidConfiguration`，OpenCV 求解、矩阵、mask 或投影异常返回 `AlgorithmFailure`，其他未预期异常返回 `InternalFailure`。函数使用局部数据，不修改或保存输入，不使用现有标定文件中的旧 `image_to_physical`，不负责理论点文件解析、质量判定、文件写入、UI 或超时。

### 5.3 CalibrationAlgorithm 质量评估

`evaluate(solution)` 校验单台相机候选解并统计全部 N 点的毫米质量指标。候选解必须包含有效角色、有限且满足 `1e-12` 行列式下限的非奇异 `image_to_physical`、恰好 N 个按 P1～PN 连续编号的点，以及有限、互异、非共线的图像点和物理点；图像点还必须位于对应角色的标定图像范围内。任一保存残差必须有限且非负。

评估直接信任 `solve()` 保存的 `CalibrationPoint::residual`，不重新投影，也不验证矩阵、点和残差的数值一致性。`detectedPointCount` 固定为 N，`usedPointCount` 统计 `used=true`，RMS 和最大残差覆盖全部 N 点，包括 `used=false` 的 RANSAC 排除点。RMS 必须使用不会因有限大残差平方而溢出的稳定计算。只有采用点数不少于 `minimum_used_points`、RMS 不大于 `maximum_rms_error_mm` 且最大残差不大于 `maximum_residual_mm` 时 `passed=true`，三个边界均包含等号。

质量门槛未通过是正常评估结果并返回 `passed=false`。非法候选结构返回 `InvalidResult`，质量计算或 OpenCV 异常返回 `AlgorithmFailure`，资源缺失及其他未预期异常返回 `InternalFailure`。函数只使用局部数据，不修改或保存输入，不负责重新求解、文件写入、UI 或超时。调用方若在 `solve()` 后修改矩阵或点，有责任同步维护权威残差。

### 5.4 CalibrationAlgorithm 公开计算链验收

`CalibrationAlgorithm::create() → detect() → solve() → evaluate()` 已通过公开接口闭环测试。闭环覆盖默认 5×5、3×3/8×8/15×15 代表性规格、五个相机角色、八种人工方向、RANSAC 排除一点仍通过、排除两点导致采用数不足、严重离群点导致最大残差不通过，以及阶段失败和连续调用状态隔离。合成闭环使用检测输出按人工方向和已知单应矩阵生成理论点，并用独立图像验证点检查候选矩阵映射；该设计验证三个阶段的数据契约衔接，不替代各阶段的独立精度和非法输入测试。

闭环验收不增加组合式运行接口。采图、理论点文件解析、步骤编排、候选文件写入、应用事务和 UI 状态由 VisionApp/VisionWorker 标定后端负责，`CalibrationAlgorithm` 仍只实现 `detect()`、`solve()` 和 `evaluate()` 三阶段纯计算。合成图和数值门槛通过只证明第一版算法内核闭环，不代表真实标定板、相机、光源或设备坐标精度已经验收。

---

## 6. 算法超时

算法业务计时从 `locate(input)` 开始处理对应输入起，到返回 AlgorithmOutput 或 Error 为止：

* Edge 覆盖预处理、四边识别、几何处理、标定和最终输出；
* MarkPoint 覆盖四图 Mark 识别、各相机标定、位姿融合和最终输出；
* 采图、ImageFrames 校验、光源控制和 Core 结果组装不计入算法超时；
* 算法使用 steady_clock 和自身配置的 chrono 时长；
* Edge 和 MarkPoint 各自一次 `locate(input)` 只创建一个截止时间，MarkPoint 四图识别和融合共享同一预算；
* 只在算法入口、主要阶段结束和最终结果返回前进行协作式检查，不要求把截止时间传入每个内部循环、OpenCV 调用或第三方算法内部；
* 一个同步阶段开始后自然执行到返回；如果执行期间越过截止时间，在下一个检查点返回超时，因此配置时长不是强制中断时刻或硬返回上限；
* 超时返回 AlgorithmTimeout；
* 不创建后台线程伪造超时，不依赖 Core 或 Qt timer 中断。

超时配置位置和主要检查边界必须保持清晰；当前阶段不要求记录或证明每个 OpenCV、第三方调用和内部循环的最大耗时。

---

## 7. 输入只读和错误

* 不得修改任何输入图像；
* 不得保存 const cv::Mat 引用、data 指针或共享 ROI 供调用结束后使用；
* 需要原地处理时先 clone 到 Algorithm 自有工作图像；
* OpenCV、第三方算法和标准异常必须在模块边界转换为统一 Error；
* AlgorithmFailure 表示算法执行失败；
* AlgorithmTimeout 表示算法业务超时；
* 未预期异常转换为 InternalFailure；
* Error::diagnostic 可保存模式、算法阶段和第三方上下文，但不得暴露为业务类型。

### 7.1 创建阶段内部异常约定

VisionAlgorithm 创建阶段统一使用以下内部异常约定：

* `std::invalid_argument` 只用于已经明确识别的配置或初始化资源错误，包括配置字段、参数范围、标定内容、模板模型及资源兼容性错误；
* `std::runtime_error`、`cv::Exception`、`std::bad_alloc` 及其他标准异常默认表示未预期的初始化故障，不得在公开创建入口统一转换为 `InvalidConfiguration`；
* 配置解析器可以在配置文件解析的局部边界，将明确由配置格式导致的 `cv::Exception` 转换为 `std::invalid_argument`；
* 标定和模型加载代码不得通过 `catch (const std::exception&)` 将全部异常重新包装成 `std::invalid_argument`；
* 不得解析 `exception::what()` 判断错误类别；
* 公开 `create()` 只将 `std::invalid_argument` 转换为 `InvalidConfiguration`，其他标准异常和未知异常统一转换为 `InternalFailure`；
* 上述异常只允许在 VisionAlgorithm 内部传播，不得跨越公开 `create()`；
* `locate()` 的运行期异常映射不使用本创建阶段约定；
* 后续新增配置和初始化资源校验时必须抛出 `std::invalid_argument`，不得使用通用 `std::runtime_error` 表达已经确认的配置错误。

---

## 8. 测试要求

算法测试不得依赖真实相机。生产定位与生产灰度链路测试必须使用固定或确定性生成的 `CV_8UC1` 图像；离线模板建模与原库的同运行时兼容差分测试允许使用仓库内固定的 `CV_8UC3` 图像，该例外不得改变生产定位输入契约。

本节列出 VisionAlgorithm 的长期完整验收要求，不因当前测试实施顺序而降低。当前 VisionTest 活动范围包含配置加载、资源合法性、稳定错误映射、公开 `create()`/`validateConfiguration()` 一致性、Edge-only 公开端口分派与输入只读、`DemoEdgeVisualizationStore` 深拷贝/快照/清理，以及不依赖实际模板模型的 Edge/Mark 几何和质量数据驱动测试。ShapeMatcher 实际模板匹配、case0/case1 兼容差分、方形全流程和完整四图 Mark 图像定位暂缓，必须作为明确的未覆盖项记录，不得宣称本节要求已经全部满足。

必须分别覆盖：

* 有效算法主配置能够通过公开创建入口返回非空、已初始化的算法端口；
* 主配置、算法参数、标定或模板资源缺失、格式错误或不兼容时，公开创建入口返回 InvalidConfiguration 且不抛出异常；
* Edge 四边识别、几何处理、中心标定和最终位姿；
* MarkPoint 四个角色输入、四点识别、各自标定和位姿融合；
* 两种模式 valid=true 和 valid=false；
* 两种模式 X/Y/R 确定性和精度；
* 角色对应标定不可交换；
* 输入图像调用前后逐字节一致；
* 不保存任何调用期图像引用；
* OpenCV/第三方错误转换；
* Edge 和 MarkPoint 在入口或主要阶段检查点发现截止时间已到时返回 AlgorithmTimeout；
* MarkPoint 四图识别与融合共享一个截止时间，而非每张图单独获得完整超时预算。

### 8.1 当前模板测试暂缓边界

当前阶段不新增模板图、模板模型、黄金图像或匹配结果基准，也不执行旧 case0/case1 和方形 ShapeMatcher 流程来证明生产定位正确。公开创建入口仍需验证配置和资源缺失、格式错误、尺寸不兼容及无部分初始化对象等稳定契约；这类资源校验不等同于模板匹配成功路径。

恢复模板和完整 MarkPoint 图像流程测试前，必须基于届时最新生产实现重新确定：

* 测试模型格式及其生成工具和运行时版本；
* 测试图像的来源、许可、像素格式和确定性；
* 模板业务参考点、坐标约定和角色对应关系；
* X、Y、R、匹配分数、几何残差和超时检查的明确容差；
* 黄金结果的生成、审核、版本化和失效规则。

旧 case0/case1 数据、历史模型、旧 XML 报告和历史可视化输出不得自动作为新基准。重新启用后仍必须覆盖上文规定的 Edge 与 MarkPoint 公开行为、只读输入、统一错误和共享超时预算。

---

## 9. 临时 Edge 演示可视化

为汇报闭环提供的 `DemoEdgeVisualizationStore` 是 VisionAlgorithm 具体实现的可选演示接口，不进入 VisionCore、`AlgorithmOutput` 或正式定位结果契约。

规则：

* 每次算法调用开始时清空上一次演示快照，禁止显示旧任务的几何结果；
* Edge 快照可包含独立拥有像素的校正后 `CV_8UC1` 图像、八个卡尺 ROI、四条拟合直线和四个角点；
* 快照中的全部几何坐标都属于校正后图像坐标系；
* 快照发布失败不得改变 OK、NG 或 Error；
* VisionWorker 只允许在视觉线程同步读取快照，并在跨线程投递前转换为独立 `QImage` 和按值几何数据；
* MarkPoint 不复用该临时 Edge 可视化结构；
* 该接口仅服务当前展示，正式调试图像方案确定后应单独评估去留。
