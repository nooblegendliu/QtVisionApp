#include "SimulationRuntimeFactory.h"

#include <VisionCore/VisionCore.h>

#include <opencv2/core.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace vision::app {
namespace {

using vision::core::AlgorithmInput;
using vision::core::AlgorithmOutcome;
using vision::core::AlgorithmOutput;
using vision::core::CameraRole;
using vision::core::CaptureOutcome;
using vision::core::DiagnosticEvent;
using vision::core::DiagnosticSeverity;
using vision::core::Error;
using vision::core::ErrorCode;
using vision::core::IDiagnosticSink;
using vision::core::ImageFrame;
using vision::core::ImageFrames;
using vision::core::LocateMode;
using vision::core::Status;

[[nodiscard]] Error simulationError(
    ErrorCode code,
    std::string message,
    std::string diagnostic)
{
    return Error{code, std::move(message), std::move(diagnostic)};
}

void submitDiagnostic(
    IDiagnosticSink* sink,
    DiagnosticSeverity severity,
    std::string operation,
    std::string message) noexcept
{
    if (sink == nullptr) {
        return;
    }
    try {
        sink->submit(DiagnosticEvent{
            severity,
            "SimulationRuntime",
            std::move(operation),
            std::move(message),
            {}});
    }
    catch (...) {
    }
}

[[nodiscard]] std::string cameraId(CameraRole role)
{
    switch (role) {
    case CameraRole::Center:
        return "SIM-CENTER";
    case CameraRole::TopLeft:
        return "SIM-TOP-LEFT";
    case CameraRole::TopRight:
        return "SIM-TOP-RIGHT";
    case CameraRole::BottomRight:
        return "SIM-BOTTOM-RIGHT";
    case CameraRole::BottomLeft:
        return "SIM-BOTTOM-LEFT";
    default:
        return "SIM-UNKNOWN";
    }
}

class SimulatedCamera final : public vision::core::ICamera {
public:
    SimulatedCamera(
        bool failFirstCapture,
        vision::hardware::ICaptureObserver* observer,
        IDiagnosticSink* diagnostics)
        : failurePending_(failFirstCapture),
          observer_(observer),
          diagnostics_(diagnostics)
    {
    }

    CaptureOutcome capture(LocateMode mode) override
    {
        if (stopped_) {
            return simulationError(
                ErrorCode::NotReady,
                "The simulated camera is stopped.",
                "capture requested after simulation shutdown");
        }
        if (mode != LocateMode::Edge && mode != LocateMode::MarkPoint) {
            return simulationError(
                ErrorCode::InvalidConfiguration,
                "The simulated camera received an unsupported locate mode.",
                "unsupported LocateMode value");
        }

        if (observer_ != nullptr) {
            observer_->onPrepared(mode);
        }
        if (failurePending_) {
            failurePending_ = false;
            submitDiagnostic(
                diagnostics_,
                DiagnosticSeverity::Warning,
                "capture",
                "Injected the configured first-capture failure.");
            return simulationError(
                ErrorCode::HardwareFailure,
                "The simulated camera capture failed.",
                "failFirstCapture injected a one-shot failure");
        }

        ImageFrames result;
        if (mode == LocateMode::Edge) {
            result.frames.reserve(1);
            result.frames.push_back(makeFrame(CameraRole::Center, 64));
        }
        else {
            constexpr std::array roles{
                CameraRole::TopLeft,
                CameraRole::TopRight,
                CameraRole::BottomRight,
                CameraRole::BottomLeft};
            result.frames.reserve(roles.size());
            std::uint8_t value = 80;
            for (const CameraRole role : roles) {
                result.frames.push_back(makeFrame(role, value));
                value = static_cast<std::uint8_t>(value + 20);
            }
        }
        submitDiagnostic(
            diagnostics_,
            DiagnosticSeverity::Info,
            "capture",
            mode == LocateMode::Edge
                ? "Produced one simulated center-camera frame."
                : "Produced four simulated corner-camera frames.");
        return result;
    }

    Status recover() override
    {
        if (stopped_) {
            return simulationError(
                ErrorCode::NotReady,
                "The simulated camera is stopped.",
                "recover requested after simulation shutdown");
        }
        submitDiagnostic(
            diagnostics_, DiagnosticSeverity::Info, "recover",
            "Simulated camera recovery completed.");
        return std::monostate{};
    }

    Status shutdown() override
    {
        stopped_ = true;
        return std::monostate{};
    }

private:
    [[nodiscard]] ImageFrame makeFrame(
        CameraRole role,
        std::uint8_t value)
    {
        ImageFrame frame;
        frame.image = cv::Mat(32, 32, CV_8UC1, cv::Scalar(value));
        frame.capturedAt = std::chrono::steady_clock::now();
        frame.frameNumber = nextFrameNumber_++;
        frame.cameraId = cameraId(role);
        frame.cameraRole = role;
        return frame;
    }

