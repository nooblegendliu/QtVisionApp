#include "VisionWorker.h"

#include "CalibrationServices.h"
#include "DiagnosticBridge.h"

#include <VisionAlgorithm/Calibration.h>
#include <VisionAlgorithm/VisionAlgorithm.h>

#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <map>
#include <utility>
#include <variant>

namespace vision::app {
namespace {

using vision::core::CameraRole;

constexpr std::array<CameraRole, 5> calibrationRoleOrder{
    CameraRole::Center,
    CameraRole::TopLeft,
    CameraRole::TopRight,
    CameraRole::BottomRight,
    CameraRole::BottomLeft};

std::size_t calibrationRoleIndex(CameraRole role)
{
    for (std::size_t index = 0; index < calibrationRoleOrder.size(); ++index) {
        if (calibrationRoleOrder[index] == role) return index;
    }
    return calibrationRoleOrder.size();
}

bool roleBelongsToMode(CalibrationMode mode, CameraRole role)
{
    return mode == CalibrationMode::Center
        ? role == CameraRole::Center
        : role == CameraRole::TopLeft || role == CameraRole::TopRight ||
            role == CameraRole::BottomRight || role == CameraRole::BottomLeft;
}

std::vector<CameraRole> rolesForMode(CalibrationMode mode)
{
    if (mode == CalibrationMode::Center) return {CameraRole::Center};
    return {CameraRole::TopLeft, CameraRole::TopRight,
            CameraRole::BottomRight, CameraRole::BottomLeft};
}

QImage independentGrayImage(const cv::Mat& image)
{
    if (image.empty() || image.type() != CV_8UC1) return {};
    return QImage(
        image.data, image.cols, image.rows,
        static_cast<qsizetype>(image.step),
        QImage::Format_Grayscale8).copy();
}

vision::core::Error calibrationError(
    vision::core::ErrorCode code,
    const char* message,
    std::string diagnostic)
{
    return vision::core::Error{code, message, std::move(diagnostic)};
}

} // namespace

struct VisionWorker::CalibrationSession {
    struct RoleData {
        std::optional<CalibrationPointFile> pointFile;
        cv::Mat rawImage;
        std::optional<vision::algorithm::CalibrationDetection> detection;
        std::optional<int> p1Index;
        std::optional<int> p2Index;
        std::optional<vision::algorithm::CalibrationGridOrientation>
            orientation;
        std::optional<vision::algorithm::CalibrationSolution> solution;
        std::optional<vision::algorithm::CalibrationEvaluation> evaluation;

        void clearAfterFile()
        {
            rawImage.release();
            detection.reset();
            p1Index.reset();
            p2Index.reset();
            orientation.reset();
            solution.reset();
            evaluation.reset();
        }

        void clearAfterCapture()
        {
            detection.reset();
            p1Index.reset();
            p2Index.reset();
            orientation.reset();
            solution.reset();
            evaluation.reset();
        }
    };

