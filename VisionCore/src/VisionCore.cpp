#include "VisionCore/VisionCore.h"

#include <cmath>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <opencv2/core.hpp>

namespace vision::core {
namespace {

Error makeError(ErrorCode code, std::string message, std::string diagnostic = {}) {
    return Error{code, std::move(message), std::move(diagnostic)};
}

Error makeBusyError() {
    return makeError(ErrorCode::Busy,
                     "A locate operation is already in progress.",
                     "VisionCore rejected a reentrant locate request while state is Locating.");
}

Error makeNotReadyError() {
    return makeError(ErrorCode::NotReady,
                     "VisionCore is not ready to perform this operation.",
                     "The current CoreState does not accept the requested operation.");
}

Error makeMissingPortError(std::string_view portName) {
    return makeError(ErrorCode::InvalidConfiguration,
                     "VisionCore was constructed without a required port.",
                     "Missing required port: " + std::string(portName));
}

Error makeUnsupportedModeError() {
    return makeError(ErrorCode::InvalidConfiguration,
                     "The locate mode is not supported.",
                     "LocateOptions.mode is not Edge or MarkPoint.");
}

Error makeUnexpectedExceptionError(std::string_view operation, const std::exception& exception) {
    return makeError(ErrorCode::InternalFailure,
                     "An unexpected exception occurred in VisionCore.",
                     std::string(operation) + ": " + exception.what());
}

Error makeUnknownExceptionError(std::string_view operation) {
    return makeError(ErrorCode::InternalFailure,
                     "An unknown exception occurred in VisionCore.",
                     std::string(operation) + ": unknown exception");
}

template <typename OutcomeType, typename Operation>
OutcomeType invokePort(Operation&& operation, std::string_view operationName) {
    try {
        OutcomeType outcome = std::forward<Operation>(operation)();
        if (outcome.valueless_by_exception()) {
            return makeError(ErrorCode::InternalFailure,
                             "A VisionCore port returned an invalid outcome.",
                             std::string(operationName) + ": std::variant is valueless_by_exception");
        }
        return outcome;
    } catch (const std::exception& exception) {
        return makeUnexpectedExceptionError(operationName, exception);
    } catch (...) {
        return makeUnknownExceptionError(operationName);
    }
}

template <typename Success>
std::optional<Error> takeError(std::variant<Success, Error>&& outcome) {
    if (!std::holds_alternative<Error>(outcome)) {
        return std::nullopt;
    }

    return std::get<Error>(std::move(outcome));
}

const char* cameraRoleName(CameraRole role) noexcept {
    switch (role) {
    case CameraRole::Unknown:
        return "Unknown";
    case CameraRole::Center:
        return "Center";
    case CameraRole::TopLeft:
        return "TopLeft";
    case CameraRole::TopRight:
        return "TopRight";
    case CameraRole::BottomRight:
        return "BottomRight";
    case CameraRole::BottomLeft:
        return "BottomLeft";
    }

    return "Unknown";
}

bool isKnownCameraRole(CameraRole role) noexcept {
    switch (role) {
    case CameraRole::Unknown:
    case CameraRole::Center:
    case CameraRole::TopLeft:
    case CameraRole::TopRight:
    case CameraRole::BottomRight:
    case CameraRole::BottomLeft:
        return true;
    }

    return false;
}

const char* locateModeName(LocateMode mode) noexcept {
    switch (mode) {
    case LocateMode::Edge:
        return "Edge";
    case LocateMode::MarkPoint:
        return "MarkPoint";
    }

    return "Unknown";
}

std::string frameDiagnostic(const ImageFrame& frame) {
    return "cameraId=" + frame.cameraId + "; cameraRole=" + cameraRoleName(frame.cameraRole) +
           "; frameNumber=" + std::to_string(frame.frameNumber);
}

FrameDiagnosticContext makeFrameDiagnosticContext(const ImageFrame& frame) {
    return FrameDiagnosticContext{frame.capturedAt, frame.frameNumber, frame.cameraId, frame.cameraRole};
}

} // namespace

VisionCore::VisionCore(std::unique_ptr<ICamera> camera,
                       std::unique_ptr<ILight> light,
                       std::unique_ptr<ILocateAlgorithm> algorithm,
                       IDiagnosticSink* diagnostics,
                       ILocateFrameObserver* frameObserver)
    : camera_(std::move(camera)),
      light_(std::move(light)),
      algorithm_(std::move(algorithm)),
      diagnostics_(diagnostics),
      frameObserver_(frameObserver) {
    if (camera_ && light_ && algorithm_) {
        return;
    }

    Error error = makeError(ErrorCode::InvalidConfiguration,
                            "VisionCore requires camera, light, and algorithm ports.",
                            "At least one required VisionCore port is null.");
    state_ = CoreState::Faulted;
    lastError_ = error;
    emitError("construction", error);
}

VisionCore::~VisionCore() noexcept {
    if (state_ == CoreState::Stopped) {
        return;
    }

    try {
        static_cast<void>(shutdown());
    } catch (...) {
        // Destruction must remain noexcept; shutdown() already performs best-effort cleanup.
    }
}

LocateOutcome VisionCore::locate(const LocateOptions& options) {
    if (state_ == CoreState::Locating) {
        return makeBusyError();
    }
    if (state_ != CoreState::Ready) {
        return makeNotReadyError();
    }

    if (!isSupportedMode(options.mode)) {
        Error error = makeUnsupportedModeError();
        emitError("locate.options", error);
        lastError_ = error;
        state_ = CoreState::Faulted;
        return error;
    }

    state_ = CoreState::Locating;

    std::optional<ImageFrames> frames;
    std::optional<Error> primaryError;
    std::optional<LocateResult> result;

    const auto recordPrimaryError = [this, &primaryError, &frames](Error error,
                                                                     std::string_view operation,
                                                                     const ImageFrame* relatedFrame = nullptr) {
        primaryError = std::move(error);
        emitError(operation, *primaryError, frames ? &*frames : nullptr, relatedFrame);
    };

    try {
        Status lightOn = turnOnLight(options.mode);
        if (auto error = takeError(std::move(lightOn))) {
            recordPrimaryError(std::move(*error), "light.turnOn");
        } else {
            CaptureOutcome capture = captureFrames(options.mode);
            if (auto error = takeError(std::move(capture))) {
                recordPrimaryError(std::move(*error), "camera.capture");
            } else {
                frames = std::get<ImageFrames>(std::move(capture));
            }
        }
    } catch (const std::exception& exception) {
        recordPrimaryError(makeUnexpectedExceptionError("locate.capture", exception), "locate.capture");
    } catch (...) {
        recordPrimaryError(makeUnknownExceptionError("locate.capture"), "locate.capture");
    }

    Status lightOff = turnOffLight();
    std::optional<Error> lightOffError = takeError(std::move(lightOff));
    if (lightOffError) {
        emitError("light.turnOff", *lightOffError, frames ? &*frames : nullptr);
    }

    if (lightOffError) {
        if (primaryError) {
            emitEvent(DiagnosticSeverity::Warning,
                      "locate.cleanupPriority",
                      "A primary locate error was superseded by a light turn-off error.",
                      frames ? &*frames : nullptr);
        }
        lastError_ = *lightOffError;
        state_ = CoreState::Faulted;
        return std::move(*lightOffError);
    }

    // Image validation and algorithm processing intentionally start only after the
    // light has been confirmed off. This keeps illumination time bounded by the
    // acquisition transaction instead of algorithm execution time.
    if (!primaryError) {
        try {
            if (!frames) {
                recordPrimaryError(
                    makeError(ErrorCode::InternalFailure,
                              "VisionCore completed capture without image frames.",
                              "The camera returned success but no ImageFrames value was retained."),
                    "camera.capture");
            } else {
                for (const ImageFrame& frame : frames->frames) {
                    if (auto imageError = validateFrame(frame)) {
                        recordPrimaryError(std::move(*imageError), "image.validate", &frame);
                        break;
                    }
                }

                if (!primaryError) {
                    if (auto frameSetError = validateFrameSet(options.mode, *frames)) {
                        recordPrimaryError(std::move(*frameSetError), "images.validate");
                    } else if (auto input = makeAlgorithmInput(options.mode, *frames)) {
                        if (frameObserver_ != nullptr) {
                            try {
                                frameObserver_->onFrames(*frames);
                            } catch (...) {
                                // Presentation is best effort and must not alter
                                // the locate result or Core state.
                            }
                        }
                        AlgorithmOutcome algorithm = locateFrames(*input);
                        if (auto error = takeError(std::move(algorithm))) {
                            recordPrimaryError(std::move(*error), "algorithm.locate");
                        } else {
                            AlgorithmOutput output = std::get<AlgorithmOutput>(std::move(algorithm));
                            if (!output.valid) {
                                result = LocateResult{0.0, 0.0, 0.0, LocateDecision::Ng};
                            } else if (!std::isfinite(output.x) || !std::isfinite(output.y) ||
                                       !std::isfinite(output.r)) {
                                recordPrimaryError(
                                    makeError(ErrorCode::InvalidResult,
                                              "The algorithm returned a non-finite locate result.",
                                              "AlgorithmOutput contained NaN or infinity."),
                                    "result.validate");
                            } else {
                                result = LocateResult{output.x, output.y, output.r, LocateDecision::Ok};
                            }
                        }
                    } else {
                        recordPrimaryError(
                            makeError(ErrorCode::InternalFailure,
                                      "VisionCore could not construct the algorithm input.",
                                      "Validated ImageFrames could not be mapped to the requested LocateMode."),
                            "algorithm.input");
                    }
                }
            }
        } catch (const std::exception& exception) {
            recordPrimaryError(makeUnexpectedExceptionError("locate.process", exception), "locate.process");
        } catch (...) {
            recordPrimaryError(makeUnknownExceptionError("locate.process"), "locate.process");
        }
    }

    if (primaryError) {
        lastError_ = *primaryError;
        state_ = faultsVisionService(primaryError->code) ? CoreState::Faulted : CoreState::Ready;
        return std::move(*primaryError);
    }

    if (!result) {
        Error error = makeError(ErrorCode::InternalFailure,
                                "VisionCore completed locate without a result.",
                                "No LocateResult or primary Error was produced after the locate flow.");
        emitError("locate", error, frames ? &*frames : nullptr);
        lastError_ = error;
        state_ = CoreState::Faulted;
        return error;
    }

    state_ = CoreState::Ready;
    lastError_.reset();
    return std::move(*result);
}

CaptureOutcome VisionCore::captureForCalibration(LocateMode mode) {
    if (state_ == CoreState::Locating) {
        return makeBusyError();
    }
    if (state_ != CoreState::Ready) {
        return makeNotReadyError();
    }
    if (!isSupportedMode(mode)) {
        Error error = makeUnsupportedModeError();
        emitError("calibration.capture.options", error);
        lastError_ = error;
        state_ = CoreState::Faulted;
        return error;
    }

    state_ = CoreState::Locating;
    std::optional<ImageFrames> frames;
    std::optional<Error> primaryError;
    const auto recordPrimaryError =
        [this, &primaryError, &frames](Error error,
                                      std::string_view operation,
                                      const ImageFrame* relatedFrame = nullptr) {
            primaryError = std::move(error);
            emitError(operation, *primaryError,
                      frames ? &*frames : nullptr, relatedFrame);
        };

    try {
        Status lightOn = turnOnLight(mode);
        if (auto error = takeError(std::move(lightOn))) {
            recordPrimaryError(
                std::move(*error), "calibration.light.turnOn");
        } else {
            CaptureOutcome capture = captureFrames(mode);
            if (auto error = takeError(std::move(capture))) {
                recordPrimaryError(
                    std::move(*error), "calibration.camera.capture");
            } else {
                frames = std::get<ImageFrames>(std::move(capture));
            }
        }
    } catch (const std::exception& exception) {
        recordPrimaryError(
            makeUnexpectedExceptionError(
                "calibration.capture", exception),
            "calibration.capture");
    } catch (...) {
        recordPrimaryError(
            makeUnknownExceptionError("calibration.capture"),
            "calibration.capture");
    }

    Status lightOff = turnOffLight();
    std::optional<Error> lightOffError = takeError(std::move(lightOff));
    if (lightOffError) {
        emitError("calibration.light.turnOff", *lightOffError,
                  frames ? &*frames : nullptr);
        if (primaryError) {
            emitEvent(
                DiagnosticSeverity::Warning,
                "calibration.capture.cleanupPriority",
                "A primary calibration capture error was superseded by a light turn-off error.",
                frames ? &*frames : nullptr);
        }
        lastError_ = *lightOffError;
        state_ = CoreState::Faulted;
        return std::move(*lightOffError);
    }

    if (!primaryError) {
        try {
            if (!frames) {
                recordPrimaryError(
                    makeError(
                        ErrorCode::InternalFailure,
                        "VisionCore completed calibration capture without image frames.",
                        "The camera returned success but no ImageFrames value was retained."),
                    "calibration.camera.capture");
            } else {
                for (const ImageFrame& frame : frames->frames) {
                    if (auto imageError = validateFrame(frame)) {
                        recordPrimaryError(
                            std::move(*imageError),
                            "calibration.image.validate", &frame);
                        break;
                    }
                }
                if (!primaryError) {
                    if (auto frameSetError = validateFrameSet(mode, *frames)) {
                        recordPrimaryError(
                            std::move(*frameSetError),
                            "calibration.images.validate");
                    }
                }
            }
        } catch (const std::exception& exception) {
            recordPrimaryError(
                makeUnexpectedExceptionError(
                    "calibration.capture.process", exception),
                "calibration.capture.process");
        } catch (...) {
            recordPrimaryError(
                makeUnknownExceptionError("calibration.capture.process"),
                "calibration.capture.process");
        }
    }

    if (primaryError) {
        lastError_ = *primaryError;
        state_ = faultsVisionService(primaryError->code)
            ? CoreState::Faulted : CoreState::Ready;
        return std::move(*primaryError);
    }
    if (!frames) {
        Error error = makeError(
            ErrorCode::InternalFailure,
            "VisionCore completed calibration capture without a result.",
            "No ImageFrames or primary Error was produced after calibration capture.");
        emitError("calibration.capture", error);
        lastError_ = error;
        state_ = CoreState::Faulted;
        return error;
    }

    state_ = CoreState::Ready;
    lastError_.reset();
    return std::move(*frames);
}

CoreStatus VisionCore::status() const {
    return CoreStatus{state_, lastError_};
}

Status VisionCore::recover() {
    if (state_ == CoreState::Ready) {
        return std::monostate{};
    }
    if (state_ == CoreState::Locating) {
        return makeBusyError();
    }
    if (state_ != CoreState::Faulted) {
        return makeNotReadyError();
    }

    try {
        std::optional<Error> lightRecoverError = takeError(recoverLight());
        if (lightRecoverError) {
            emitError("light.recover", *lightRecoverError);
        }

        std::optional<Error> lightOffError = takeError(turnOffLight());
        if (lightOffError) {
            emitError("light.turnOff", *lightOffError);
        }

        std::optional<Error> cameraRecoverError = takeError(recoverCamera());
        if (cameraRecoverError) {
            emitError("camera.recover", *cameraRecoverError);
        }

        if (lightOffError) {
            lastError_ = *lightOffError;
            state_ = CoreState::Faulted;
            return std::move(*lightOffError);
        }
        if (lightRecoverError) {
            lastError_ = *lightRecoverError;
            state_ = CoreState::Faulted;
            return std::move(*lightRecoverError);
        }
        if (cameraRecoverError) {
            lastError_ = *cameraRecoverError;
            state_ = CoreState::Faulted;
            return std::move(*cameraRecoverError);
        }

        state_ = CoreState::Ready;
        lastError_.reset();
        return std::monostate{};
    } catch (const std::exception& exception) {
        Error error = makeUnexpectedExceptionError("recover", exception);
        emitError("recover", error);
        lastError_ = error;
        state_ = CoreState::Faulted;
        return error;
    } catch (...) {
        Error error = makeUnknownExceptionError("recover");
        emitError("recover", error);
        lastError_ = error;
        state_ = CoreState::Faulted;
        return error;
    }
}

Status VisionCore::shutdown() {
    if (state_ == CoreState::Stopped) {
        return std::monostate{};
    }
    if (state_ == CoreState::Locating) {
        return makeBusyError();
    }
    if (state_ == CoreState::ShuttingDown) {
        return makeNotReadyError();
    }

    state_ = CoreState::ShuttingDown;

    try {
        std::optional<Error> lightOffError = takeError(turnOffLight());
        if (lightOffError) {
            emitError("light.turnOff", *lightOffError);
        }

        std::optional<Error> cameraShutdownError = takeError(shutdownCamera());
        if (cameraShutdownError) {
            emitError("camera.shutdown", *cameraShutdownError);
        }

        std::optional<Error> lightShutdownError = takeError(shutdownLight());
        if (lightShutdownError) {
            emitError("light.shutdown", *lightShutdownError);
        }

        state_ = CoreState::Stopped;

        if (lightOffError) {
            lastError_ = *lightOffError;
            return std::move(*lightOffError);
        }
        if (lightShutdownError) {
            lastError_ = *lightShutdownError;
            return std::move(*lightShutdownError);
        }
        if (cameraShutdownError) {
            lastError_ = *cameraShutdownError;
            return std::move(*cameraShutdownError);
        }

        return std::monostate{};
    } catch (const std::exception& exception) {
        Error error = makeUnexpectedExceptionError("shutdown", exception);
        emitError("shutdown", error);
        lastError_ = error;
        state_ = CoreState::Stopped;
        return error;
    } catch (...) {
        Error error = makeUnknownExceptionError("shutdown");
        emitError("shutdown", error);
        lastError_ = error;
        state_ = CoreState::Stopped;
        return error;
    }
}

Status VisionCore::turnOnLight(LocateMode mode) {
    if (!light_) {
        return makeMissingPortError("ILight");
    }
    return invokePort<Status>([this, mode] { return light_->turnOn(mode); }, "light.turnOn");
}

Status VisionCore::turnOffLight() {
    if (!light_) {
        return makeMissingPortError("ILight");
    }
    return invokePort<Status>([this] { return light_->turnOff(); }, "light.turnOff");
}

Status VisionCore::recoverLight() {
    if (!light_) {
        return makeMissingPortError("ILight");
    }
    return invokePort<Status>([this] { return light_->recover(); }, "light.recover");
}

Status VisionCore::shutdownLight() {
    if (!light_) {
        return makeMissingPortError("ILight");
    }
    return invokePort<Status>([this] { return light_->shutdown(); }, "light.shutdown");
}

CaptureOutcome VisionCore::captureFrames(LocateMode mode) {
    if (!camera_) {
        return makeMissingPortError("ICamera");
    }
    return invokePort<CaptureOutcome>([this, mode] { return camera_->capture(mode); }, "camera.capture");
}

Status VisionCore::recoverCamera() {
    if (!camera_) {
        return makeMissingPortError("ICamera");
    }
    return invokePort<Status>([this] { return camera_->recover(); }, "camera.recover");
}

Status VisionCore::shutdownCamera() {
    if (!camera_) {
        return makeMissingPortError("ICamera");
    }
    return invokePort<Status>([this] { return camera_->shutdown(); }, "camera.shutdown");
}

AlgorithmOutcome VisionCore::locateFrames(const AlgorithmInput& input) {
    if (!algorithm_) {
        return makeMissingPortError("ILocateAlgorithm");
    }
    return invokePort<AlgorithmOutcome>([this, &input] { return algorithm_->locate(input); }, "algorithm.locate");
}

std::optional<Error> VisionCore::validateFrame(const ImageFrame& frame) const {
    const std::string context = frameDiagnostic(frame);
    if (frame.image.empty()) {
        return makeError(ErrorCode::InvalidImage,
                         "The captured image is empty.",
                         "ImageFrame.image is empty; " + context);
    }
    if (frame.image.type() != CV_8UC1) {
        return makeError(ErrorCode::InvalidImage,
                         "The captured image format is not CV_8UC1.",
                         "ImageFrame.image.type=" + std::to_string(frame.image.type()) + "; " + context);
    }
    if (!frame.image.isContinuous()) {
        return makeError(ErrorCode::InvalidImage,
                         "The captured image is not continuously stored.",
                         "ImageFrame.image.isContinuous() is false; " + context);
    }
    if (frame.capturedAt == std::chrono::steady_clock::time_point{}) {
        return makeError(ErrorCode::InvalidImage,
                         "The captured image timestamp is invalid.",
                         "ImageFrame.capturedAt is the default time point; " + context);
    }
    if (frame.cameraId.empty()) {
        return makeError(ErrorCode::InvalidImage,
                         "The captured image camera identifier is empty.",
                         "ImageFrame.cameraId is empty; " + context);
    }
    if (frame.cameraRole == CameraRole::Unknown || !isKnownCameraRole(frame.cameraRole)) {
        return makeError(ErrorCode::InvalidImage,
                         "The captured image camera role is unknown.",
                         "ImageFrame.cameraRole is Unknown; " + context);
    }
    return std::nullopt;
}

std::optional<Error> VisionCore::validateFrameSet(LocateMode mode, const ImageFrames& frames) const {
    if (!isSupportedMode(mode)) {
        return makeUnsupportedModeError();
    }

    const std::size_t expectedCount = mode == LocateMode::Edge ? 1U : 4U;
    if (frames.frames.size() != expectedCount) {
        return makeError(ErrorCode::InvalidImage,
                         "The captured image count does not match the locate mode.",
                         "mode=" + std::string(locateModeName(mode)) + "; expectedCount=" +
                             std::to_string(expectedCount) + "; actualCount=" +
                             std::to_string(frames.frames.size()));
    }

    for (std::size_t index = 0; index < frames.frames.size(); ++index) {
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (frames.frames[index].cameraId == frames.frames[previous].cameraId) {
                return makeError(ErrorCode::InvalidImage,
                                 "The captured image set contains duplicate camera identifiers.",
                                 "Duplicate cameraId=" + frames.frames[index].cameraId);
            }
        }
    }

