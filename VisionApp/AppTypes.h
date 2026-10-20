#pragma once

#include <VisionCore/Diagnostics.h>
#include <VisionCore/VisionCore.h>
#include <VisionTcp/VisionTcpTypes.h>

#include <QDateTime>
#include <QImage>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>
#include <variant>

namespace vision::app {

enum class ApplicationState {
    Starting,
    Ready,
    StartFailed,
    ShuttingDown,
    Stopped
};

enum class VisionServiceState {
    Starting,
    Ready,
    Faulted,
    Recovering,
    Reconfiguring,
    Stopped
};

enum class ConfigurationApplyState {
    Idle,
    Validating,
    Reconfiguring,
    RestartingTcp,
    Persisting,
    RollingBack,
    Failed
};

enum class TaskState {
    Idle,
    Preparing,
    Running,
    AwaitingResultAck
};

enum class CalibrationMode {
    Center,
    FourCorners
};

enum class CalibrationProcessState {
    NoInput,
    ReadyToCapture,
    Capturing,
    ImageReady,
    Detecting,
    SelectingAnchors,
    ReadyToSolve,
    Solving,
    CandidateReady,
    Evaluating,
    ReadyToApply,
    Applying,
    Completed,
    Failed
};

struct CalibrationPointPresentation {
    std::size_t pointIndex{0U};
    QPointF imagePoint;
    QPointF physicalPoint;
    double residual{0.0};
    bool used{false};
};

struct CalibrationRoleSnapshot {
    vision::core::CameraRole role{vision::core::CameraRole::Unknown};
    QString pointFilePath;
    bool pointFileValid{false};
    int theoreticalPointCount{0};
    QImage rectifiedImage;
    QVector<QPointF> detectionPoints;
    std::optional<int> p1DetectionIndex;
    std::optional<int> p2DetectionIndex;
    QVector<CalibrationPointPresentation> points;
    std::array<double, 9> imageToPhysical{};
    bool hasSolution{false};
    int detectedPointCount{0};
    int usedPointCount{0};
    double rmsError{0.0};
    double maximumError{0.0};
    bool evaluated{false};
    bool passed{false};
};

struct CalibrationSnapshot {
    bool backendAvailable{false};
    bool runtimeAvailable{false};
    bool maintenanceLocked{false};
    CalibrationMode mode{CalibrationMode::Center};
    CalibrationProcessState state{CalibrationProcessState::NoInput};
    QString message;
    QVector<CalibrationRoleSnapshot> roles;
    std::optional<vision::core::Error> error;
};

struct TcpTaskContext {
    QString taskId;
    vision::tcp::ConnectionContext connection;
    vision::tcp::TcpLocateMode tcpMode{vision::tcp::TcpLocateMode::Edge};
    vision::core::LocateOptions options{vision::core::LocateMode::Edge};

    friend bool operator==(
        const TcpTaskContext& left,
        const TcpTaskContext& right) noexcept
    {
        return left.taskId == right.taskId &&
            left.connection == right.connection &&
            left.tcpMode == right.tcpMode &&
            left.options.mode == right.options.mode;
    }
};

using TaskContext = TcpTaskContext;

struct VisionTaskCommand {
    TaskContext context;
};

struct TaskImageSnapshot {
    vision::core::CameraRole role{vision::core::CameraRole::Unknown};
    QImage image;
};

struct VisionTaskCompletion {
    TaskContext context;
    vision::core::LocateOutcome outcome;
    vision::core::CoreStatus coreStatus;
    QVector<TaskImageSnapshot> images;
};

struct TcpRecoveryContext {
    quint32 sequence{0};
    vision::tcp::ConnectionContext connection;

    friend bool operator==(
        const TcpRecoveryContext&,
        const TcpRecoveryContext&) = default;
};

struct LocalRecoveryContext {
    quint64 sequence{0};

    friend bool operator==(
        const LocalRecoveryContext&,
        const LocalRecoveryContext&) = default;
};

using RecoveryContext =
    std::variant<TcpRecoveryContext, LocalRecoveryContext>;

struct RecoveryCommand {
    RecoveryContext context;
};

struct RecoveryCompletion {
    RecoveryContext context;
    vision::core::Status status;
    vision::core::CoreStatus coreStatus;
};

struct RuntimeReconfigurationCompletion {
    quint64 requestId{0};
    bool applied{false};
    bool rolledBack{false};
    vision::core::Status status;
    vision::core::CoreStatus coreStatus;
};

struct InitialRuntimeRollbackCompletion {
    quint64 requestId{0};
    vision::core::Status status;
};

struct AlgorithmValidationCompletion {
    quint64 requestId{0};
    vision::core::Status status;
};

struct ShutdownCompletion {
    vision::core::Status status;
};

struct AppDiagnostic {
    QDateTime occurredAt;
    vision::core::DiagnosticSeverity severity{
        vision::core::DiagnosticSeverity::Info};
    QString component;
    QString operation;
    QString message;
    QStringList frames;
};

struct TaskDisplayResult {
    QString taskLabel;
    vision::core::LocateMode mode{vision::core::LocateMode::Edge};
    QString outcome;
    bool hasPose{false};
    double x{0.0};
    double y{0.0};
    double r{0.0};
    std::optional<vision::core::Error> error;
};

struct TaskPresentationSnapshot {
    TaskDisplayResult result;
    QVector<TaskImageSnapshot> images;
    QDateTime completedAt;
};

struct ApplicationSnapshot {
    ApplicationState applicationState{ApplicationState::Starting};
    VisionServiceState visionState{VisionServiceState::Starting};
    TaskState taskState{TaskState::Idle};
    ConfigurationApplyState configurationApplyState{
        ConfigurationApplyState::Idle};
    quint64 activeConfigurationRevision{1};
    bool runtimeInitialized{false};
    bool calibrationMaintenanceLocked{false};
    bool tcpListening{false};
    std::optional<vision::tcp::ConnectionContext> tcpConnection;
    std::optional<QString> activeTaskId;
    std::optional<vision::core::Error> lastError;
};

} // namespace vision::app

Q_DECLARE_METATYPE(vision::core::Error)
Q_DECLARE_METATYPE(vision::core::CoreStatus)
Q_DECLARE_METATYPE(vision::app::TcpTaskContext)
Q_DECLARE_METATYPE(vision::app::TaskImageSnapshot)
Q_DECLARE_METATYPE(vision::app::VisionTaskCommand)
Q_DECLARE_METATYPE(vision::app::VisionTaskCompletion)
Q_DECLARE_METATYPE(vision::app::RecoveryCommand)
Q_DECLARE_METATYPE(vision::app::RecoveryCompletion)
Q_DECLARE_METATYPE(vision::app::RuntimeReconfigurationCompletion)
Q_DECLARE_METATYPE(vision::app::InitialRuntimeRollbackCompletion)
Q_DECLARE_METATYPE(vision::app::AlgorithmValidationCompletion)
Q_DECLARE_METATYPE(vision::app::ShutdownCompletion)
Q_DECLARE_METATYPE(vision::app::AppDiagnostic)
Q_DECLARE_METATYPE(vision::app::TaskDisplayResult)
Q_DECLARE_METATYPE(vision::app::TaskPresentationSnapshot)
Q_DECLARE_METATYPE(vision::app::ApplicationSnapshot)
Q_DECLARE_METATYPE(vision::app::CalibrationMode)
Q_DECLARE_METATYPE(vision::app::CalibrationProcessState)
Q_DECLARE_METATYPE(vision::app::CalibrationSnapshot)