    CalibrationMode mode{CalibrationMode::Center};
    CalibrationProcessState state{CalibrationProcessState::NoInput};
    bool maintenanceLocked{false};
    std::array<RoleData, 5> roles;
};

VisionWorker::VisionWorker(
    VisionRuntimeConfig runtimeConfig,
    CoreFactory coreFactory,
    QObject* parent)
    : QObject(parent),
      runtimeConfig_(std::move(runtimeConfig)),
      coreFactory_(std::move(coreFactory)),
      calibrationSession_(std::make_unique<CalibrationSession>())
{
}

VisionWorker::~VisionWorker() = default;

void VisionWorker::ensureDiagnosticBridge()
{
    if (diagnostics_ != nullptr) {
        return;
    }
    diagnostics_ = new DiagnosticBridge(this);
    connect(
        diagnostics_, &DiagnosticBridge::diagnosticReady,
        this, &VisionWorker::diagnosticReceived,
        Qt::DirectConnection);
}

void VisionWorker::initialize()
{
    if (shuttingDown_) {
        emit initializationFailed(makeWorkerError(
            vision::core::ErrorCode::NotReady,
            "The application is shutting down.",
            "VisionWorker::initialize was invoked during shutdown."));
        return;
    }
    if (core_) {
        emit initialized(core_->status());
        return;
    }
    if (!coreFactory_) {
        emit initializationFailed(makeWorkerError(
            vision::core::ErrorCode::InvalidConfiguration,
            "The vision runtime factory is not configured.",
            "VisionWorker received an empty CoreFactory."));
        return;
    }

    ensureDiagnosticBridge();
    try {
        CoreFactoryOutcome outcome = coreFactory_(
            runtimeConfig_, this, diagnostics_, this);
        if (const auto* error =
                std::get_if<vision::core::Error>(&outcome)) {
            emit initializationFailed(*error);
            return;
        }
        core_ = std::move(
            std::get<std::unique_ptr<vision::core::VisionCore>>(outcome));
        if (!core_) {
            emit initializationFailed(makeWorkerError(
                vision::core::ErrorCode::InternalFailure,
                "The vision runtime could not be created.",
                "CoreFactory returned a null VisionCore."));
            return;
        }
        const auto status = core_->status();
        if (status.state != vision::core::CoreState::Ready) {
            const auto error = status.lastError.value_or(makeWorkerError(
                vision::core::ErrorCode::InternalFailure,
                "The vision runtime did not enter Ready.",
                "VisionCore was returned in an unexpected state."));
            static_cast<void>(core_->shutdown());
            core_.reset();
            emit initializationFailed(error);
            return;
        }
        initializeCalibrationBackend();
        emit initialized(status);
    }
    catch (const std::exception& exception) {
        if (core_) {
            try {
                static_cast<void>(core_->shutdown());
            }
            catch (...) {
            }
            core_.reset();
        }
        emit initializationFailed(makeWorkerError(
            vision::core::ErrorCode::InternalFailure,
            "The vision runtime could not be created.",
            exception.what()));
    }
    catch (...) {
        if (core_) {
            try {
                static_cast<void>(core_->shutdown());
            }
            catch (...) {
            }
            core_.reset();
        }
        emit initializationFailed(makeWorkerError(
            vision::core::ErrorCode::InternalFailure,
            "The vision runtime could not be created.",
            "VisionWorker caught an unknown exception during initialization."));
    }
}

void VisionWorker::locate(const VisionTaskCommand& command)
{
    if (!core_ || shuttingDown_) {
        VisionTaskCompletion completion{
            command.context,
            makeWorkerError(
                vision::core::ErrorCode::NotReady,
                "The vision service is not ready.",
                "VisionWorker rejected locate without an available core."),
            currentStatus()};
        completion.images.clear();
        emit taskCompleted(completion);
        return;
    }
    if (activeTask_.has_value() ||
        (calibrationSession_ && calibrationSession_->maintenanceLocked)) {
        VisionTaskCompletion completion{
            command.context,
            makeWorkerError(
                vision::core::ErrorCode::Busy,
                "A locate task is already active.",
                "VisionWorker defensive busy check rejected a second task."),
            core_->status()};
        completion.images.clear();
        emit taskCompleted(completion);
        return;
    }

    activeTask_ = command.context;
    activeImages_.clear();
    const auto options = command.context.options;
    vision::core::LocateOutcome outcome = makeWorkerError(
        vision::core::ErrorCode::InternalFailure,
        "The vision task failed internally.",
        "VisionWorker caught an unknown locate exception.");
    try {
        outcome = core_->locate(options);
    }
    catch (const std::exception& exception) {
        outcome = makeWorkerError(
            vision::core::ErrorCode::InternalFailure,
            "The vision task failed internally.",
            exception.what());
    }
    catch (...) {
    }
    const auto status = core_->status();
    const TaskContext completedContext = *activeTask_;
    activeTask_.reset();
    emit taskCompleted(VisionTaskCompletion{
        completedContext, std::move(outcome), status,
        std::move(activeImages_)});
    activeImages_.clear();
}

void VisionWorker::recover(const RecoveryCommand& command)
{
    if (!core_ || shuttingDown_) {
        emit recoveryCompleted(RecoveryCompletion{
            command.context,
            makeWorkerError(
                vision::core::ErrorCode::NotReady,
                "The vision service is not ready.",
                "VisionWorker rejected recover without an available core."),
            currentStatus()});
        return;
    }
    if (calibrationSession_ && calibrationSession_->maintenanceLocked) {
        emit recoveryCompleted(RecoveryCompletion{
            command.context,
            makeWorkerError(
                vision::core::ErrorCode::Busy,
                "Calibration maintenance is active.",
                "VisionWorker rejected recover during calibration maintenance."),
            currentStatus()});
        return;
    }
    vision::core::Status status = makeWorkerError(
        vision::core::ErrorCode::InternalFailure,
        "Vision recovery failed internally.",
        "VisionWorker caught an unknown recovery exception.");
    try {
        status = core_->recover();
    }
    catch (const std::exception& exception) {
        status = makeWorkerError(
            vision::core::ErrorCode::InternalFailure,
            "Vision recovery failed internally.",
            exception.what());
    }
    catch (...) {
    }
    emit recoveryCompleted(RecoveryCompletion{
        command.context, status, core_->status()});
}

void VisionWorker::reconfigureRuntime(
    quint64 requestId,
    const VisionRuntimeConfig& candidate)
{
    auto reject = [this, requestId](vision::core::Error error) {
        emit runtimeReconfigurationCompleted(
            RuntimeReconfigurationCompletion{
                requestId, false, false, std::move(error), currentStatus()});
    };

    if (shuttingDown_) {
        reject(makeWorkerError(
            vision::core::ErrorCode::NotReady,
            "The vision service is not ready for reconfiguration.",
            "VisionWorker is shutting down."));
        return;
    }
    if (activeTask_.has_value() ||
        (calibrationSession_ && calibrationSession_->maintenanceLocked)) {
        reject(makeWorkerError(
            vision::core::ErrorCode::Busy,
            "A locate task is already active.",
            "VisionWorker rejected runtime reconfiguration while locating."));
        return;
    }

    ensureDiagnosticBridge();
    const auto create = [this](const VisionRuntimeConfig& config)
        -> CoreFactoryOutcome {
        try {
            CoreFactoryOutcome outcome =
                coreFactory_(config, this, diagnostics_, this);
            auto* created = std::get_if<
                std::unique_ptr<vision::core::VisionCore>>(&outcome);
            if (created == nullptr) {
                return outcome;
            }
            if (!*created) {
                return makeWorkerError(
                    vision::core::ErrorCode::InternalFailure,
                    "The vision runtime could not be created.",
                    "The runtime factory returned a null core.");
            }
            const auto status = (*created)->status();
            if (status.state != vision::core::CoreState::Ready) {
                const auto error = status.lastError.value_or(makeWorkerError(
                    vision::core::ErrorCode::InternalFailure,
                    "The vision runtime did not enter Ready.",
                    "The runtime factory returned a non-ready core."));
                static_cast<void>((*created)->shutdown());
                created->reset();
                return error;
            }
            return outcome;
        }
        catch (const std::exception& exception) {
            return makeWorkerError(
                vision::core::ErrorCode::InternalFailure,
                "The vision runtime could not be created.",
                exception.what());
        }
        catch (...) {
            return makeWorkerError(
                vision::core::ErrorCode::InternalFailure,
                "The vision runtime could not be created.",
                "The runtime factory threw an unknown exception.");
        }
    };

    if (!core_) {
        CoreFactoryOutcome initialOutcome = create(candidate);
        if (auto* initialCore = std::get_if<
                std::unique_ptr<vision::core::VisionCore>>(&initialOutcome);
            initialCore != nullptr && *initialCore) {
            core_ = std::move(*initialCore);
            runtimeConfig_ = candidate;
            initializeCalibrationBackend();
            emit runtimeReconfigurationCompleted(
                RuntimeReconfigurationCompletion{
                    requestId, true, false, std::monostate{}, core_->status()});
            return;
        }
        const auto* error =
            std::get_if<vision::core::Error>(&initialOutcome);
        reject(error != nullptr ? *error : makeWorkerError(
            vision::core::ErrorCode::InternalFailure,
            "The vision runtime could not be created.",
            "The runtime factory returned an invalid outcome."));
        return;
    }

    const VisionRuntimeConfig previous = runtimeConfig_;
    vision::core::Status shutdownStatus = core_->shutdown();
    core_.reset();
    if (const auto* error =
            std::get_if<vision::core::Error>(&shutdownStatus)) {
        reject(*error);
        return;
    }

    CoreFactoryOutcome candidateOutcome = create(candidate);
    if (auto* candidateCore = std::get_if<
            std::unique_ptr<vision::core::VisionCore>>(&candidateOutcome);
        candidateCore != nullptr && *candidateCore) {
        core_ = std::move(*candidateCore);
        runtimeConfig_ = candidate;
        initializeCalibrationBackend();
        emit runtimeReconfigurationCompleted(
            RuntimeReconfigurationCompletion{
                requestId, true, false, std::monostate{}, core_->status()});
        return;
    }

    vision::core::Error candidateError =
        std::holds_alternative<vision::core::Error>(candidateOutcome)
        ? std::get<vision::core::Error>(std::move(candidateOutcome))
        : makeWorkerError(
            vision::core::ErrorCode::InternalFailure,
            "The candidate vision runtime could not be created.",
            "The runtime factory returned a null core.");

    CoreFactoryOutcome rollbackOutcome = create(previous);
    if (auto* rollbackCore = std::get_if<
            std::unique_ptr<vision::core::VisionCore>>(&rollbackOutcome);
        rollbackCore != nullptr && *rollbackCore) {
        core_ = std::move(*rollbackCore);
        runtimeConfig_ = previous;
        initializeCalibrationBackend();
        emit runtimeReconfigurationCompleted(
            RuntimeReconfigurationCompletion{
                requestId, false, true, std::move(candidateError),
                core_->status()});
        return;
    }

    std::string diagnostic = candidateError.diagnostic;
    diagnostic += "; rollback failed: ";
    if (const auto* rollbackError =
            std::get_if<vision::core::Error>(&rollbackOutcome)) {
        diagnostic += rollbackError->diagnostic;
    }
    else {
        diagnostic += "runtime factory returned a null core";
    }
    const auto error = makeWorkerError(
        vision::core::ErrorCode::InternalFailure,
        "The new vision configuration failed and the previous runtime could not be restored.",
        diagnostic.c_str());
    emit runtimeReconfigurationCompleted(
        RuntimeReconfigurationCompletion{
            requestId, false, false, error, currentStatus()});
}

void VisionWorker::rollbackInitialRuntime(
    quint64 requestId,
    const VisionRuntimeConfig& previousConfiguration)
{
    vision::core::Status status{std::monostate{}};
    if (shuttingDown_) {
        status = makeWorkerError(
            vision::core::ErrorCode::NotReady,
            "The candidate vision runtime could not be rolled back.",
            "VisionWorker is shutting down.");
    }
    else if (activeTask_.has_value()) {
        status = makeWorkerError(
            vision::core::ErrorCode::Busy,
            "The candidate vision runtime could not be rolled back.",
            "VisionWorker received an initial runtime rollback while locating.");
    }
    else if (!core_) {
        status = makeWorkerError(
            vision::core::ErrorCode::InternalFailure,
            "The candidate vision runtime could not be rolled back.",
            "VisionWorker had no candidate runtime to discard.");
        runtimeConfig_ = previousConfiguration;
    }
    else {
        try {
            status = core_->shutdown();
        }
        catch (const std::exception& exception) {
            status = makeWorkerError(
                vision::core::ErrorCode::InternalFailure,
                "The candidate vision runtime could not be shut down.",
                exception.what());
        }
        catch (...) {
            status = makeWorkerError(
                vision::core::ErrorCode::InternalFailure,
                "The candidate vision runtime could not be shut down.",
                "VisionWorker caught an unknown rollback exception.");
        }
        core_.reset();
        runtimeConfig_ = previousConfiguration;
    }

    emit initialRuntimeRollbackCompleted(
        InitialRuntimeRollbackCompletion{requestId, std::move(status)});
}

void VisionWorker::shutdown()
{
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;
    calibrationAlgorithm_.reset();
    calibrationSession_.reset();
    vision::core::Status status{std::monostate{}};
    if (core_) {
        try {
            status = core_->shutdown();
        }
        catch (const std::exception& exception) {
            status = makeWorkerError(
                vision::core::ErrorCode::InternalFailure,
                "Vision shutdown failed internally.",
                exception.what());
        }
        catch (...) {
            status = makeWorkerError(
                vision::core::ErrorCode::InternalFailure,
                "Vision shutdown failed internally.",
                "VisionWorker caught an unknown shutdown exception.");
        }
        core_.reset();
    }
    emit shutdownCompleted(ShutdownCompletion{std::move(status)});
}

void VisionWorker::validateAlgorithmConfiguration(
    quint64 requestId,
    const QString& path)
{
#ifdef _WIN32
    const std::filesystem::path native(path.toStdWString());
#else
    const std::filesystem::path native(path.toStdString());
#endif
    emit algorithmValidationCompleted(AlgorithmValidationCompletion{
        requestId,
        vision::algorithm::VisionAlgorithm::validateConfiguration(native)});
}

void VisionWorker::initializeCalibrationBackend()
{
    calibrationSession_ = std::make_unique<CalibrationSession>();
    calibrationAlgorithm_.reset();
    auto outcome = vision::algorithm::CalibrationAlgorithm::create(
        runtimeConfig_.algorithmConfigPath);
    if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
        calibrationAlgorithm_.reset();
        publishCalibration(
            CalibrationProcessState::Failed,
            QStringLiteral("标定后端资源不可用"), *error);
        return;
    }
    calibrationAlgorithm_ = std::move(std::get<
        std::unique_ptr<vision::algorithm::CalibrationAlgorithm>>(outcome));
    if (!calibrationAlgorithm_) {
        publishCalibration(
            CalibrationProcessState::Failed,
            QStringLiteral("标定后端创建失败"),
            calibrationError(
                vision::core::ErrorCode::InternalFailure,
                "The calibration backend could not be created.",
                "CalibrationAlgorithm::create returned null."));
        return;
    }
    publishCalibration(
        CalibrationProcessState::NoInput,
        QStringLiteral("请选择各相机理论点文件"));
}