    if (mode == LocateMode::Edge) {
        if (frames.frames.front().cameraRole != CameraRole::Center) {
            return makeError(ErrorCode::InvalidImage,
                             "Edge locate requires one Center camera image.",
                             "Edge mode received " + std::string(cameraRoleName(frames.frames.front().cameraRole)) +
                                 "; " + frameDiagnostic(frames.frames.front()));
        }
        return std::nullopt;
    }

    bool hasTopLeft = false;
    bool hasTopRight = false;
    bool hasBottomRight = false;
    bool hasBottomLeft = false;
    for (const ImageFrame& frame : frames.frames) {
        bool* seen = nullptr;
        switch (frame.cameraRole) {
        case CameraRole::TopLeft:
            seen = &hasTopLeft;
            break;
        case CameraRole::TopRight:
            seen = &hasTopRight;
            break;
        case CameraRole::BottomRight:
            seen = &hasBottomRight;
            break;
        case CameraRole::BottomLeft:
            seen = &hasBottomLeft;
            break;
        case CameraRole::Unknown:
        case CameraRole::Center:
            return makeError(ErrorCode::InvalidImage,
                             "MarkPoint locate requires only corner camera images.",
                             "MarkPoint mode received " + std::string(cameraRoleName(frame.cameraRole)) + "; " +
                                 frameDiagnostic(frame));
        default:
            return makeError(ErrorCode::InvalidImage,
                             "MarkPoint locate received an unsupported camera role.",
                             "MarkPoint mode received an unknown CameraRole value; " + frameDiagnostic(frame));
        }

        if (*seen) {
            return makeError(ErrorCode::InvalidImage,
                             "The captured image set contains duplicate camera roles.",
                             "Duplicate cameraRole=" + std::string(cameraRoleName(frame.cameraRole)) + "; " +
                                 frameDiagnostic(frame));
        }
        *seen = true;
    }

