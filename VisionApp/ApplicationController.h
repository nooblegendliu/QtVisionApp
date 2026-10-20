#pragma once

#include "AppConfig.h"
#include "AppTypes.h"
#include "ConfigurationService.h"
#include "VisionRuntimeFactory.h"

#include <VisionTcp/VisionTcpServer.h>

#include <QObject>
#include <QThread>

#include <memory>
#include <optional>

namespace spdlog {
class logger;
}

class QTimer;

namespace vision::app {

class VisionWorker;

class ApplicationController final : public QObject {
    Q_OBJECT

public:
    ApplicationController(
        ApplicationConfig config,
        CoreFactory coreFactory,
        std::shared_ptr<spdlog::logger> logger,
        QObject* parent = nullptr,
        std::filesystem::path configPath = {});
    ~ApplicationController() override;

    [[nodiscard]] ApplicationSnapshot snapshot() const;
    [[nodiscard]] bool isTcpListening() const noexcept;
    [[nodiscard]] quint16 tcpPort() const noexcept;
    [[nodiscard]] ApplicationConfig configuration() const;
    [[nodiscard]] const std::filesystem::path& configurationPath() const noexcept;

public slots:
    void start();
    void retryInitialization();
    void retryTcp();
    void requestLocalRecovery();
    void requestAlgorithmValidation(const QString& path);
    void requestApplyConfiguration(vision::app::ApplicationConfig candidate);
    void requestCalibrationPointFile(
        vision::app::CalibrationMode mode,
        vision::core::CameraRole role,
        const QString& path);
    void requestCalibrationCapture(vision::app::CalibrationMode mode);
    void requestCalibrationDetection();
    void requestCalibrationPointSelection(
        vision::core::CameraRole role, int detectionIndex);
    void requestCalibrationSolve();
    void requestCalibrationEvaluation();
    void requestCalibrationApply();
    void requestCalibrationReset();
    void shutdown();

signals:
    void snapshotChanged(const vision::app::ApplicationSnapshot& snapshot);
    void taskResultChanged(const vision::app::TaskDisplayResult& result);
    void taskPresentationChanged(
        const vision::app::TaskPresentationSnapshot& presentation);
    void logMessage(const QString& message);
    void actionRejected(const QString& reason);
    void configurationApplied(
        const vision::app::ApplicationConfig& configuration);
    void algorithmValidationFinished(
        const vision::app::AlgorithmValidationCompletion& completion);
    void calibrationSnapshotChanged(
        const vision::app::CalibrationSnapshot& snapshot);
    void startupFailed(const vision::core::Error& error);
    void shutdownFinished();

    void initializeWorker();
    void locateWorker(const vision::app::VisionTaskCommand& command);
    void recoverWorker(const vision::app::RecoveryCommand& command);
    void shutdownWorker();
    void reconfigureWorker(
        quint64 requestId,
        const vision::app::VisionRuntimeConfig& configuration);
    void rollbackInitialRuntimeWorker(
        quint64 requestId,
        const vision::app::VisionRuntimeConfig& previousConfiguration);
    void validateAlgorithmWorker(quint64 requestId, const QString& path);
    void setCalibrationPointFileWorker(
        vision::app::CalibrationMode mode,
        vision::core::CameraRole role,
        const QString& path);
    void captureCalibrationWorker(vision::app::CalibrationMode mode);
    void detectCalibrationWorker();
    void selectCalibrationPointWorker(
        vision::core::CameraRole role, int detectionIndex);
    void solveCalibrationWorker();
    void evaluateCalibrationWorker();
    void applyCalibrationWorker();
    void resetCalibrationWorker();

private slots:
    void onWorkerInitialized(const vision::core::CoreStatus& status);
    void onWorkerInitializationFailed(const vision::core::Error& error);
    void onWorkerPrepared(const vision::app::TcpTaskContext& context);
    void onWorkerTaskCompleted(
        const vision::app::VisionTaskCompletion& completion);
    void onWorkerRecoveryCompleted(
        const vision::app::RecoveryCompletion& completion);
    void onWorkerRuntimeReconfigurationCompleted(
        const vision::app::RuntimeReconfigurationCompletion& completion);
    void onWorkerInitialRuntimeRollbackCompleted(
        const vision::app::InitialRuntimeRollbackCompletion& completion);
    void onWorkerAlgorithmValidationCompleted(
        const vision::app::AlgorithmValidationCompletion& completion);
    void onWorkerCalibrationUpdated(
        const vision::app::CalibrationSnapshot& snapshot);
    void onWorkerDiagnostic(const vision::app::AppDiagnostic& diagnostic);
    void onWorkerShutdownCompleted(
        const vision::app::ShutdownCompletion& completion);
    void onWorkerDestroyed();
    void onVisionThreadFinished();