void VisionWorker::publishCalibration(
    CalibrationProcessState state,
    QString message,
    std::optional<vision::core::Error> error)
{
    if (!calibrationSession_) {
        calibrationSession_ = std::make_unique<CalibrationSession>();
    }
    calibrationSession_->state = state;
    CalibrationSnapshot snapshot;
    snapshot.backendAvailable = calibrationAlgorithm_ != nullptr && core_ != nullptr;
    snapshot.runtimeAvailable = core_ != nullptr;
    snapshot.maintenanceLocked = calibrationSession_->maintenanceLocked;
    snapshot.mode = calibrationSession_->mode;
    snapshot.state = state;
    snapshot.message = std::move(message);
    snapshot.error = std::move(error);

    for (CameraRole role : rolesForMode(calibrationSession_->mode)) {
        const std::size_t index = calibrationRoleIndex(role);
        if (index >= calibrationSession_->roles.size()) continue;
        const auto& data = calibrationSession_->roles[index];
        CalibrationRoleSnapshot roleSnapshot;
        roleSnapshot.role = role;
        if (data.pointFile) {
            roleSnapshot.pointFilePath = QString::fromStdWString(
                data.pointFile->path.wstring());
            roleSnapshot.pointFileValid = true;
            roleSnapshot.theoreticalPointCount = static_cast<int>(
                data.pointFile->points.size());
        }
        if (data.detection) {
            roleSnapshot.rectifiedImage = independentGrayImage(
                data.detection->rectifiedImage);
            roleSnapshot.detectionPoints.reserve(static_cast<qsizetype>(
                data.detection->imagePoints.size()));
            for (const auto& point : data.detection->imagePoints) {
                roleSnapshot.detectionPoints.push_back(
                    QPointF(point.x, point.y));
            }
        } else if (!data.rawImage.empty()) {
            roleSnapshot.rectifiedImage = independentGrayImage(data.rawImage);
        }
        roleSnapshot.p1DetectionIndex = data.p1Index;
        roleSnapshot.p2DetectionIndex = data.p2Index;
        if (data.solution) {
            roleSnapshot.hasSolution = true;
            std::copy(std::begin(data.solution->imageToPhysical.val),
                      std::end(data.solution->imageToPhysical.val),
                      roleSnapshot.imageToPhysical.begin());
            roleSnapshot.points.reserve(static_cast<qsizetype>(
                data.solution->points.size()));
            for (const auto& point : data.solution->points) {
                roleSnapshot.points.push_back(CalibrationPointPresentation{
                    point.pointIndex,
                    QPointF(point.imagePoint.x, point.imagePoint.y),
                    QPointF(point.physicalPoint.x, point.physicalPoint.y),
                    point.residual,
                    point.used});
            }
        }
        if (data.evaluation) {
            roleSnapshot.evaluated = true;
            roleSnapshot.detectedPointCount = static_cast<int>(
                data.evaluation->detectedPointCount);
            roleSnapshot.usedPointCount = static_cast<int>(
                data.evaluation->usedPointCount);
            roleSnapshot.rmsError = data.evaluation->rmsError;
            roleSnapshot.maximumError = data.evaluation->maximumError;
            roleSnapshot.passed = data.evaluation->passed;
        }
        snapshot.roles.push_back(std::move(roleSnapshot));
    }
    emit calibrationUpdated(snapshot);
}