    bool failurePending_{false};
    bool stopped_{false};
    std::uint64_t nextFrameNumber_{1};
    vision::hardware::ICaptureObserver* observer_{nullptr};
    IDiagnosticSink* diagnostics_{nullptr};
};

class SimulatedLight final : public vision::core::ILight {
public:
    explicit SimulatedLight(IDiagnosticSink* diagnostics)
        : diagnostics_(diagnostics)
    {
    }

    Status turnOn(LocateMode) override
    {
        if (stopped_) {
            return simulationError(
                ErrorCode::NotReady,
                "The simulated light is stopped.",
                "turnOn requested after simulation shutdown");
        }
        on_ = true;
        return std::monostate{};
    }

    Status turnOff() override
    {
        on_ = false;
        return std::monostate{};
    }

    Status recover() override
    {
        if (stopped_) {
            return simulationError(
                ErrorCode::NotReady,
                "The simulated light is stopped.",
                "recover requested after simulation shutdown");
        }
        on_ = false;
        submitDiagnostic(
            diagnostics_, DiagnosticSeverity::Info, "recover",
            "Simulated light recovery completed.");
        return std::monostate{};
    }

    Status shutdown() override
    {
        on_ = false;
        stopped_ = true;
        return std::monostate{};
    }

private:
    bool on_{false};
    bool stopped_{false};
    IDiagnosticSink* diagnostics_{nullptr};
};

class SimulatedAlgorithm final : public vision::core::ILocateAlgorithm {
public:
    SimulatedAlgorithm(
        SimulationConfig config,
        IDiagnosticSink* diagnostics)
        : config_(std::move(config)), diagnostics_(diagnostics)
    {
    }

    AlgorithmOutcome locate(const AlgorithmInput& input) override
    {
        if (config_.locateDelay > std::chrono::milliseconds::zero()) {
            std::this_thread::sleep_for(config_.locateDelay);
        }

        const SimulationResultConfig* configured = nullptr;
        if (std::holds_alternative<vision::core::EdgeAlgorithmInput>(input)) {
            configured = &config_.edgeResult;
        }
        else if (std::holds_alternative<
                     vision::core::MarkPointAlgorithmInput>(input)) {
            configured = &config_.markPointResult;
        }
        else {
            return simulationError(
                ErrorCode::InternalFailure,
                "The simulated algorithm received an invalid input.",
                "AlgorithmInput has no supported alternative");
        }

        const bool valid = configured->decision == SimulationDecision::Ok;
        submitDiagnostic(
            diagnostics_, DiagnosticSeverity::Info, "locate",
            valid ? "Returned the configured simulated OK result."
                  : "Returned the configured simulated NG result.");
        if (!valid) {
            return AlgorithmOutput{0.0, 0.0, 0.0, false};
        }
        return AlgorithmOutput{
            configured->x,
            configured->y,
            configured->r,
            true};
    }

private:
    SimulationConfig config_;
    IDiagnosticSink* diagnostics_{nullptr};
};

[[nodiscard]] Error factoryError(std::string diagnostic)
{
    return simulationError(
        ErrorCode::InternalFailure,
        "The simulation runtime could not be created.",
        std::move(diagnostic));
}

} // namespace

CoreFactory makeSimulationCoreFactory(SimulationConfig config)
{
    return [config = std::move(config)](
               const VisionRuntimeConfig&,
               vision::hardware::ICaptureObserver* observer,
               IDiagnosticSink* diagnostics,
               vision::core::ILocateFrameObserver* frameObserver)
               -> CoreFactoryOutcome {
        try {
            auto camera = std::make_unique<SimulatedCamera>(
                config.failFirstCapture, observer, diagnostics);
            auto light = std::make_unique<SimulatedLight>(diagnostics);
            auto algorithm = std::make_unique<SimulatedAlgorithm>(
                config, diagnostics);
            auto core = std::make_unique<vision::core::VisionCore>(
                std::move(camera),
                std::move(light),
                std::move(algorithm),
                diagnostics,
                frameObserver);
            const vision::core::CoreStatus status = core->status();
            if (status.state != vision::core::CoreState::Ready) {
                return status.lastError.value_or(factoryError(
                    "VisionCore did not enter Ready."));
            }
            submitDiagnostic(
                diagnostics, DiagnosticSeverity::Warning, "create",
                "Development simulation runtime is active.");
            return core;
        }
        catch (const std::exception& exception) {
            return factoryError(exception.what());
        }
        catch (...) {
            return factoryError("unknown exception");
        }
    };
}

} // namespace vision::app
