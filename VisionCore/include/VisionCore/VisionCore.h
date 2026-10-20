#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "VisionCore/Diagnostics.h"
#include "VisionCore/Ports.h"

namespace vision::core {

class VisionCore {
public:
    VisionCore(std::unique_ptr<ICamera> camera,
               std::unique_ptr<ILight> light,
               std::unique_ptr<ILocateAlgorithm> algorithm,
               IDiagnosticSink* diagnostics = nullptr,
               ILocateFrameObserver* frameObserver = nullptr);
    ~VisionCore() noexcept;

    VisionCore(const VisionCore&) = delete;
    VisionCore& operator=(const VisionCore&) = delete;
    VisionCore(VisionCore&&) = delete;
    VisionCore& operator=(VisionCore&&) = delete;

    LocateOutcome locate(const LocateOptions& options);
    // Performs the same guarded light/camera acquisition transaction used by
    // locate(), but returns validated frames without invoking the locate
    // algorithm. Edge selects Center; MarkPoint selects all four corner roles.
    CaptureOutcome captureForCalibration(LocateMode mode);
    CoreStatus status() const;
    Status recover();
    Status shutdown();

private:
    Status turnOnLight(LocateMode mode);
    Status turnOffLight();
    Status recoverLight();
    Status shutdownLight();
    CaptureOutcome captureFrames(LocateMode mode);
    Status recoverCamera();
    Status shutdownCamera();
    AlgorithmOutcome locateFrames(const AlgorithmInput& input);

    std::optional<Error> validateFrame(const ImageFrame& frame) const;
    std::optional<Error> validateFrameSet(LocateMode mode, const ImageFrames& frames) const;
    std::optional<AlgorithmInput> makeAlgorithmInput(LocateMode mode, const ImageFrames& frames) const;

    void emitError(std::string_view operation,
                   const Error& error,
                   const ImageFrames* frames = nullptr,
                   const ImageFrame* relatedFrame = nullptr) noexcept;
    void emitEvent(DiagnosticSeverity severity,
                   std::string_view operation,
                   std::string_view message,
                   const ImageFrames* frames = nullptr,
                   const ImageFrame* relatedFrame = nullptr) noexcept;

    static bool faultsVisionService(ErrorCode code) noexcept;
    static bool isSupportedMode(LocateMode mode) noexcept;

    std::unique_ptr<ICamera> camera_;
    std::unique_ptr<ILight> light_;
    std::unique_ptr<ILocateAlgorithm> algorithm_;
    IDiagnosticSink* diagnostics_;
    ILocateFrameObserver* frameObserver_;
    CoreState state_{CoreState::Ready};
    std::optional<Error> lastError_;
};

} // namespace vision::core
