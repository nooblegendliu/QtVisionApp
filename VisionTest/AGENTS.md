# VisionTest 当前功能测试约束

## 1. 本轮授权和边界

本文件按开发者批准的“VisionTest 按当前软件功能重写方案”替换旧测试实施说明，服从根目录与生产模块契约。已授权本轮 Release|x64 构建、分模块默认测试、默认全集及独立自动 UI 验收；不运行 Debug、MVS 环境组或人工 UI 组。后续未明确授权的任务仍不得自行执行构建、测试或测试发现。

保留唯一 VisionTest.vcxproj、现有 GoogleTest 入口和依赖版本，不新增生产接口、不修改生产实现。发现生产缺陷必须保留有效失败用例，记录期望、实际、复现入口和影响，不降低断言、伪造通过或禁用失败用例。

默认测试不进入海康 SDK。仅配置校验、缺失硬触发观察器在 SDK 前拒绝和编译期所有权约束属于 Hardware 默认组。MVS 环境冒烟独立禁用，只验证初始化、枚举、错误和释放；真实设备组当前不存在。

模板匹配成功路径、旧 case0/case1、方形 ShapeMatcher 和完整四图 MarkPoint 图像定位继续暂缓，不新增模型或黄金图作为通过依据。四角标定不属于该暂缓范围。

## 2. 目录和断言

include/TestSupport 只存放被多个模块实际复用的无业务基础设施。source 第一层固定为 VisionCore、VisionAlgorithm、VisionHardware、VisionTcp、VisionApp；其下按功能域设置 tests，确需复用时设置 support。

功能域：Core 为 Locate/ImageContract/Lifecycle/Diagnostics；Algorithm 为 Creation/Edge/MarkGeometry/Calibration；Hardware 为 Configuration/Environment；Tcp 为 Protocol/Session/Replies；App 为 Configuration/Coordination/Calibration/Presentation/Ui。模块级 support 仅用于跨本模块功能域复用。

每个测试文件只负责一个主题。项目与 filters 镜像实际文件结构。测试资源只放入测试专用目录或运行时唯一临时目录，不修改生产示例、本机配置或生产图片。旧代码可作为覆盖清点来源；重写以公开行为与生产契约核对预期，不能把历史报告当验收。

只通过返回值、公开状态/信号、网络、文件及生命周期观察行为；不访问私有成员，不 invoke 私有槽注入成功状态。测试替身不得跨模块共享。Core 测试不包含 Qt、不使用文件或墙钟等待。Algorithm 内部几何和已有 ExecutionContext 可直接验证，不新增公开端口。

## 3. 生命周期与隔离

沿用 GoogleTest main，测试 Environment 在运行前创建唯一 Qt 应用。Default 使用 QCoreApplication；Ui/ManualUi 必须独立进程使用 QApplication，禁止与 Default/MVS 混跑。默认组不弹窗。

每次资源目录采用唯一名称及原子创建，不删除固定名称的已有目录。QSettings、配置、日志、标定文件均隔离。Fake 只在线程内产生独立连续 CV_8UC1，跨线程仅发送独立快照。

普通事件等待、Fake 条件等待和退出等待默认 5 秒。失败清理先放行测试门闩，再请求 shutdown，保持主线程事件循环直到 shutdownFinished。不得 QThread::terminate，不得销毁正在运行的 QThread；不可恢复的测试清理超时必须终止独立测试进程并报告基础设施失败。

## 4. 活动功能矩阵

| 模块 | 默认活动范围 | 独立/暂缓范围 |
| --- | --- | --- |
| Core | 两模式及标定采集、帧集合/格式/元数据、OK/NG、全部 ErrorCode、状态、异常、清理恢复关闭、诊断与观察器 | 无真机 |
| Algorithm | 创建与启用开关、资源验证、Edge 图像与几何质量、Mark 几何质量、标定 detect/solve/evaluate/计算链、只读与错误、已有 deadline | 模板与完整 Mark 图像暂缓 |
| Hardware | 纯配置、观察器 SDK 前拒绝、句柄不可复制移动 | MVS 显式环境组；真实设备未覆盖 |
| Tcp | 命令回复、字段矩阵、拆粘包、编码长度、连接代次、单客户端、心跳 | 无外部上位机 |
| App | 配置/日志/诊断、任务/会话/ACK、初始化恢复重配置关闭、标定维护与事务、快照、模拟运行时 | 独立关键 UI 组 |

数值基准由独立构造产生。Edge 合成图中心最大 3 px、角度 0.5 度；标定圆心 1 px；理想标定映射 1e-6 mm；Mark 纯几何 1e-9。更严格且有依据的既有数值断言可以保留，不得放宽来获得通过。这些不是生产精度验收。

标定算法覆盖 n=3..15、五角色、八方向、仿射/透视、RANSAC、全部点残差、质量门槛包含边界与连续调用隔离。质量超限正常 passed=false，结构错误才是 Error。App 覆盖理论点文件、人工方向、维护锁、采集识别、求解评估、文件事务与运行时重载、重试/重置和失败恢复。

## 5. UI

按已批准范围测试中心和四角 5x5 完整标定。使用真实 MainWindow、CalibrationWorkspace、Controller、Worker、Core 和标定算法，仅替换硬件和生产 Locate 端口。真实鼠标键盘事件验证 P1/P2、角色切换、门禁、求解评估应用重置、无效选点与缺点重试。其他规格由算法/后端覆盖。

配置编辑/撤销/应用、详细监控切换、未保存配置退出确认也属于独立 Ui 组；窗口切换不得改变任务准入或触发 Locate。文件对话框可通过公开入口预置路径，不注入后端结果，不做像素外观回归。运行前隔离 QSettings；截图与事件只进入本次输出目录。

## 6. 运行与交付

run_tests.ps1 参数 Module=All/Core/Algorithm/Hardware/Tcp/App，Group=Default/Ui/MvsEnvironment/ManualUi，默认 All+Default。Ui/ManualUi 仅 App，MvsEnvironment 仅 Hardware。脚本不构建，非法组合、零执行用例、启动失败、测试失败或600秒进程超时都返回非零。

每次独立输出 XML、stdout/stderr 和模块汇总。环境/手工组不进入默认报告。功能矩阵区分已通过、失败、未执行、暂缓、环境测试；无法确定性触发的路径记录缺口，不制造占位通过测试。

交付包含 README、COVERAGE、DEFECTS、实际验收报告；核验生产代码校验和不变。更新或删除测试必须同步维护工程及 filters，旧报告不作为新版本证据。