void VisionWorker::setCalibrationPointFile(
    CalibrationMode mode, CameraRole role, const QString& path)
{
    if (shuttingDown_ || activeTask_.has_value() ||
        (calibrationSession_ && calibrationSession_->maintenanceLocked)) {
        publishCalibration(
            calibrationSession_ ? calibrationSession_->state
                                : CalibrationProcessState::NoInput,
            QStringLiteral("当前视觉任务不允许修改标定输入"),
            calibrationError(
                vision::core::ErrorCode::Busy,
                "The calibration point file cannot be changed now.",
                "A locate or calibration maintenance session is active."));
        return;
    }
    if (!roleBelongsToMode(mode, role)) {
        publishCalibration(
            CalibrationProcessState::Failed,
            QStringLiteral("理论点文件与相机角色不匹配"),
            calibrationError(
                vision::core::ErrorCode::InvalidConfiguration,
                "The calibration point file role is invalid.",
                "The selected role does not belong to the requested calibration mode."));
        return;
    }
#ifdef _WIN32
    const std::filesystem::path native(path.toStdWString());
#else
    const std::filesystem::path native(path.toStdString());
#endif
    calibrationSession_->mode = mode;
    auto outcome = loadCalibrationPointFile(native);
    if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
        calibrationSession_->mode = mode;
        publishCalibration(
            calibrationSession_->state,
            QStringLiteral("理论点文件校验失败"), *error);
        return;
    }
    for (CameraRole affected : rolesForMode(mode)) {
        calibrationSession_->roles[calibrationRoleIndex(affected)]
            .clearAfterFile();
    }
    calibrationSession_->roles[calibrationRoleIndex(role)].pointFile =
        std::move(std::get<CalibrationPointFile>(outcome));

    bool complete = true;
    std::optional<std::size_t> count;
    for (CameraRole required : rolesForMode(mode)) {
        const auto& file = calibrationSession_->roles[
            calibrationRoleIndex(required)].pointFile;
        if (!file) {
            complete = false;
            continue;
        }
        if (!count) count = file->points.size();
        else if (*count != file->points.size()) complete = false;
    }
    publishCalibration(
        complete ? CalibrationProcessState::ReadyToCapture
                 : CalibrationProcessState::NoInput,
        complete
            ? QStringLiteral("理论点文件校验通过，可以采集")
            : QStringLiteral("请继续选择其余相机理论点文件"));
}

