#pragma once

#include "AppTypes.h"
#include "VisionRuntimeFactory.h"

#include <VisionHardware/HikHardware.h>

#include <QObject>

#include <memory>
#include <optional>

namespace vision::algorithm {
class CalibrationAlgorithm;
}

namespace vision::app {

class DiagnosticBridge;

class VisionWorker final : public QObject,
                           public vision::hardware::ICaptureObserver,
                           public vision::core::ILocateFrameObserver {
    Q_OBJECT

public:
    VisionWorker(VisionRuntimeConfig runtimeConfig, CoreFactory coreFactory,
                 QObject* parent = nullptr);
    ~VisionWorker() override;

    void onPrepared(vision::core::LocateMode mode) noexcept override;
    void onFrames(const vision::core::ImageFrames& frames) noexcept override;

public slots:
    void initialize();
    void locate(const vision::app::VisionTaskCommand& command);
    void recover(const vision::app::RecoveryCommand& command);
    void reconfigureRuntime(
        quint64 requestId,
        const vision::app::VisionRuntimeConfig& candidate);
    void rollbackInitialRuntime(
        quint64 requestId,
        const vision::app::VisionRuntimeConfig& previousConfiguration);
    void validateAlgorithmConfiguration(quint64 requestId, const QString& path);
    void setCalibrationPointFile(
        vision::app::CalibrationMode mode,
        vision::core::CameraRole role,
        const QString& path);
    void captureCalibration(vision::app::CalibrationMode mode);
    void detectCalibration();
    void selectCalibrationPoint(
        vision::core::CameraRole role, int detectionIndex);
    void solveCalibration();
    void evaluateCalibration();
    void applyCalibration();
    void resetCalibration();
    void shutdown();

signals:
    void initialized(const vision::core::CoreStatus& status);
    void initializationFailed(const vision::core::Error& error);
    void taskPrepared(const vision::app::TcpTaskContext& context);
    void taskCompleted(const vision::app::VisionTaskCompletion& completion);
    void recoveryCompleted(const vision::app::RecoveryCompletion& completion);
    void runtimeReconfigurationCompleted(
        const vision::app::RuntimeReconfigurationCompletion& completion);
    void initialRuntimeRollbackCompleted(
        const vision::app::InitialRuntimeRollbackCompletion& completion);
    void algorithmValidationCompleted(
        const vision::app::AlgorithmValidationCompletion& completion);
    void calibrationUpdated(
        const vision::app::CalibrationSnapshot& snapshot);
    void diagnosticReceived(const vision::app::AppDiagnostic& diagnostic);
    void shutdownCompleted(const vision::app::ShutdownCompletion& completion);

private:
    [[nodiscard]] vision::core::Error makeWorkerError(
        vision::core::ErrorCode code,
        const char* message,
        const char* diagnostic) const;
    [[nodiscard]] vision::core::CoreStatus currentStatus() const;
    void ensureDiagnosticBridge();
    void initializeCalibrationBackend();
    void publishCalibration(
        vision::app::CalibrationProcessState state,
        QString message = {},
        std::optional<vision::core::Error> error = std::nullopt);

    struct CalibrationSession;
    VisionRuntimeConfig runtimeConfig_;
    CoreFactory coreFactory_;
    DiagnosticBridge* diagnostics_{nullptr};
    std::unique_ptr<vision::core::VisionCore> core_;
    std::unique_ptr<vision::algorithm::CalibrationAlgorithm>
        calibrationAlgorithm_;
    std::unique_ptr<CalibrationSession> calibrationSession_;
    std::optional<TaskContext> activeTask_;
    QVector<TaskImageSnapshot> activeImages_;
    bool shuttingDown_{false};
};

} // namespace vision::app
