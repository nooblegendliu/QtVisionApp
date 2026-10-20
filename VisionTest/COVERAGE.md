# 当前功能覆盖矩阵

本表以最终可执行文件的 Default 全集与独立 Ui XML 为依据。每行链接到用例源码，完整用例结果与参数值见 XML；未执行项不计入通过数。

| 默认模块 | 实际执行 | 失败 |
| --- | ---: | ---: |
| Core | 105 | 0 |
| Algorithm | 134 | 0 |
| Hardware | 110 | 0 |
| Tcp | 15 | 0 |
| App | 54 | 0 |

Default 合计 418；Ui 独立执行 15，不与环境组混算。

| 功能域 / 文件 | 当前契约 | 用例标识 | 实际结果 |
| --- | --- | --- | --- |
| [VisionAlgorithm/Calibration/ComputeChainBehaviorTests](source/VisionAlgorithm/Calibration/ComputeChain/tests/ComputeChainBehaviorTests.cpp) | n=3..15、五角色、八方向；detect/solve/evaluate及计算链 | `Algorithm_CalibrationEndToEndTests.*` | 已通过 7 |
| [VisionAlgorithm/Calibration/CreationBehaviorTests](source/VisionAlgorithm/Calibration/Creation/tests/CreationBehaviorTests.cpp) | n=3..15、五角色、八方向；detect/solve/evaluate及计算链 | `Algorithm_CalibrationFrameworkTests.*` | 已通过 13 |
| [VisionAlgorithm/Calibration/DetectionBehaviorTests](source/VisionAlgorithm/Calibration/Detection/tests/DetectionBehaviorTests.cpp) | n=3..15、五角色、八方向；detect/solve/evaluate及计算链 | `Algorithm_CalibrationDetectionTests.*` | 已通过 16 |
| [VisionAlgorithm/Calibration/EvaluationBehaviorTests](source/VisionAlgorithm/Calibration/Evaluation/tests/EvaluationBehaviorTests.cpp) | n=3..15、五角色、八方向；detect/solve/evaluate及计算链 | `Algorithm_CalibrationEvaluationTests.*` | 已通过 8 |
| [VisionAlgorithm/Calibration/SolveBehaviorTests](source/VisionAlgorithm/Calibration/Solve/tests/SolveBehaviorTests.cpp) | n=3..15、五角色、八方向；detect/solve/evaluate及计算链 | `Algorithm_CalibrationSolveTests.*` | 已通过 7 |
| [VisionAlgorithm/Creation/DemoEdgeVisualizationStoreTests](source/VisionAlgorithm/Creation/tests/DemoEdgeVisualizationStoreTests.cpp) | 公开创建与资源校验、禁用模式、无部分对象、异常边界 | `Algorithm_DemoEdgeVisualizationStoreTests.*` | 已通过 3 |
| [VisionAlgorithm/Creation/VisionAlgorithmCreationTests](source/VisionAlgorithm/Creation/tests/VisionAlgorithmCreationTests.cpp) | 公开创建与资源校验、禁用模式、无部分对象、异常边界 | `Algorithm_VisionAlgorithmCreationTests.*` | 已通过 13 |
| [VisionAlgorithm/Creation/VisionAlgorithmPublicApiTests](source/VisionAlgorithm/Creation/tests/VisionAlgorithmPublicApiTests.cpp) | 公开创建与资源校验、禁用模式、无部分对象、异常边界 | `Algorithm_VisionAlgorithmPublicApiTests.*` | 已通过 6 |
| [VisionAlgorithm/Edge/EdgeGeometryTests](source/VisionAlgorithm/Edge/tests/EdgeGeometryTests.cpp) | 合成灰度图、几何/质量、物理转换、只读及协作deadline | `Algorithm_EdgeGeometryRotatedRectangleTests.*`<br>`Algorithm_EdgeGeometryTests.*` | 已通过 5 |
| [VisionAlgorithm/Edge/EdgePipelineTests](source/VisionAlgorithm/Edge/tests/EdgePipelineTests.cpp) | 合成灰度图、几何/质量、物理转换、只读及协作deadline | `Algorithm_EdgePipelinePhysicalPoseTests.*`<br>`Algorithm_EdgePipelineTests.*` | 已通过 6 |
| [VisionAlgorithm/Edge/EdgeQualityTests](source/VisionAlgorithm/Edge/tests/EdgeQualityTests.cpp) | 合成灰度图、几何/质量、物理转换、只读及协作deadline | `Algorithm_EdgeAbsoluteAngleTests.*`<br>`Algorithm_EdgeMeasurementRangeTests.*`<br>`Algorithm_EdgeQualityConfigTests.*`<br>`Algorithm_EdgeQualityTests.*` | 已通过 21 |
| [VisionAlgorithm/MarkGeometry/MarkGeometryTests](source/VisionAlgorithm/MarkGeometry/tests/MarkGeometryTests.cpp) | 四点几何、质量闭区间和资源结构；不包含图像匹配 | `Algorithm_MarkGeometryRotatedRectangleTests.*`<br>`Algorithm_MarkGeometryTests.*` | 已通过 4 |
| [VisionAlgorithm/MarkGeometry/MarkQualityTests](source/VisionAlgorithm/MarkGeometry/tests/MarkQualityTests.cpp) | 四点几何、质量闭区间和资源结构；不包含图像匹配 | `Algorithm_MarkQualityConfigTests.*`<br>`Algorithm_MarkQualityRangeTests.*`<br>`Algorithm_MarkQualityTests.*` | 已通过 20 |
| [VisionAlgorithm/MarkGeometry/MarkResourceValidationTests](source/VisionAlgorithm/MarkGeometry/tests/MarkResourceValidationTests.cpp) | 四点几何、质量闭区间和资源结构；不包含图像匹配 | `Algorithm_MarkResourceValidationTests.*` | 已通过 5 |
| [VisionApp/Calibration/CalibrationServicesTests](source/VisionApp/Calibration/tests/CalibrationServicesTests.cpp) | 公开维护流程、点文件、方向、矩阵文件事务与回滚 | `App_CalibrationFileTransactionTests.*`<br>`App_CalibrationOrientationTests.*`<br>`App_CalibrationPointFileTests.*` | 已通过 8 |
| [VisionApp/Calibration/CalibrationWorkflowTests](source/VisionApp/Calibration/tests/CalibrationWorkflowTests.cpp) | 公开维护流程、点文件、方向、矩阵文件事务与回滚 | `App_CalibrationWorkflow.*` | 已通过 4 |
| [VisionApp/Configuration/RuntimeBehaviorTests](source/VisionApp/Configuration/Runtime/tests/RuntimeBehaviorTests.cpp) | 配置解析/保存/校验；运行时重建与回滚；revision | `App_ApplicationControllerConfigurationTests.*` | 已通过 10 |
| [VisionApp/Configuration/AppConfigTests](source/VisionApp/Configuration/tests/AppConfigTests.cpp) | 配置解析/保存/校验；运行时重建与回滚；revision | `App_AppConfigTests.*` | 已通过 10 |
| [VisionApp/Coordination/LifecycleBehaviorTests](source/VisionApp/Coordination/Lifecycle/tests/LifecycleBehaviorTests.cpp) | 生命周期、任务/ACK/会话、恢复、模拟后端与像素快照 | `App_ApplicationControllerLifecycleTests.*` | 已通过 6 |
| [VisionApp/Coordination/RecoveryBehaviorTests](source/VisionApp/Coordination/Recovery/tests/RecoveryBehaviorTests.cpp) | 生命周期、任务/ACK/会话、恢复、模拟后端与像素快照 | `App_ApplicationControllerRecoveryTests.*` | 已通过 3 |
| [VisionApp/Coordination/TaskDeliveryBehaviorTests](source/VisionApp/Coordination/TaskDelivery/tests/TaskDeliveryBehaviorTests.cpp) | 生命周期、任务/ACK/会话、恢复、模拟后端与像素快照 | `App_ApplicationControllerTaskDeliveryTests.*` | 已通过 5 |
| [VisionApp/Coordination/SimulationAndSnapshotsTests](source/VisionApp/Coordination/tests/SimulationAndSnapshotsTests.cpp) | 生命周期、任务/ACK/会话、恢复、模拟后端与像素快照 | `App_Simulation.*`<br>`App_WorkerSnapshots.*` | 已通过 2 |
| [VisionApp/Presentation/DiagnosticBridgeTests](source/VisionApp/Presentation/tests/DiagnosticBridgeTests.cpp) | 协议结果和诊断映射；日志过滤/轮转/失败 | `App_DiagnosticBridgeTests.*` | 已通过 1 |
| [VisionApp/Presentation/LoggingTests](source/VisionApp/Presentation/tests/LoggingTests.cpp) | 协议结果和诊断映射；日志过滤/轮转/失败 | `App_Logging.*` | 已通过 2 |
| [VisionApp/Presentation/ProtocolMapperTests](source/VisionApp/Presentation/tests/ProtocolMapperTests.cpp) | 协议结果和诊断映射；日志过滤/轮转/失败 | `App_ProtocolMapperTests.*` | 已通过 3 |
| [VisionApp/Ui/CalibrationCompletionTests](source/VisionApp/Ui/tests/CalibrationCompletionTests.cpp) | 独立真实Widgets交互；中心/四角标定、配置、窗口和退出 | `DISABLED_App_Ui_CalibrationCompletion.*` | 已通过 2 |
| [VisionApp/Ui/CalibrationUiInteractiveTests](source/VisionApp/Ui/tests/CalibrationUiInteractiveTests.cpp) | 独立真实Widgets交互；中心/四角标定、配置、窗口和退出 | `DISABLED_App_ManualUi_CalibrationUiInteractiveTests.*` | 未执行：人工观察 |
| [VisionApp/Ui/CalibrationUiSelectionTests](source/VisionApp/Ui/tests/CalibrationUiSelectionTests.cpp) | 独立真实Widgets交互；中心/四角标定、配置、窗口和退出 | `DISABLED_App_Ui_CalibrationUiSelectionTests.*` | 已通过 11 |
| [VisionApp/Ui/ConfigurationAndWindowsTests](source/VisionApp/Ui/tests/ConfigurationAndWindowsTests.cpp) | 独立真实Widgets交互；中心/四角标定、配置、窗口和退出 | `DISABLED_App_Ui_Configuration.*`<br>`DISABLED_App_Ui_Windows.*` | 已通过 2 |
| [VisionCore/Diagnostics/VisionCoreDiagnosticsTests](source/VisionCore/Diagnostics/tests/VisionCoreDiagnosticsTests.cpp) | 诊断只读副本、失败不干预业务；帧观察及调用期所有权 | `Core_VisionCoreDiagnosticsFixture.*`<br>`Core_VisionCoreDiagnosticsTests.*` | 已通过 10 |
| [VisionCore/Diagnostics/VisionCoreFrameObserverTests](source/VisionCore/Diagnostics/tests/VisionCoreFrameObserverTests.cpp) | 诊断只读副本、失败不干预业务；帧观察及调用期所有权 | `Core_VisionCoreFrameObserverTests.*` | 已通过 7 |
| [VisionCore/ImageContract/VisionCoreImageContractTests](source/VisionCore/ImageContract/tests/VisionCoreImageContractTests.cpp) | 格式、连续性、角色、数量、元数据；非法输入零算法访问 | `Core_VisionCoreImageContractTests.*` | 已通过 19 |
| [VisionCore/Lifecycle/VisionCoreLifecycleTests](source/VisionCore/Lifecycle/tests/VisionCoreLifecycleTests.cpp) | 准入、重入、状态和lastError；恢复/关闭优先级；幂等及析构 | `Core_VisionCoreLifecycleFixture.*`<br>`Core_VisionCoreLifecycleTests.*` | 已通过 8 |
| [VisionCore/Lifecycle/VisionCoreStateTests](source/VisionCore/Lifecycle/tests/VisionCoreStateTests.cpp) | 准入、重入、状态和lastError；恢复/关闭优先级；幂等及析构 | `Core_VisionCoreStateFixture.*`<br>`Core_VisionCoreStateTests.*` | 已通过 12 |
| [VisionCore/Locate/CalibrationCaptureTests](source/VisionCore/Locate/tests/CalibrationCaptureTests.cpp) | 两模式与标定采集；正常/NG/全部稳定错误；先关灯后处理；异常兜底 | `Core_CalibrationCapture.*` | 已通过 4 |
| [VisionCore/Locate/VisionCoreErrorTests](source/VisionCore/Locate/tests/VisionCoreErrorTests.cpp) | 两模式与标定采集；正常/NG/全部稳定错误；先关灯后处理；异常兜底 | `Core_VisionCoreErrorTests.*` | 已通过 7 |
| [VisionCore/Locate/VisionCoreExceptionTests](source/VisionCore/Locate/tests/VisionCoreExceptionTests.cpp) | 两模式与标定采集；正常/NG/全部稳定错误；先关灯后处理；异常兜底 | `Core_VisionCoreExceptionTests.*` | 已通过 16 |
| [VisionCore/Locate/VisionCoreLocateTests](source/VisionCore/Locate/tests/VisionCoreLocateTests.cpp) | 两模式与标定采集；正常/NG/全部稳定错误；先关灯后处理；异常兜底 | `Core_VisionCoreLocateTests.*`<br>`Core_VisionCoreNonFiniteResultTests.*` | 已通过 22 |
| [VisionHardware/Configuration/HikCameraSystemTests](source/VisionHardware/Configuration/tests/HikCameraSystemTests.cpp) | SDK前配置规则、五角色、16种照明组合、无观察器拒绝 | `Hardware_CameraCombination.*`<br>`Hardware_CameraRules.*` | 已通过 108 |
| [VisionHardware/Configuration/HikLightControllerConfigTests](source/VisionHardware/Configuration/tests/HikLightControllerConfigTests.cpp) | SDK前配置规则、五角色、16种照明组合、无观察器拒绝 | `Hardware_LightRules.*` | 已通过 2 |
| [VisionHardware/Environment/HikMvsEnvironmentSmokeTests](source/VisionHardware/Environment/tests/HikMvsEnvironmentSmokeTests.cpp) | SDK初始化/枚举/错误/释放；与设备链路验收分离 | `DISABLED_Hardware_Mvs_HikMvsEnvironmentSmokeTests.*` | 环境测试：未执行 |
| [VisionTcp/Protocol/VisionTcpCodecTests](source/VisionTcp/Protocol/tests/VisionTcpCodecTests.cpp) | 现有报文与字段边界、UTF8/JSON、半包粘包和完整性 | `Tcp_VisionTcpCodecTests.*` | 已通过 5 |
| [VisionTcp/Replies/VisionTcpRepliesTests](source/VisionTcp/Replies/tests/VisionTcpRepliesTests.cpp) | 全部稳定回复、旧连接发送隔离与枚举映射 | `Tcp_VisionTcpProtocolNameTests.*`<br>`Tcp_VisionTcpRepliesTests.*` | 已通过 4 |
| [VisionTcp/Session/VisionTcpSessionTests](source/VisionTcp/Session/tests/VisionTcpSessionTests.cpp) | 监听、单客户端、代次、断线重连和心跳 | `Tcp_VisionTcpSessionTests.*` | 已通过 6 |

## 覆盖边界

- 模板匹配成功与完整四图 MarkPoint 图像定位：暂缓。
- MVS 环境：保留独立禁用组，未执行；真机采图、节点、SDK关闭有限性：未验证。
- Debug、原生文件对话框、长期压力和生产精度：未验收。
- 无公开确定性触发入口的任意迟到 Worker completion、系统级故障注入等缺口见 [DEFECTS.md](DEFECTS.md)。

## 迁移核对

Controller 原聚合文件按生命周期/任务/恢复/配置拆分；原 CalibrationFramework 文件按创建/检测/求解/评估/计算链拆分。已按现行契约核对的边界断言和合成数学基准保留在相应功能域，未通过更名修改其预期。

Hardware 摄像配置重写为逐槽单规则参数矩阵，光源覆盖全部16种开关组合；Core 原三项标定采集测试由新的双模式/清理恢复/非法输入/重入矩阵替代；私有槽维护锁用例由真实公开标定会话替代。测试资源及Fake等待逻辑改为唯一目录与有界同步。

新增 App 完整标定会话和候选失败回滚、模拟后端、实际cv::Mat源像素覆写后的显示快照、日志轮转，以及中心/四角完整UI、配置确认和窗口切换。旧报告不作为本表依据。