void VisionWorker::captureCalibration(CalibrationMode mode)
{
    if (!core_ || !calibrationAlgorithm_ || shuttingDown_ ||
        activeTask_.has_value()) {
        publishCalibration(
            calibrationSession_->state,
            QStringLiteral("当前状态不能开始标定采集"),
            calibrationError(
                vision::core::ErrorCode::Busy,
                "Calibration capture is not available.",
                "The runtime is unavailable or another operation is active."));
        return;
    }
    if (calibrationSession_->maintenanceLocked &&
        calibrationSession_->mode != mode) {
        publishCalibration(
            calibrationSession_->state,
            QStringLiteral("请先重置当前标定会话再切换模式"),
            calibrationError(
                vision::core::ErrorCode::Busy,
                "Another calibration mode is active.",
                "Calibration maintenance belongs to the other mode."));
        return;
    }
    calibrationSession_->mode = mode;
    std::optional<std::size_t> expectedCount;
    for (CameraRole role : rolesForMode(mode)) {
        const auto& file = calibrationSession_->roles[
            calibrationRoleIndex(role)].pointFile;
        if (!file || (expectedCount &&
                      file->points.size() != *expectedCount)) {
            publishCalibration(
                CalibrationProcessState::NoInput,
                QStringLiteral("理论点文件未完整准备或点数不一致"),
                calibrationError(
                    vision::core::ErrorCode::InvalidConfiguration,
                    "Calibration point files are incomplete.",
                    "Every required role must have the same valid point count."));
            return;
        }
        expectedCount = file->points.size();
    }

    calibrationSession_->maintenanceLocked = true;
    publishCalibration(
        CalibrationProcessState::Capturing,
        QStringLiteral("正在采集标定图像"));
    const auto captureMode = mode == CalibrationMode::Center
        ? vision::core::LocateMode::Edge
        : vision::core::LocateMode::MarkPoint;
    auto outcome = core_->captureForCalibration(captureMode);
    if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
        publishCalibration(
            CalibrationProcessState::Failed,
            QStringLiteral("标定图像采集失败"), *error);
        return;
    }
    for (CameraRole role : rolesForMode(mode)) {
        auto& data = calibrationSession_->roles[calibrationRoleIndex(role)];
        data.rawImage.release();
        data.clearAfterCapture();
    }
    for (const auto& frame :
         std::get<vision::core::ImageFrames>(outcome).frames) {
        const std::size_t index = calibrationRoleIndex(frame.cameraRole);
        if (index < calibrationSession_->roles.size()) {
            calibrationSession_->roles[index].rawImage = frame.image.clone();
        }
    }
    publishCalibration(
        CalibrationProcessState::ImageReady,
        QStringLiteral("标定图像采集完成"));
}