    void onPrepareLocate(
        const vision::tcp::PrepareLocateCommand& command);
    void onQueryStatus(
        const vision::tcp::QueryStatusCommand& command);
    void onTcpRecover(const vision::tcp::RecoverCommand& command);
    void onResultAck(const vision::tcp::ResultAck& acknowledgement);
    void onClientConnected(vision::tcp::ConnectionContext connection);
    void onClientDisconnected(vision::tcp::ConnectionContext connection);
    void onResultAckTimeout();

private:
    [[nodiscard]] bool canAcceptTask() const noexcept;
    [[nodiscard]] bool isCurrentConnection(
        vision::tcp::ConnectionContext connection) const noexcept;
    [[nodiscard]] std::optional<TcpTaskContext> activeTcpTask() const;
    [[nodiscard]] vision::core::Error stableError(
        vision::core::ErrorCode code,
        const char* message,
        const char* diagnostic = "") const;
    void beginRecovery(RecoveryContext context);
    void finishTask();
    void createWorker();
    void startTcpIfPossible();
    void rejectPrepare(
        const vision::tcp::PrepareLocateCommand& command,
        vision::core::ErrorCode code,
        const QString& message);
    void updateFromCoreStatus(const vision::core::CoreStatus& status);
    void finishConfigurationApply();
    void beginInitialConfigurationRollback(
        const vision::core::Error& applyError);
    void failConfigurationApply(const vision::core::Error& error);
    bool applyTcpConfiguration(
        const vision::tcp::TcpServerConfig& configuration);
    [[nodiscard]] std::optional<vision::core::Error>
        restorePreviousTcpConfiguration(bool restoreListening);
    void publishSnapshot();
    void writeLog(const QString& message, bool warning = false);
    void writeError(const vision::core::Error& error, const QString& operation);

    ApplicationConfig config_;
    ConfigurationService configurationService_;
    CoreFactory coreFactory_;
    std::shared_ptr<spdlog::logger> logger_;

    QThread visionThread_;
    VisionWorker* worker_{nullptr};
    vision::tcp::VisionTcpServer tcpServer_;
    QTimer* resultAckTimer_{nullptr};

    ApplicationState applicationState_{ApplicationState::Starting};
    VisionServiceState visionState_{VisionServiceState::Starting};
    TaskState taskState_{TaskState::Idle};
    bool runtimeInitialized_{false};
    bool started_{false};
    bool shutdownRequested_{false};
    ConfigurationApplyState configurationApplyState_{
        ConfigurationApplyState::Idle};
    quint64 configurationRequestSequence_{0};
    quint64 activeConfigurationRequest_{0};
    bool pendingVisionChange_{false};
    bool pendingTcpChange_{false};
    bool pendingInitializationApply_{false};
    bool rollingBackConfiguration_{false};
    bool rollingBackInitialRuntime_{false};
    bool pendingTcpWasListening_{false};
    std::optional<ApplicationConfig> pendingConfiguration_;
    std::optional<vision::core::Error> pendingApplyError_;
    std::optional<vision::core::Error> pendingTcpRollbackError_;
    std::shared_ptr<spdlog::logger> pendingLogger_;
    quint64 localRecoverySequence_{0};
    quint64 algorithmValidationSequence_{0};
    std::optional<TaskContext> activeTask_;
    std::optional<RecoveryContext> activeRecovery_;
    std::optional<vision::core::Error> lastError_;
    std::optional<vision::tcp::LocateResultReply> pendingResult_;
    CalibrationSnapshot calibrationSnapshot_;
    bool calibrationMaintenanceLocked_{false};
    int resultRetriesSent_{0};
};

} // namespace vision::app