    if (!hasTopLeft || !hasTopRight || !hasBottomRight || !hasBottomLeft) {
        return makeError(ErrorCode::InvalidImage,
                         "The captured image set is missing a required corner camera role.",
                         "MarkPoint mode requires TopLeft, TopRight, BottomRight, and BottomLeft exactly once.");
    }

    return std::nullopt;
}

std::optional<AlgorithmInput> VisionCore::makeAlgorithmInput(LocateMode mode, const ImageFrames& frames) const {
    if (mode == LocateMode::Edge) {
        if (frames.frames.size() != 1U || frames.frames.front().cameraRole != CameraRole::Center) {
            return std::nullopt;
        }
        return AlgorithmInput{EdgeAlgorithmInput{std::cref(frames.frames.front().image)}};
    }

    if (mode != LocateMode::MarkPoint || frames.frames.size() != 4U) {
        return std::nullopt;
    }

    const ImageFrame* topLeft = nullptr;
    const ImageFrame* topRight = nullptr;
    const ImageFrame* bottomRight = nullptr;
    const ImageFrame* bottomLeft = nullptr;
    for (const ImageFrame& frame : frames.frames) {
        switch (frame.cameraRole) {
        case CameraRole::TopLeft:
            topLeft = &frame;
            break;
        case CameraRole::TopRight:
            topRight = &frame;
            break;
        case CameraRole::BottomRight:
            bottomRight = &frame;
            break;
        case CameraRole::BottomLeft:
            bottomLeft = &frame;
            break;
        case CameraRole::Unknown:
        case CameraRole::Center:
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    if (topLeft == nullptr || topRight == nullptr || bottomRight == nullptr || bottomLeft == nullptr) {
        return std::nullopt;
    }

    return AlgorithmInput{MarkPointAlgorithmInput{
        std::cref(topLeft->image),
        std::cref(topRight->image),
        std::cref(bottomRight->image),
        std::cref(bottomLeft->image)}};
}

void VisionCore::emitError(std::string_view operation,
                           const Error& error,
                           const ImageFrames* frames,
                           const ImageFrame* relatedFrame) noexcept {
    try {
        std::string message = error.message;
        if (!error.diagnostic.empty()) {
            message += " | diagnostic: ";
            message += error.diagnostic;
        }
        emitEvent(DiagnosticSeverity::Error, operation, message, frames, relatedFrame);
    } catch (...) {
        // Diagnostics are observational and must never alter the business outcome.
    }
}

void VisionCore::emitEvent(DiagnosticSeverity severity,
                           std::string_view operation,
                           std::string_view message,
                           const ImageFrames* frames,
                           const ImageFrame* relatedFrame) noexcept {
    if (diagnostics_ == nullptr) {
        return;
    }

    try {
        std::vector<FrameDiagnosticContext> contexts;
        if (relatedFrame != nullptr) {
            contexts.push_back(makeFrameDiagnosticContext(*relatedFrame));
        } else if (frames != nullptr) {
            contexts.reserve(frames->frames.size());
            for (const ImageFrame& frame : frames->frames) {
                contexts.push_back(makeFrameDiagnosticContext(frame));
            }
        }

        DiagnosticEvent event{
            severity,
            "VisionCore",
            std::string(operation),
            std::string(message),
            std::move(contexts)};
        diagnostics_->submit(event);
    } catch (...) {
        // A non-conforming diagnostic sink must not interrupt VisionCore.
    }
}

bool VisionCore::faultsVisionService(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::AlgorithmFailure:
    case ErrorCode::AlgorithmTimeout:
    case ErrorCode::InvalidResult:
        return false;
    case ErrorCode::NotReady:
    case ErrorCode::Busy:
    case ErrorCode::InvalidConfiguration:
    case ErrorCode::HardwareUnavailable:
    case ErrorCode::HardwareFailure:
    case ErrorCode::HardwareTimeout:
    case ErrorCode::InvalidImage:
    case ErrorCode::InternalFailure:
        return true;
    }

    return true;
}

bool VisionCore::isSupportedMode(LocateMode mode) noexcept {
    return mode == LocateMode::Edge || mode == LocateMode::MarkPoint;
}

} // namespace vision::core