void VisionWorker::detectCalibration()
{
    if (!calibrationAlgorithm_ || !calibrationSession_->maintenanceLocked) {
        publishCalibration(
            calibrationSession_->state,
            QStringLiteral("当前状态不能执行圆点检测"),
            calibrationError(
                vision::core::ErrorCode::NotReady,
                "Calibration detection is not ready.",
                "No active calibration maintenance session."));
        return;
    }
    publishCalibration(
        CalibrationProcessState::Detecting,
        QStringLiteral("正在检测标定圆点"));
    for (CameraRole role : rolesForMode(calibrationSession_->mode)) {
        auto& data = calibrationSession_->roles[calibrationRoleIndex(role)];
        auto outcome = calibrationAlgorithm_->detect(role, data.rawImage);
        if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
            publishCalibration(
                CalibrationProcessState::Failed,
                QStringLiteral("圆点检测失败"), *error);
            return;
        }
        data.detection = std::move(
            std::get<vision::algorithm::CalibrationDetection>(outcome));
        data.p1Index.reset();
        data.p2Index.reset();
        data.orientation.reset();
        data.solution.reset();
        data.evaluation.reset();
        if (!data.detection->usableForSolving) {
            publishCalibration(
                CalibrationProcessState::ImageReady,
                QStringLiteral("未检测到完整圆点阵列，请重新采集或检测"));
            return;
        }
        if (!data.pointFile || data.pointFile->points.size() !=
                data.detection->imagePoints.size()) {
            publishCalibration(
                CalibrationProcessState::Failed,
                QStringLiteral("理论点数与检测点数不一致"),
                calibrationError(
                    vision::core::ErrorCode::InvalidConfiguration,
                    "The theoretical and detected point counts differ.",
                    "The selected point file does not match grid_side_points."));
            return;
        }
    }
    publishCalibration(
        CalibrationProcessState::SelectingAnchors,
        QStringLiteral("请在每幅图像中依次点击P1和P2"));
}

void VisionWorker::selectCalibrationPoint(
    CameraRole role, int detectionIndex)
{
    if (!calibrationSession_ || !calibrationSession_->maintenanceLocked ||
        !roleBelongsToMode(calibrationSession_->mode, role) ||
        detectionIndex < 0) {
        publishCalibration(
            calibrationSession_ ? calibrationSession_->state
                                : CalibrationProcessState::Failed,
            QStringLiteral("无效的标定点选择"),
            calibrationError(
                vision::core::ErrorCode::InvalidResult,
                "The calibration point selection is invalid.",
                "Role, session, or detection index is invalid."));
        return;
    }
    auto& data = calibrationSession_->roles[calibrationRoleIndex(role)];
    if (!data.detection || !data.detection->usableForSolving) {
        publishCalibration(
            calibrationSession_->state,
            QStringLiteral("当前图像没有可选择的完整圆点阵列"));
        return;
    }
    const int count = static_cast<int>(data.detection->imagePoints.size());
    const int side = static_cast<int>(std::llround(
        std::sqrt(static_cast<double>(count))));
    if (side * side != count || detectionIndex >= count) {
        publishCalibration(
            calibrationSession_->state,
            QStringLiteral("圆点索引超出检测结果范围"),
            calibrationError(
                vision::core::ErrorCode::InvalidResult,
                "The calibration point selection is invalid.",
                "Detection index is outside the detected grid."));
        return;
    }

    const std::size_t selected = static_cast<std::size_t>(detectionIndex);
    const std::size_t n = static_cast<std::size_t>(side);
    const bool corner = selected == 0U || selected == n - 1U ||
        selected == (n - 1U) * n || selected == n * n - 1U;
    if (!data.p1Index || data.orientation) {
        if (!corner) {
            publishCalibration(
                CalibrationProcessState::SelectingAnchors,
                QStringLiteral("P1必须选择阵列角点"));
            return;
        }
        data.p1Index = detectionIndex;
        data.p2Index.reset();
        data.orientation.reset();
    } else {
        auto outcome = inferCalibrationOrientation(
            side, static_cast<std::size_t>(*data.p1Index), selected);
        if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
            publishCalibration(
                CalibrationProcessState::SelectingAnchors,
                QStringLiteral("P2必须是P1沿网格方向的直接相邻点"),
                *error);
            return;
        }
        data.p2Index = detectionIndex;
        data.orientation = std::get<
            vision::algorithm::CalibrationGridOrientation>(outcome);
    }

    bool complete = true;
    for (CameraRole required : rolesForMode(calibrationSession_->mode)) {
        if (!calibrationSession_->roles[
                calibrationRoleIndex(required)].orientation) {
            complete = false;
            break;
        }
    }
    publishCalibration(
        complete ? CalibrationProcessState::ReadyToSolve
                 : CalibrationProcessState::SelectingAnchors,
        complete
            ? QStringLiteral("P1/P2对应关系已建立，可以计算矩阵")
            : QStringLiteral("请继续完成其余图像的P1/P2选择"));
}

void VisionWorker::solveCalibration()
{
    if (!calibrationAlgorithm_ || !calibrationSession_->maintenanceLocked) {
        return;
    }
    publishCalibration(
        CalibrationProcessState::Solving,
        QStringLiteral("正在计算候选标定矩阵"));
    for (CameraRole role : rolesForMode(calibrationSession_->mode)) {
        auto& data = calibrationSession_->roles[calibrationRoleIndex(role)];
        if (!data.detection || !data.pointFile || !data.orientation) {
            publishCalibration(
                CalibrationProcessState::Failed,
                QStringLiteral("标定对应关系不完整"),
                calibrationError(
                    vision::core::ErrorCode::InvalidConfiguration,
                    "Calibration correspondence is incomplete.",
                    "Detection, point file, or P1/P2 orientation is missing."));
            return;
        }
        auto outcome = calibrationAlgorithm_->solve(
            *data.detection, data.pointFile->points, *data.orientation);
        if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
            publishCalibration(
                CalibrationProcessState::Failed,
                QStringLiteral("候选标定矩阵计算失败"), *error);
            return;
        }
        data.solution = std::move(
            std::get<vision::algorithm::CalibrationSolution>(outcome));
        data.evaluation.reset();
    }
    publishCalibration(
        CalibrationProcessState::CandidateReady,
        QStringLiteral("候选标定矩阵已生成"));
}

