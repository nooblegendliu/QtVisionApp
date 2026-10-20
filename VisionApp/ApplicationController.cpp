#include "ApplicationController.h"

#include "ProtocolMapper.h"
#include "VisionWorker.h"
#include "Logging.h"

#include <QTimer>

#include <spdlog/logger.h>

#include <cassert>
#include <limits>
#include <utility>
#include <variant>

namespace vision::app {
namespace {

[[nodiscard]] bool sameContext(
    const TaskContext& left,
    const TaskContext& right)
{
    return left == right;
}

[[nodiscard]] bool sameRecovery(
    const RecoveryContext& left,
    const RecoveryContext& right)
{
    return left == right;
}

[[nodiscard]] bool sameTcp(
    const vision::tcp::TcpServerConfig& left,
    const vision::tcp::TcpServerConfig& right)
{
    return left.listenAddress == right.listenAddress &&
        left.port == right.port &&
        left.heartbeatTimeout == right.heartbeatTimeout &&
        left.maximumFrameBytes == right.maximumFrameBytes;
}

[[nodiscard]] bool sameLogging(
    const LoggingConfig& left,
    const LoggingConfig& right)
{
    return left.directory == right.directory &&
        left.level == right.level &&
        left.maximumFileBytes == right.maximumFileBytes &&
        left.maximumFiles == right.maximumFiles;
}

[[nodiscard]] bool sameCamera(
    const vision::hardware::HikCameraDeviceConfig& left,
    const vision::hardware::HikCameraDeviceConfig& right)
{
    return left.role == right.role && left.model == right.model &&
        left.serialNumber == right.serialNumber &&
        left.exposureTime == right.exposureTime && left.gain == right.gain;
}

[[nodiscard]] bool sameVision(
    const VisionRuntimeConfig& left,
    const VisionRuntimeConfig& right)
{
    for (std::size_t index = 0; index < left.cameras.size(); ++index) {
        if (!sameCamera(left.cameras[index], right.cameras[index])) {
            return false;
        }
    }
    if (left.productionTrigger.mode != right.productionTrigger.mode ||
        left.productionTrigger.inputLine != right.productionTrigger.inputLine ||
        left.productionTrigger.activation != right.productionTrigger.activation ||
        left.productionTrigger.delay != right.productionTrigger.delay ||
        left.captureTimeout != right.captureTimeout ||
        left.algorithmConfigPath != right.algorithmConfigPath ||
        left.lightController.serialNumber !=
            right.lightController.serialNumber) {
        return false;
    }
    for (std::size_t index = 0;
         index < left.lightController.channels.size(); ++index) {
        const auto& a = left.lightController.channels[index];
        const auto& b = right.lightController.channels[index];
        if (a.role != b.role || a.outputSelector != b.outputSelector ||
            a.normalizedIntensity != b.normalizedIntensity) {
            return false;
        }
    }
    return true;
}

} // namespace

ApplicationController::ApplicationController(
    ApplicationConfig config,
    CoreFactory coreFactory,
    std::shared_ptr<spdlog::logger> logger,
    QObject* parent,
    std::filesystem::path configPath)
    : QObject(parent),
      config_(std::move(config)),
      configurationService_(std::move(configPath), config_),
      coreFactory_(std::move(coreFactory)),
      logger_(std::move(logger)),
      tcpServer_(config_.tcp, this),
      resultAckTimer_(new QTimer(this))
{
    qRegisterMetaType<vision::core::Error>();
    qRegisterMetaType<vision::core::CoreStatus>();
    qRegisterMetaType<VisionTaskCommand>();
    qRegisterMetaType<VisionTaskCompletion>();
    qRegisterMetaType<RecoveryCommand>();
    qRegisterMetaType<RecoveryCompletion>();
    qRegisterMetaType<ShutdownCompletion>();
    qRegisterMetaType<TcpTaskContext>();
    qRegisterMetaType<AppDiagnostic>();
    qRegisterMetaType<TaskDisplayResult>();
    qRegisterMetaType<TaskImageSnapshot>();
    qRegisterMetaType<TaskPresentationSnapshot>();
    qRegisterMetaType<ApplicationSnapshot>();
    qRegisterMetaType<ApplicationConfig>();
    qRegisterMetaType<VisionRuntimeConfig>();
    qRegisterMetaType<RuntimeReconfigurationCompletion>();
    qRegisterMetaType<InitialRuntimeRollbackCompletion>();
    qRegisterMetaType<AlgorithmValidationCompletion>();
    qRegisterMetaType<CalibrationMode>();
    qRegisterMetaType<CalibrationProcessState>();
    qRegisterMetaType<CalibrationSnapshot>();
    qRegisterMetaType<vision::core::CameraRole>();

    auto acknowledgementInterval =
        config_.resultDelivery.acknowledgementInterval;
    if (acknowledgementInterval <= std::chrono::milliseconds::zero() ||
        acknowledgementInterval > std::chrono::milliseconds{
            std::numeric_limits<int>::max()}) {
        acknowledgementInterval = std::chrono::seconds(1);
    }
    if (config_.resultDelivery.maximumRetries < 0) {
        config_.resultDelivery.maximumRetries = 0;
    }
    resultAckTimer_->setSingleShot(false);
    resultAckTimer_->setInterval(
        static_cast<int>(acknowledgementInterval.count()));

    connect(&visionThread_, &QThread::finished,
            this, &ApplicationController::onVisionThreadFinished);

    connect(&tcpServer_, &vision::tcp::VisionTcpServer::prepareLocateRequested,
            this, &ApplicationController::onPrepareLocate);
    connect(&tcpServer_, &vision::tcp::VisionTcpServer::queryStatusRequested,
            this, &ApplicationController::onQueryStatus);
    connect(&tcpServer_, &vision::tcp::VisionTcpServer::recoverRequested,
            this, &ApplicationController::onTcpRecover);
    connect(&tcpServer_, &vision::tcp::VisionTcpServer::resultAcknowledged,
            this, &ApplicationController::onResultAck);
    connect(&tcpServer_, &vision::tcp::VisionTcpServer::clientConnected,
            this, &ApplicationController::onClientConnected);
    connect(&tcpServer_, &vision::tcp::VisionTcpServer::clientDisconnected,
            this, &ApplicationController::onClientDisconnected);
    connect(&tcpServer_, &vision::tcp::VisionTcpServer::protocolMessage,
            this, [this](const QString& message) {
                writeLog(QStringLiteral("TCP: %1").arg(message));
            });
    connect(resultAckTimer_, &QTimer::timeout,
            this, &ApplicationController::onResultAckTimeout);
}

ApplicationController::~ApplicationController()
{
    assert(!visionThread_.isRunning());
    assert(worker_ == nullptr);
}

ApplicationSnapshot ApplicationController::snapshot() const
{
    ApplicationSnapshot value;
    value.applicationState = applicationState_;
    value.visionState = visionState_;
    value.taskState = taskState_;
    value.configurationApplyState = configurationApplyState_;
    value.activeConfigurationRevision = configurationService_.revision();
    value.runtimeInitialized = runtimeInitialized_;
    value.calibrationMaintenanceLocked = calibrationMaintenanceLocked_;
    value.tcpListening = tcpServer_.isListening();
    value.tcpConnection = tcpServer_.activeConnection();
    if (const auto task = activeTcpTask()) {
        value.activeTaskId = task->taskId;
    }
    value.lastError = lastError_;
    return value;
}

bool ApplicationController::isTcpListening() const noexcept
{
    return tcpServer_.isListening();
}

quint16 ApplicationController::tcpPort() const noexcept
{
    return tcpServer_.serverPort();
}

ApplicationConfig ApplicationController::configuration() const
{
    return config_;
}

const std::filesystem::path&
ApplicationController::configurationPath() const noexcept
{
    return configurationService_.path();
}

void ApplicationController::createWorker()
{
    if (worker_ != nullptr) {
        return;
    }

    worker_ = new VisionWorker(config_.vision, std::move(coreFactory_));
    worker_->moveToThread(&visionThread_);

    connect(this, &ApplicationController::initializeWorker,
            worker_, &VisionWorker::initialize, Qt::QueuedConnection);
    connect(this, &ApplicationController::locateWorker,
            worker_, &VisionWorker::locate, Qt::QueuedConnection);
    connect(this, &ApplicationController::recoverWorker,
            worker_, &VisionWorker::recover, Qt::QueuedConnection);
    connect(this, &ApplicationController::shutdownWorker,
            worker_, &VisionWorker::shutdown, Qt::QueuedConnection);
    connect(this, &ApplicationController::reconfigureWorker,
            worker_, &VisionWorker::reconfigureRuntime,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::rollbackInitialRuntimeWorker,
            worker_, &VisionWorker::rollbackInitialRuntime,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::validateAlgorithmWorker,
            worker_, &VisionWorker::validateAlgorithmConfiguration,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::setCalibrationPointFileWorker,
            worker_, &VisionWorker::setCalibrationPointFile,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::captureCalibrationWorker,
            worker_, &VisionWorker::captureCalibration,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::detectCalibrationWorker,
            worker_, &VisionWorker::detectCalibration,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::selectCalibrationPointWorker,
            worker_, &VisionWorker::selectCalibrationPoint,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::solveCalibrationWorker,
            worker_, &VisionWorker::solveCalibration,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::evaluateCalibrationWorker,
            worker_, &VisionWorker::evaluateCalibration,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::applyCalibrationWorker,
            worker_, &VisionWorker::applyCalibration,
            Qt::QueuedConnection);
    connect(this, &ApplicationController::resetCalibrationWorker,
            worker_, &VisionWorker::resetCalibration,
            Qt::QueuedConnection);

    connect(worker_, &VisionWorker::initialized,
            this, &ApplicationController::onWorkerInitialized);
    connect(worker_, &VisionWorker::initializationFailed,
            this, &ApplicationController::onWorkerInitializationFailed);
    connect(worker_, &VisionWorker::taskPrepared,
            this, &ApplicationController::onWorkerPrepared);
    connect(worker_, &VisionWorker::taskCompleted,
            this, &ApplicationController::onWorkerTaskCompleted);
    connect(worker_, &VisionWorker::recoveryCompleted,
            this, &ApplicationController::onWorkerRecoveryCompleted);
    connect(worker_, &VisionWorker::runtimeReconfigurationCompleted,
            this,
            &ApplicationController::onWorkerRuntimeReconfigurationCompleted);
    connect(worker_, &VisionWorker::initialRuntimeRollbackCompleted,
            this,
            &ApplicationController::onWorkerInitialRuntimeRollbackCompleted);
    connect(worker_, &VisionWorker::algorithmValidationCompleted,
            this,
            &ApplicationController::onWorkerAlgorithmValidationCompleted);
    connect(worker_, &VisionWorker::calibrationUpdated,
            this, &ApplicationController::onWorkerCalibrationUpdated);
    connect(worker_, &VisionWorker::diagnosticReceived,
            this, &ApplicationController::onWorkerDiagnostic);
    connect(worker_, &VisionWorker::shutdownCompleted,
            this, &ApplicationController::onWorkerShutdownCompleted);
    connect(worker_, &VisionWorker::shutdownCompleted,
            worker_, &QObject::deleteLater, Qt::DirectConnection);
    connect(worker_, &QObject::destroyed,
            this, &ApplicationController::onWorkerDestroyed);
}

void ApplicationController::start()
{
    if (started_ || shutdownRequested_) {
        return;
    }
    started_ = true;
    applicationState_ = ApplicationState::Starting;
    visionState_ = VisionServiceState::Starting;
    publishSnapshot();

    createWorker();
    visionThread_.setObjectName(QStringLiteral("VisionWorkerThread"));
    visionThread_.start();
    emit initializeWorker();
}

void ApplicationController::retryInitialization()
{
    if (shutdownRequested_ || !started_ || runtimeInitialized_ ||
        worker_ == nullptr || taskState_ != TaskState::Idle ||
        activeRecovery_.has_value() ||
        (configurationApplyState_ != ConfigurationApplyState::Idle &&
         configurationApplyState_ != ConfigurationApplyState::Failed)) {
        emit actionRejected(
            QStringLiteral("The current state does not allow vision initialization."));
        return;
    }
    applicationState_ = ApplicationState::Starting;
    visionState_ = VisionServiceState::Starting;
    lastError_.reset();
    publishSnapshot();
    emit initializeWorker();
}

void ApplicationController::retryTcp()
{
    if (shutdownRequested_ || !runtimeInitialized_ ||
        visionState_ != VisionServiceState::Ready ||
        (configurationApplyState_ != ConfigurationApplyState::Idle &&
         configurationApplyState_ != ConfigurationApplyState::Failed)) {
        emit actionRejected(
            QStringLiteral("The current state does not allow TCP restart."));
        return;
    }
    startTcpIfPossible();
}

void ApplicationController::requestLocalRecovery()
{
    beginRecovery(LocalRecoveryContext{++localRecoverySequence_});
}

void ApplicationController::requestAlgorithmValidation(const QString& path)
{
    if (shutdownRequested_ || worker_ == nullptr ||
        taskState_ != TaskState::Idle || activeRecovery_.has_value() ||
        calibrationMaintenanceLocked_ ||
        (configurationApplyState_ != ConfigurationApplyState::Idle &&
         configurationApplyState_ != ConfigurationApplyState::Failed)) {
        emit actionRejected(QStringLiteral(
            "Algorithm configuration can only be validated while idle."));
        return;
    }
    emit validateAlgorithmWorker(++algorithmValidationSequence_, path);
}

void ApplicationController::requestApplyConfiguration(
    ApplicationConfig candidate)
{
    const bool canInitializeFromFault =
        started_ && worker_ != nullptr && !runtimeInitialized_ &&
        visionState_ == VisionServiceState::Faulted;
    if (shutdownRequested_ ||
        (!runtimeInitialized_ && !canInitializeFromFault) ||
        taskState_ != TaskState::Idle || activeRecovery_.has_value() ||
        calibrationMaintenanceLocked_ ||
        (configurationApplyState_ != ConfigurationApplyState::Idle &&
         configurationApplyState_ != ConfigurationApplyState::Failed)) {
        emit actionRejected(QStringLiteral(
            "Configuration can only be applied while the vision service is idle."));
        return;
    }

    configurationApplyState_ = ConfigurationApplyState::Validating;
    publishSnapshot();
    if (auto status = configurationService_.validate(candidate);
        const auto* error = std::get_if<vision::core::Error>(&status)) {
        failConfigurationApply(*error);
        return;
    }

    pendingLogger_.reset();
    if (!sameLogging(config_.logging, candidate.logging)) {
        auto loggerOutcome = createApplicationLogger(candidate.logging);
        if (const auto* error =
                std::get_if<vision::core::Error>(&loggerOutcome)) {
            failConfigurationApply(*error);
            return;
        }
        pendingLogger_ = std::get<std::shared_ptr<spdlog::logger>>(
            std::move(loggerOutcome));
    }

    pendingInitializationApply_ = !runtimeInitialized_;
    pendingVisionChange_ = pendingInitializationApply_ ||
        !sameVision(config_.vision, candidate.vision);
    pendingTcpChange_ = !sameTcp(config_.tcp, candidate.tcp);
    pendingConfiguration_ = std::move(candidate);
    pendingApplyError_.reset();
    pendingTcpRollbackError_.reset();
    rollingBackConfiguration_ = false;
    rollingBackInitialRuntime_ = false;
    pendingTcpWasListening_ = tcpServer_.isListening();
    activeConfigurationRequest_ = ++configurationRequestSequence_;

    if (pendingTcpChange_ && !pendingInitializationApply_ &&
        !applyTcpConfiguration(pendingConfiguration_->tcp)) {
        const auto error = stableError(
            vision::core::ErrorCode::NotReady,
            "The new TCP configuration could not be applied.",
            tcpServer_.lastError().toUtf8().constData());
        const auto rollbackError = restorePreviousTcpConfiguration(
            pendingTcpWasListening_);
        if (rollbackError.has_value()) {
            vision::core::Error finalError = *rollbackError;
            finalError.diagnostic += "; original configuration apply failure: " +
                error.message + "; " + error.diagnostic;
            failConfigurationApply(finalError);
        }
        else {
            failConfigurationApply(error);
        }
        return;
    }

    if (pendingVisionChange_) {
        configurationApplyState_ = ConfigurationApplyState::Reconfiguring;
        visionState_ = VisionServiceState::Reconfiguring;
        publishSnapshot();
        emit reconfigureWorker(
            activeConfigurationRequest_, pendingConfiguration_->vision);
        return;
    }

    finishConfigurationApply();
}

void ApplicationController::requestCalibrationPointFile(
    CalibrationMode mode,
    vision::core::CameraRole role,
    const QString& path)
{
    if (shutdownRequested_ || worker_ == nullptr ||
        taskState_ != TaskState::Idle || activeRecovery_.has_value() ||
        calibrationMaintenanceLocked_ ||
        (configurationApplyState_ != ConfigurationApplyState::Idle &&
         configurationApplyState_ != ConfigurationApplyState::Failed)) {
        emit actionRejected(QStringLiteral(
            "理论点文件只能在视觉任务空闲且未进入标定维护时修改。"));
        return;
    }
    emit setCalibrationPointFileWorker(mode, role, path);
}

void ApplicationController::requestCalibrationCapture(CalibrationMode mode)
{
    if (shutdownRequested_ || worker_ == nullptr || !runtimeInitialized_ ||
        visionState_ != VisionServiceState::Ready ||
        taskState_ != TaskState::Idle || activeRecovery_.has_value() ||
        (configurationApplyState_ != ConfigurationApplyState::Idle &&
         configurationApplyState_ != ConfigurationApplyState::Failed)) {
        emit actionRejected(QStringLiteral(
            "当前状态不能开始标定采集。"));
        return;
    }
    // Reserve the maintenance gate before the queued worker operation starts,
    // so a TCP locate cannot enter during the cross-thread hand-off.
    calibrationMaintenanceLocked_ = true;
    calibrationSnapshot_.maintenanceLocked = true;
    publishSnapshot();
    emit calibrationSnapshotChanged(calibrationSnapshot_);
    emit captureCalibrationWorker(mode);
}

void ApplicationController::requestCalibrationDetection()
{
    if (!calibrationMaintenanceLocked_ || worker_ == nullptr) {
        emit actionRejected(QStringLiteral("请先采集标定图像。"));
        return;
    }
    emit detectCalibrationWorker();
}

void ApplicationController::requestCalibrationPointSelection(
    vision::core::CameraRole role, int detectionIndex)
{
    if (!calibrationMaintenanceLocked_ || worker_ == nullptr) return;
    emit selectCalibrationPointWorker(role, detectionIndex);
}

void ApplicationController::requestCalibrationSolve()
{
    if (!calibrationMaintenanceLocked_ || worker_ == nullptr) {
        emit actionRejected(QStringLiteral("标定对应关系尚未准备完成。"));
        return;
    }
    emit solveCalibrationWorker();
}

void ApplicationController::requestCalibrationEvaluation()
{
    if (!calibrationMaintenanceLocked_ || worker_ == nullptr) {
        emit actionRejected(QStringLiteral("没有可评估的标定候选。"));
        return;
    }
    emit evaluateCalibrationWorker();
}

void ApplicationController::requestCalibrationApply()
{
    if (!calibrationMaintenanceLocked_ || worker_ == nullptr) {
        emit actionRejected(QStringLiteral("没有可应用的标定候选。"));
        return;
    }
    emit applyCalibrationWorker();
}

void ApplicationController::requestCalibrationReset()
{
    if (worker_ == nullptr || shutdownRequested_ ||
        taskState_ != TaskState::Idle || activeRecovery_.has_value() ||
        (configurationApplyState_ != ConfigurationApplyState::Idle &&
         configurationApplyState_ != ConfigurationApplyState::Failed)) {
        emit actionRejected(QStringLiteral("当前状态不能重置标定会话。"));
        return;
    }
    emit resetCalibrationWorker();
}

void ApplicationController::shutdown()
{
    if (shutdownRequested_) {
        return;
    }
    shutdownRequested_ = true;
    applicationState_ = ApplicationState::ShuttingDown;
    resultAckTimer_->stop();
    pendingResult_.reset();
    if (taskState_ == TaskState::AwaitingResultAck) {
        finishTask();
    }
    tcpServer_.stop();
    publishSnapshot();

    if (!visionThread_.isRunning()) {
        if (worker_ != nullptr) {
            delete worker_;
            worker_ = nullptr;
        }
        visionState_ = VisionServiceState::Stopped;
        applicationState_ = ApplicationState::Stopped;
        runtimeInitialized_ = false;
        publishSnapshot();
        emit shutdownFinished();
        return;
    }
    if (worker_ != nullptr) {
        emit shutdownWorker();
    }
    else {
        visionThread_.quit();
    }
}

void ApplicationController::onWorkerInitialized(
    const vision::core::CoreStatus& status)
{
    if (shutdownRequested_) {
        return;
    }
    updateFromCoreStatus(status);
    runtimeInitialized_ = visionState_ == VisionServiceState::Ready;
    if (!runtimeInitialized_) {
        applicationState_ = ApplicationState::StartFailed;
        const auto error = status.lastError.value_or(stableError(
            vision::core::ErrorCode::InternalFailure,
            "The vision runtime did not enter Ready."));
        lastError_ = error;
        publishSnapshot();
        emit startupFailed(error);
        return;
    }

    applicationState_ = ApplicationState::Ready;
    startTcpIfPossible();
    writeLog(QStringLiteral("Vision runtime initialization completed."));
}

void ApplicationController::onWorkerInitializationFailed(
    const vision::core::Error& error)
{
    if (shutdownRequested_) {
        return;
    }
    runtimeInitialized_ = false;
    applicationState_ = ApplicationState::StartFailed;
    visionState_ = VisionServiceState::Faulted;
    lastError_ = error;
    writeError(error, QStringLiteral("Vision initialization"));
    publishSnapshot();
    emit startupFailed(error);
}

void ApplicationController::onWorkerPrepared(
    const TcpTaskContext& context)
{
    const auto active = activeTcpTask();
    if (!active || *active != context || taskState_ != TaskState::Preparing) {
        writeLog(
            QStringLiteral("Ignored a Prepared event for a stale task."),
            true);
        return;
    }
    taskState_ = TaskState::Running;
    publishSnapshot();
    if (isCurrentConnection(context.connection)) {
        static_cast<void>(tcpServer_.sendLocatePrepared(
            vision::tcp::LocatePreparedReply{
                context.taskId, context.tcpMode, context.connection}));
    }
}

void ApplicationController::onWorkerTaskCompleted(
    const VisionTaskCompletion& completion)
{
    if (!activeTask_ || !sameContext(*activeTask_, completion.context)) {
        writeLog(
            QStringLiteral("Ignored a vision result for a stale task."),
            true);
        return;
    }

    updateFromCoreStatus(completion.coreStatus);
    if (const auto* error =
            std::get_if<vision::core::Error>(&completion.outcome)) {
        lastError_ = *error;
        writeError(*error, QStringLiteral("Locate"));
    }
    const TaskDisplayResult display =
        makeTaskDisplayResult(completion.context, completion.outcome);
    emit taskResultChanged(display);
    emit taskPresentationChanged(TaskPresentationSnapshot{
        display, completion.images, QDateTime::currentDateTime()});

    const auto& tcpTask = completion.context;
    if (!shutdownRequested_ && isCurrentConnection(tcpTask.connection)) {
        const auto reply = makeLocateReply(tcpTask, completion.outcome);
        if (tcpServer_.sendLocateResult(reply)) {
            pendingResult_ = reply;
            resultRetriesSent_ = 0;
            taskState_ = TaskState::AwaitingResultAck;
            resultAckTimer_->start();
            publishSnapshot();
            return;
        }
    }
    writeLog(
        QStringLiteral(
            "Result for task %1 was not sent because its TCP session is inactive.")
            .arg(tcpTask.taskId),
        true);
    finishTask();
}

void ApplicationController::onWorkerRecoveryCompleted(
    const RecoveryCompletion& completion)
{
    if (!activeRecovery_ ||
        !sameRecovery(*activeRecovery_, completion.context)) {
        writeLog(
            QStringLiteral("Ignored a recovery result for a stale request."),
            true);
        return;
    }
    activeRecovery_.reset();
    updateFromCoreStatus(completion.coreStatus);

    const auto* error = std::get_if<vision::core::Error>(&completion.status);
    if (error != nullptr) {
        lastError_ = *error;
        writeError(*error, QStringLiteral("Recovery"));
    }
    else if (visionState_ == VisionServiceState::Ready) {
        lastError_.reset();
    }

    if (const auto* tcpContext =
            std::get_if<TcpRecoveryContext>(&completion.context);
        tcpContext != nullptr && isCurrentConnection(tcpContext->connection)) {
        vision::tcp::RecoverReply reply;
        reply.sequence = tcpContext->sequence;
        reply.connection = tcpContext->connection;
        reply.success = error == nullptr;
        if (error != nullptr) {
            reply.errorCode = toTcpErrorCode(error->code);
            reply.message = QString::fromStdString(error->message);
        }
        static_cast<void>(tcpServer_.sendRecoverResult(reply));
    }
    publishSnapshot();
}

void ApplicationController::onWorkerRuntimeReconfigurationCompleted(
    const RuntimeReconfigurationCompletion& completion)
{
    if (shutdownRequested_) {
        return;
    }
    if (completion.requestId != activeConfigurationRequest_ ||
        !pendingConfiguration_.has_value()) {
        writeLog(
            QStringLiteral("Ignored a stale runtime reconfiguration result."),
            true);
        return;
    }

    updateFromCoreStatus(completion.coreStatus);
    if (rollingBackConfiguration_) {
        rollingBackConfiguration_ = false;
        const auto applyError = pendingApplyError_.value_or(stableError(
            vision::core::ErrorCode::InternalFailure,
            "Configuration persistence failed."));
        if (!completion.applied) {
            visionState_ = VisionServiceState::Faulted;
            if (const auto* workerError =
                    std::get_if<vision::core::Error>(&completion.status)) {
                vision::core::Error finalError = *workerError;
                finalError.diagnostic +=
                    "; original configuration apply failure: " +
                    applyError.message + "; " + applyError.diagnostic;
                if (pendingTcpRollbackError_.has_value()) {
                    finalError.diagnostic += "; TCP rollback failure: " +
                        pendingTcpRollbackError_->message + "; " +
                        pendingTcpRollbackError_->diagnostic;
                }
                failConfigurationApply(finalError);
                return;
            }
            failConfigurationApply(applyError);
            return;
        }
        visionState_ = VisionServiceState::Ready;
        if (pendingTcpRollbackError_.has_value()) {
            vision::core::Error finalError = *pendingTcpRollbackError_;
            finalError.diagnostic += "; original configuration apply failure: " +
                applyError.message + "; " + applyError.diagnostic;
            failConfigurationApply(finalError);
        }
        else {
            failConfigurationApply(applyError);
        }
        return;
    }

    if (!completion.applied) {
        if (pendingTcpChange_ && !pendingInitializationApply_) {
            pendingTcpRollbackError_ = restorePreviousTcpConfiguration(
                pendingTcpWasListening_);
        }
        const auto* error =
            std::get_if<vision::core::Error>(&completion.status);
        const auto applyError = error != nullptr ? *error : stableError(
            vision::core::ErrorCode::InternalFailure,
            "The vision configuration could not be applied.");
        if (pendingTcpRollbackError_.has_value()) {
            vision::core::Error finalError = *pendingTcpRollbackError_;
            finalError.diagnostic += "; original configuration apply failure: " +
                applyError.message + "; " + applyError.diagnostic;
            failConfigurationApply(finalError);
        }
        else {
            failConfigurationApply(applyError);
        }
        return;
    }

    if (pendingInitializationApply_) {
        runtimeInitialized_ = true;
        if (!applyTcpConfiguration(pendingConfiguration_->tcp)) {
            const QByteArray diagnostic = tcpServer_.lastError().toUtf8();
            beginInitialConfigurationRollback(stableError(
                vision::core::ErrorCode::NotReady,
                "The TCP server could not start after vision reinitialization.",
                diagnostic.constData()));
            return;
        }
    }

    visionState_ = VisionServiceState::Ready;
    finishConfigurationApply();
}

void ApplicationController::onWorkerInitialRuntimeRollbackCompleted(
    const InitialRuntimeRollbackCompletion& completion)
{
    if (shutdownRequested_) {
        return;
    }
    if (completion.requestId != activeConfigurationRequest_ ||
        !pendingConfiguration_.has_value() ||
        !rollingBackInitialRuntime_) {
        writeLog(
            QStringLiteral("Ignored a stale initial runtime rollback result."),
            true);
        return;
    }

    rollingBackInitialRuntime_ = false;
    runtimeInitialized_ = false;
    visionState_ = VisionServiceState::Faulted;
    applicationState_ = ApplicationState::StartFailed;
    const auto applyError = pendingApplyError_.value_or(stableError(
        vision::core::ErrorCode::InternalFailure,
        "The configuration transaction failed."));

    if (const auto* workerError =
            std::get_if<vision::core::Error>(&completion.status)) {
        vision::core::Error finalError = *workerError;
        finalError.diagnostic += "; original configuration apply failure: " +
            applyError.message + "; " + applyError.diagnostic;
        if (pendingTcpRollbackError_.has_value()) {
            finalError.diagnostic += "; TCP rollback failure: " +
                pendingTcpRollbackError_->message + "; " +
                pendingTcpRollbackError_->diagnostic;
        }
        failConfigurationApply(finalError);
        return;
    }

    if (pendingTcpRollbackError_.has_value()) {
        vision::core::Error finalError = *pendingTcpRollbackError_;
        finalError.diagnostic += "; original configuration apply failure: " +
            applyError.message + "; " + applyError.diagnostic;
        failConfigurationApply(finalError);
        return;
    }
    failConfigurationApply(applyError);
}

void ApplicationController::onWorkerDiagnostic(
    const AppDiagnostic& diagnostic)
{
    QString message = QStringLiteral("%1/%2: %3")
        .arg(diagnostic.component, diagnostic.operation, diagnostic.message);
    if (!diagnostic.frames.isEmpty()) {
        message += QStringLiteral(" [%1]").arg(diagnostic.frames.join(", "));
    }
    writeLog(
        message,
        diagnostic.severity != vision::core::DiagnosticSeverity::Info);
}

void ApplicationController::onWorkerAlgorithmValidationCompleted(
    const AlgorithmValidationCompletion& completion)
{
    emit algorithmValidationFinished(completion);
}

void ApplicationController::onWorkerCalibrationUpdated(
    const CalibrationSnapshot& snapshot)
{
    const bool lockChanged =
        calibrationMaintenanceLocked_ != snapshot.maintenanceLocked;
    const bool runtimeWasInitialized = runtimeInitialized_;
    calibrationSnapshot_ = snapshot;
    calibrationMaintenanceLocked_ = snapshot.maintenanceLocked;
    if (!snapshot.runtimeAvailable && runtimeInitialized_) {
        runtimeInitialized_ = false;
        visionState_ = VisionServiceState::Faulted;
        applicationState_ = ApplicationState::StartFailed;
        if (snapshot.error) lastError_ = snapshot.error;
    }
    if (snapshot.error) {
        writeError(*snapshot.error, QStringLiteral("Calibration"));
    }
    if (lockChanged || runtimeWasInitialized != runtimeInitialized_) {
        publishSnapshot();
    }
    emit calibrationSnapshotChanged(snapshot);
}

void ApplicationController::onWorkerShutdownCompleted(
    const ShutdownCompletion& completion)
{
    if (const auto* error =
            std::get_if<vision::core::Error>(&completion.status)) {
        writeError(*error, QStringLiteral("Vision shutdown"));
    }
    runtimeInitialized_ = false;
    visionState_ = VisionServiceState::Stopped;
    activeTask_.reset();
    activeRecovery_.reset();
    calibrationMaintenanceLocked_ = false;
    taskState_ = TaskState::Idle;
    publishSnapshot();
    // The worker is deleted while its event loop is still active. The thread
    // is stopped only after onWorkerDestroyed observes that deletion.
}

void ApplicationController::onWorkerDestroyed()
{
    worker_ = nullptr;
    if (visionThread_.isRunning()) {
        visionThread_.quit();
    }
}

void ApplicationController::onVisionThreadFinished()
{
    runtimeInitialized_ = false;
    visionState_ = VisionServiceState::Stopped;
    applicationState_ = ApplicationState::Stopped;
    publishSnapshot();
    if (shutdownRequested_) {
        emit shutdownFinished();
    }
}

void ApplicationController::onPrepareLocate(
    const vision::tcp::PrepareLocateCommand& command)
{
    if (!isCurrentConnection(command.connection)) {
        return;
    }
    if (shutdownRequested_ ||
        applicationState_ != ApplicationState::Ready ||
        !runtimeInitialized_ || visionState_ != VisionServiceState::Ready) {
        rejectPrepare(
            command,
            vision::core::ErrorCode::NotReady,
            QStringLiteral("The vision service is not ready."));
        return;
    }
    if (configurationApplyState_ != ConfigurationApplyState::Idle &&
        configurationApplyState_ != ConfigurationApplyState::Failed) {
        rejectPrepare(
            command,
            vision::core::ErrorCode::Busy,
            QStringLiteral("A configuration transaction is active."));
        return;
    }
    if (taskState_ != TaskState::Idle || activeRecovery_.has_value()) {
        rejectPrepare(
            command,
            vision::core::ErrorCode::Busy,
            QStringLiteral("A locate or recovery task is already active."));
        return;
    }
    if (calibrationMaintenanceLocked_) {
        rejectPrepare(
            command,
            vision::core::ErrorCode::Busy,
            QStringLiteral("Calibration maintenance is active."));
        return;
    }

    TcpTaskContext context{
        command.taskId,
        command.connection,
        command.mode,
        vision::core::LocateOptions{toCoreMode(command.mode)}};
    activeTask_ = context;
    taskState_ = TaskState::Preparing;
    publishSnapshot();
    emit locateWorker(VisionTaskCommand{context});
}

void ApplicationController::onQueryStatus(
    const vision::tcp::QueryStatusCommand& command)
{
    if (!isCurrentConnection(command.connection)) {
        return;
    }
    vision::tcp::StatusReply reply;
    reply.sequence = command.sequence;
    reply.applicationState = toTcpApplicationState(applicationState_);
    reply.visionState = toTcpVisionState(visionState_);
    reply.taskState = toTcpTaskState(taskState_);
    if (const auto task = activeTcpTask()) {
        reply.activeTaskId = task->taskId;
    }
    if (lastError_) {
        reply.lastErrorCode = toTcpErrorCode(lastError_->code);
    }
    reply.connection = command.connection;
    static_cast<void>(tcpServer_.sendStatus(reply));
}

void ApplicationController::onTcpRecover(
    const vision::tcp::RecoverCommand& command)
{
    if (!isCurrentConnection(command.connection)) {
        return;
    }
    beginRecovery(TcpRecoveryContext{command.sequence, command.connection});
}

void ApplicationController::onResultAck(
    const vision::tcp::ResultAck& acknowledgement)
{
    const auto task = activeTcpTask();
    if (taskState_ != TaskState::AwaitingResultAck || !task ||
        task->taskId != acknowledgement.taskId ||
        task->connection != acknowledgement.connection) {
        writeLog(
            QStringLiteral("Ignored a ResultAck for a stale task."),
            true);
        return;
    }
    writeLog(
        QStringLiteral("Vision result for task %1 was acknowledged.")
            .arg(task->taskId));
    finishTask();
}

void ApplicationController::onClientConnected(
    vision::tcp::ConnectionContext connection)
{
    writeLog(
        QStringLiteral("TCP client connected with generation %1.")
            .arg(connection.generation));
    publishSnapshot();
}

void ApplicationController::onClientDisconnected(
    vision::tcp::ConnectionContext connection)
{
    writeLog(
        QStringLiteral("TCP client generation %1 disconnected.")
            .arg(connection.generation),
        true);
    const auto task = activeTcpTask();
    if (task && task->connection == connection &&
        taskState_ == TaskState::AwaitingResultAck) {
        writeLog(
            QStringLiteral("Result delivery for task %1 was not acknowledged.")
                .arg(task->taskId),
            true);
        finishTask();
    }
    publishSnapshot();
}

void ApplicationController::onResultAckTimeout()
{
    const auto task = activeTcpTask();
    if (taskState_ != TaskState::AwaitingResultAck || !task ||
        !pendingResult_.has_value()) {
        resultAckTimer_->stop();
        return;
    }
    if (!isCurrentConnection(task->connection)) {
        writeLog(
            QStringLiteral("Result delivery for task %1 was not acknowledged.")
                .arg(task->taskId),
            true);
        finishTask();
        return;
    }
    if (resultRetriesSent_ < config_.resultDelivery.maximumRetries) {
        static_cast<void>(tcpServer_.sendLocateResult(*pendingResult_));
        ++resultRetriesSent_;
        writeLog(
            QStringLiteral("Resent result for task %1 (%2/%3).")
                .arg(task->taskId)
                .arg(resultRetriesSent_)
                .arg(config_.resultDelivery.maximumRetries),
            true);
        return;
    }

    writeLog(
        QStringLiteral(
            "Result for task %1 was not acknowledged; closing the TCP session.")
            .arg(task->taskId),
        true);
    tcpServer_.disconnectClient(task->connection);
    finishTask();
}

bool ApplicationController::canAcceptTask() const noexcept
{
    return !shutdownRequested_ && runtimeInitialized_ &&
        applicationState_ == ApplicationState::Ready &&
        visionState_ == VisionServiceState::Ready &&
        taskState_ == TaskState::Idle && !activeRecovery_.has_value() &&
        !calibrationMaintenanceLocked_ &&
        (configurationApplyState_ == ConfigurationApplyState::Idle ||
         configurationApplyState_ == ConfigurationApplyState::Failed);
}

bool ApplicationController::isCurrentConnection(
    vision::tcp::ConnectionContext connection) const noexcept
{
    const auto active = tcpServer_.activeConnection();
    return active.has_value() && *active == connection;
}

std::optional<TcpTaskContext> ApplicationController::activeTcpTask() const
{
    return activeTask_;
}

vision::core::Error ApplicationController::stableError(
    vision::core::ErrorCode code,
    const char* message,
    const char* diagnostic) const
{
    return vision::core::Error{code, message, diagnostic};
}

void ApplicationController::beginRecovery(RecoveryContext context)
{
    auto reject = [this, &context](const vision::core::Error& error) {
        if (const auto* tcp = std::get_if<TcpRecoveryContext>(&context);
            tcp != nullptr && isCurrentConnection(tcp->connection)) {
            vision::tcp::RecoverReply reply;
            reply.sequence = tcp->sequence;
            reply.success = false;
            reply.errorCode = toTcpErrorCode(error.code);
            reply.message = QString::fromStdString(error.message);
            reply.connection = tcp->connection;
            static_cast<void>(tcpServer_.sendRecoverResult(reply));
        }
        else {
            emit actionRejected(QString::fromStdString(error.message));
        }
    };

    if (shutdownRequested_ || !runtimeInitialized_ ||
        (configurationApplyState_ != ConfigurationApplyState::Idle &&
         configurationApplyState_ != ConfigurationApplyState::Failed)) {
        reject(stableError(
            vision::core::ErrorCode::NotReady,
            "The vision service is not initialized."));
        return;
    }
    if (taskState_ != TaskState::Idle || activeRecovery_.has_value() ||
        calibrationMaintenanceLocked_) {
        reject(stableError(
            vision::core::ErrorCode::Busy,
            "A locate, recovery, or calibration task is already active."));
        return;
    }
    activeRecovery_ = context;
    visionState_ = VisionServiceState::Recovering;
    publishSnapshot();
    emit recoverWorker(RecoveryCommand{std::move(context)});
}

void ApplicationController::finishTask()
{
    resultAckTimer_->stop();
    pendingResult_.reset();
    resultRetriesSent_ = 0;
    activeTask_.reset();
    taskState_ = TaskState::Idle;
    publishSnapshot();
}

void ApplicationController::startTcpIfPossible()
{
    if (shutdownRequested_ ||
        !runtimeInitialized_ || visionState_ != VisionServiceState::Ready) {
        return;
    }
    if (tcpServer_.isListening()) {
        applicationState_ = ApplicationState::Ready;
        publishSnapshot();
        return;
    }
    if (!tcpServer_.start()) {
        const QByteArray diagnostic = tcpServer_.lastError().toUtf8();
        const auto error = stableError(
            vision::core::ErrorCode::NotReady,
            "The TCP server could not start.",
            diagnostic.constData());
        applicationState_ = ApplicationState::StartFailed;
        lastError_ = error;
        writeError(error, QStringLiteral("TCP startup"));
        publishSnapshot();
        emit startupFailed(error);
        return;
    }

    applicationState_ = ApplicationState::Ready;
    writeLog(
        QStringLiteral("TCP is listening on %1:%2.")
            .arg(config_.tcp.listenAddress.toString())
            .arg(tcpServer_.serverPort()));
    publishSnapshot();
}

void ApplicationController::rejectPrepare(
    const vision::tcp::PrepareLocateCommand& command,
    vision::core::ErrorCode code,
    const QString& message)
{
    vision::tcp::PrepareRejectedReply reply;
    reply.taskId = command.taskId;
    reply.errorCode = toTcpErrorCode(code);
    reply.message = message;
    if (const auto task = activeTcpTask()) {
        reply.activeTaskId = task->taskId;
    }
    reply.connection = command.connection;
    static_cast<void>(tcpServer_.sendPrepareRejected(reply));
}

void ApplicationController::updateFromCoreStatus(
    const vision::core::CoreStatus& status)
{
    visionState_ = toVisionServiceState(status.state);
    if (status.lastError.has_value()) {
        lastError_ = status.lastError;
    }
    else if (status.state == vision::core::CoreState::Ready) {
        lastError_.reset();
    }
}

void ApplicationController::finishConfigurationApply()
{
    if (!pendingConfiguration_) {
        return;
    }
    configurationApplyState_ = ConfigurationApplyState::Persisting;
    publishSnapshot();

    auto status = configurationService_.commit(*pendingConfiguration_);
    if (const auto* error = std::get_if<vision::core::Error>(&status)) {
        pendingApplyError_ = *error;
        if (pendingInitializationApply_) {
            beginInitialConfigurationRollback(*error);
            return;
        }
        if (pendingTcpChange_) {
            pendingTcpRollbackError_ = restorePreviousTcpConfiguration(
                pendingTcpWasListening_);
        }
        if (pendingVisionChange_) {
            rollingBackConfiguration_ = true;
            configurationApplyState_ = ConfigurationApplyState::RollingBack;
            visionState_ = VisionServiceState::Reconfiguring;
            publishSnapshot();
            emit reconfigureWorker(
                activeConfigurationRequest_, config_.vision);
            return;
        }
        if (pendingTcpRollbackError_.has_value()) {
            vision::core::Error finalError = *pendingTcpRollbackError_;
            finalError.diagnostic += "; original configuration apply failure: " +
                error->message + "; " + error->diagnostic;
            failConfigurationApply(finalError);
        }
        else {
            failConfigurationApply(*error);
        }
        return;
    }

    config_ = *pendingConfiguration_;
    auto acknowledgementInterval =
        config_.resultDelivery.acknowledgementInterval;
    resultAckTimer_->setInterval(static_cast<int>(
        std::min<std::chrono::milliseconds::rep>(
            acknowledgementInterval.count(),
            std::numeric_limits<int>::max())));
    if (pendingLogger_) {
        logger_ = std::move(pendingLogger_);
    }

    pendingConfiguration_.reset();
    pendingApplyError_.reset();
    pendingTcpRollbackError_.reset();
    pendingVisionChange_ = false;
    pendingTcpChange_ = false;
    pendingInitializationApply_ = false;
    rollingBackConfiguration_ = false;
    rollingBackInitialRuntime_ = false;
    pendingTcpWasListening_ = false;
    configurationApplyState_ = ConfigurationApplyState::Idle;
    visionState_ = VisionServiceState::Ready;
    if (tcpServer_.isListening()) {
        applicationState_ = ApplicationState::Ready;
    }
    lastError_.reset();
    writeLog(QStringLiteral("Application configuration was applied."));
    publishSnapshot();
    emit configurationApplied(config_);
}

void ApplicationController::beginInitialConfigurationRollback(
    const vision::core::Error& applyError)
{
    pendingApplyError_ = applyError;
    pendingTcpRollbackError_ = restorePreviousTcpConfiguration(
        pendingTcpWasListening_);
    runtimeInitialized_ = false;
    rollingBackInitialRuntime_ = true;
    configurationApplyState_ = ConfigurationApplyState::RollingBack;
    visionState_ = VisionServiceState::Reconfiguring;
    publishSnapshot();
    emit rollbackInitialRuntimeWorker(
        activeConfigurationRequest_, config_.vision);
}

void ApplicationController::failConfigurationApply(
    const vision::core::Error& error)
{
    pendingConfiguration_.reset();
    pendingLogger_.reset();
    pendingApplyError_.reset();
    pendingTcpRollbackError_.reset();
    pendingVisionChange_ = false;
    pendingTcpChange_ = false;
    pendingInitializationApply_ = false;
    rollingBackConfiguration_ = false;
    rollingBackInitialRuntime_ = false;
    pendingTcpWasListening_ = false;
    configurationApplyState_ = ConfigurationApplyState::Failed;
    lastError_ = error;
    writeError(error, QStringLiteral("Configuration apply"));
    publishSnapshot();
    emit actionRejected(QString::fromStdString(error.message));
}

bool ApplicationController::applyTcpConfiguration(
    const vision::tcp::TcpServerConfig& configuration)
{
    configurationApplyState_ = ConfigurationApplyState::RestartingTcp;
    publishSnapshot();
    tcpServer_.stop();
    return tcpServer_.reconfigure(configuration) && tcpServer_.start();
}

std::optional<vision::core::Error>
ApplicationController::restorePreviousTcpConfiguration(bool restoreListening)
{
    tcpServer_.stop();
    if (!tcpServer_.reconfigure(config_.tcp) ||
        (restoreListening && !tcpServer_.start())) {
        const auto error = stableError(
            vision::core::ErrorCode::NotReady,
            "The previous TCP configuration could not be restored.",
            tcpServer_.lastError().toUtf8().constData());
        lastError_ = error;
        applicationState_ = ApplicationState::StartFailed;
        writeError(error, QStringLiteral("TCP rollback"));
        return error;
    }
    return std::nullopt;
}

void ApplicationController::publishSnapshot()
{
    emit snapshotChanged(snapshot());
}

void ApplicationController::writeLog(
    const QString& message,
    bool warning)
{
    try {
        if (logger_) {
            if (warning) {
                logger_->warn("{}", message.toStdString());
            }
            else {
                logger_->info("{}", message.toStdString());
            }
        }
    }
    catch (...) {
        // Logging is best effort and must not alter application behavior.
    }
    emit logMessage(message);
}

void ApplicationController::writeError(
    const vision::core::Error& error,
    const QString& operation)
{
    const QString stableMessage = QStringLiteral("%1 failed: %2")
        .arg(operation, QString::fromStdString(error.message));
    try {
        if (logger_) {
            logger_->error(
                "{} | code={} | diagnostic={}",
                stableMessage.toStdString(),
                static_cast<int>(error.code),
                error.diagnostic);
        }
    }
    catch (...) {
        // Logging is best effort and must not alter application behavior.
    }
    emit logMessage(stableMessage);
}

} // namespace vision::app