void VisionWorker::evaluateCalibration()
{
    if (!calibrationAlgorithm_ || !calibrationSession_->maintenanceLocked) {
        return;
    }
    publishCalibration(
        CalibrationProcessState::Evaluating,
        QStringLiteral("正在评估候选标定质量"));
    bool passed = true;
    for (CameraRole role : rolesForMode(calibrationSession_->mode)) {
        auto& data = calibrationSession_->roles[calibrationRoleIndex(role)];
        if (!data.solution) {
            publishCalibration(
                CalibrationProcessState::Failed,
                QStringLiteral("没有可评估的候选矩阵"),
                calibrationError(
                    vision::core::ErrorCode::InvalidResult,
                    "Calibration solution is missing.",
                    "evaluateCalibration requires every role solution."));
            return;
        }
        auto outcome = calibrationAlgorithm_->evaluate(*data.solution);
        if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
            publishCalibration(
                CalibrationProcessState::Failed,
                QStringLiteral("候选标定质量评估失败"), *error);
            return;
        }
        data.evaluation = std::get<
            vision::algorithm::CalibrationEvaluation>(outcome);
        passed = passed && data.evaluation->passed;
    }
    publishCalibration(
        passed ? CalibrationProcessState::ReadyToApply
               : CalibrationProcessState::CandidateReady,
        passed
            ? QStringLiteral("所有候选标定质量通过，可以应用")
            : QStringLiteral("候选标定质量未通过，不能应用"));
}

void VisionWorker::applyCalibration()
{
    if (!core_ || !calibrationAlgorithm_ ||
        !calibrationSession_->maintenanceLocked) return;
    CalibrationFileTransaction::MatrixMap matrices;
    for (CameraRole role : rolesForMode(calibrationSession_->mode)) {
        const auto& data = calibrationSession_->roles[
            calibrationRoleIndex(role)];
        if (!data.solution || !data.evaluation || !data.evaluation->passed) {
            publishCalibration(
                CalibrationProcessState::CandidateReady,
                QStringLiteral("只有全部质量通过的候选标定才能应用"));
            return;
        }
        matrices.emplace(role, data.solution->imageToPhysical);
    }
    publishCalibration(
        CalibrationProcessState::Applying,
        QStringLiteral("正在提交标定文件并重新加载视觉运行时"));

    auto transactionOutcome = CalibrationFileTransaction::stageAndReplace(
        runtimeConfig_.algorithmConfigPath, matrices);
    if (const auto* error =
            std::get_if<vision::core::Error>(&transactionOutcome)) {
        publishCalibration(
            CalibrationProcessState::Failed,
            QStringLiteral("标定文件提交失败"), *error);
        return;
    }
    auto transaction = std::move(std::get<CalibrationFileTransaction>(
        transactionOutcome));
    auto candidateCalibration =
        vision::algorithm::CalibrationAlgorithm::create(
            runtimeConfig_.algorithmConfigPath);
    if (const auto* error =
            std::get_if<vision::core::Error>(&candidateCalibration)) {
        const auto rollbackStatus = transaction.rollback();
        if (std::holds_alternative<std::monostate>(rollbackStatus)) {
            publishCalibration(
                CalibrationProcessState::Failed,
                QStringLiteral("候选标定资源验证失败，已恢复旧标定"),
                *error);
            return;
        }
        const auto* rollbackError =
            std::get_if<vision::core::Error>(&rollbackStatus);
        try {
            static_cast<void>(core_->shutdown());
        } catch (...) {
        }
        core_.reset();
        calibrationAlgorithm_.reset();
        std::string diagnostic = error->diagnostic;
        if (rollbackError) {
            diagnostic += "; rollback failed: " + rollbackError->diagnostic;
        }
        publishCalibration(
            CalibrationProcessState::Failed,
            QStringLiteral("候选标定验证及回滚失败，视觉服务不可用"),
            calibrationError(
                vision::core::ErrorCode::InternalFailure,
                "The calibration update failed and the previous files could not be restored.",
                std::move(diagnostic)));
        return;
    }

    auto createCore = [this]() -> CoreFactoryOutcome {
        try {
            return coreFactory_(runtimeConfig_, this, diagnostics_, this);
        } catch (const std::exception& exception) {
            return calibrationError(
                vision::core::ErrorCode::InternalFailure,
                "The vision runtime could not be reloaded.",
                exception.what());
        } catch (...) {
            return calibrationError(
                vision::core::ErrorCode::InternalFailure,
                "The vision runtime could not be reloaded.",
                "Unknown runtime factory exception.");
        }
    };

    vision::core::Status shutdownStatus = core_->shutdown();
    core_.reset();
    if (const auto* error =
            std::get_if<vision::core::Error>(&shutdownStatus)) {
        const auto rollbackStatus = transaction.rollback();
        auto restoredCalibration =
            vision::algorithm::CalibrationAlgorithm::create(
                runtimeConfig_.algorithmConfigPath);
        CoreFactoryOutcome restored = createCore();
        auto* restoredCore = std::get_if<
            std::unique_ptr<vision::core::VisionCore>>(&restored);
        if (std::holds_alternative<std::monostate>(rollbackStatus) &&
            std::holds_alternative<std::unique_ptr<
                vision::algorithm::CalibrationAlgorithm>>(
                    restoredCalibration) &&
            restoredCore && *restoredCore &&
            (*restoredCore)->status().state ==
                vision::core::CoreState::Ready) {
            core_ = std::move(*restoredCore);
            calibrationAlgorithm_ = std::move(std::get<std::unique_ptr<
                vision::algorithm::CalibrationAlgorithm>>(
                    restoredCalibration));
            publishCalibration(
                CalibrationProcessState::Failed,
                QStringLiteral("旧视觉运行时关闭失败，标定已回滚"), *error);
            return;
        }

        calibrationAlgorithm_.reset();
        std::string diagnostic = error->diagnostic;
        diagnostic += "; rollback after shutdown failure did not restore a ready runtime";
        publishCalibration(
            CalibrationProcessState::Failed,
            QStringLiteral("旧运行时关闭失败且恢复失败，视觉服务不可用"),
            calibrationError(
                vision::core::ErrorCode::InternalFailure,
                "The calibration update failed and the previous runtime could not be restored.",
                std::move(diagnostic)));
        return;
    }

    CoreFactoryOutcome candidateCoreOutcome = createCore();
    auto* candidateCore = std::get_if<
        std::unique_ptr<vision::core::VisionCore>>(&candidateCoreOutcome);
    if (candidateCore && *candidateCore &&
        (*candidateCore)->status().state == vision::core::CoreState::Ready) {
        core_ = std::move(*candidateCore);
        calibrationAlgorithm_ = std::move(std::get<
            std::unique_ptr<vision::algorithm::CalibrationAlgorithm>>(
                candidateCalibration));
        static_cast<void>(transaction.commit());
        calibrationSession_->maintenanceLocked = false;
        publishCalibration(
            CalibrationProcessState::Completed,
            QStringLiteral("标定已应用并重新加载"));
        return;
    }

    vision::core::Error candidateError =
        std::holds_alternative<vision::core::Error>(candidateCoreOutcome)
        ? std::get<vision::core::Error>(candidateCoreOutcome)
        : calibrationError(
            vision::core::ErrorCode::InternalFailure,
            "The updated vision runtime could not be created.",
            "CoreFactory returned a null or non-ready core.");
    const auto rollbackStatus = transaction.rollback();
    auto restoredCalibration = vision::algorithm::CalibrationAlgorithm::create(
        runtimeConfig_.algorithmConfigPath);
    CoreFactoryOutcome restoredCoreOutcome = createCore();
    auto* restoredCore = std::get_if<
        std::unique_ptr<vision::core::VisionCore>>(&restoredCoreOutcome);
    if (std::holds_alternative<std::monostate>(rollbackStatus) &&
        std::holds_alternative<
            std::unique_ptr<vision::algorithm::CalibrationAlgorithm>>(
                restoredCalibration) &&
        restoredCore && *restoredCore &&
        (*restoredCore)->status().state == vision::core::CoreState::Ready) {
        core_ = std::move(*restoredCore);
        calibrationAlgorithm_ = std::move(std::get<
            std::unique_ptr<vision::algorithm::CalibrationAlgorithm>>(
                restoredCalibration));
        publishCalibration(
            CalibrationProcessState::Failed,
            QStringLiteral("新标定运行时创建失败，已恢复旧标定"),
            candidateError);
        return;
    }

    calibrationAlgorithm_.reset();
    std::string diagnostic = candidateError.diagnostic;
    diagnostic += "; calibration file or runtime rollback failed";
    publishCalibration(
        CalibrationProcessState::Failed,
        QStringLiteral("标定应用及回滚失败，视觉服务不可用"),
        calibrationError(
            vision::core::ErrorCode::InternalFailure,
            "The calibration update failed and the previous runtime could not be restored.",
            std::move(diagnostic)));
}

void VisionWorker::resetCalibration()
{
    calibrationSession_ = std::make_unique<CalibrationSession>();
    publishCalibration(
        CalibrationProcessState::NoInput,
        QStringLiteral("标定会话已重置"));
}

void VisionWorker::onPrepared(vision::core::LocateMode mode) noexcept
{
    if (!activeTask_.has_value()) {
        return;
    }
    try {
        if (activeTask_->options.mode == mode) {
            emit taskPrepared(*activeTask_);
        }
    }
    catch (...) {
        // A hardware observer must never throw through a vendor callback boundary.
    }
}

void VisionWorker::onFrames(
    const vision::core::ImageFrames& frames) noexcept
{
    try {
        QVector<TaskImageSnapshot> snapshots;
        snapshots.reserve(static_cast<qsizetype>(frames.frames.size()));
        for (const auto& frame : frames.frames) {
            if (frame.image.empty() || frame.image.type() != CV_8UC1) {
                continue;
            }
            const QImage view(
                frame.image.data,
                frame.image.cols,
                frame.image.rows,
                static_cast<qsizetype>(frame.image.step),
                QImage::Format_Grayscale8);
            QImage owned = view.copy();
            if (!owned.isNull()) {
                snapshots.push_back(TaskImageSnapshot{
                    frame.cameraRole, std::move(owned)});
            }
        }
        activeImages_ = std::move(snapshots);
    }
    catch (...) {
        activeImages_.clear();
    }
}

vision::core::Error VisionWorker::makeWorkerError(
    vision::core::ErrorCode code,
    const char* message,
    const char* diagnostic) const
{
    return vision::core::Error{code, message, diagnostic};
}

vision::core::CoreStatus VisionWorker::currentStatus() const
{
    if (core_) {
        return core_->status();
    }
    return vision::core::CoreStatus{
        shuttingDown_ ? vision::core::CoreState::Stopped
                      : vision::core::CoreState::Faulted,
        std::nullopt};
}

} // namespace vision::app
