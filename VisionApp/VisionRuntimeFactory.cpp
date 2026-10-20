#include "VisionRuntimeFactory.h"

#include <VisionAlgorithm/VisionAlgorithm.h>
#include <VisionCore/VisionCore.h>

#include <exception>
#include <utility>
#include <variant>

namespace vision::app {
namespace {

[[nodiscard]] vision::core::Error factoryError(
    vision::core::ErrorCode code,
    std::string diagnostic)
{
    return vision::core::Error{
        code,
        "The vision runtime could not be created.",
        std::move(diagnostic)};
}

} // namespace

vision::hardware::HikCameraSystemConfig makeCameraSystemConfig(
    const VisionRuntimeConfig& config)
{
    vision::hardware::HikCameraSystemConfig cameraConfig;
    cameraConfig.cameras = config.cameras;
    cameraConfig.captureTimeout = config.captureTimeout;
    cameraConfig.trigger = config.productionTrigger;
    return cameraConfig;
}

CoreFactory makeProductionCoreFactory()
{
    return [](
               const VisionRuntimeConfig& config,
               vision::hardware::ICaptureObserver* observer,
               vision::core::IDiagnosticSink* diagnostics,
               vision::core::ILocateFrameObserver* frameObserver)
               -> CoreFactoryOutcome {
        try {
            auto algorithmOutcome =
                vision::algorithm::VisionAlgorithm::create(
                    config.algorithmConfigPath);
            if (const auto* error =
                    std::get_if<vision::core::Error>(&algorithmOutcome)) {
                return *error;
            }
            auto algorithm = std::move(std::get<
                std::unique_ptr<vision::core::ILocateAlgorithm>>(
                    algorithmOutcome));
            if (!algorithm) {
                return factoryError(
                    vision::core::ErrorCode::InternalFailure,
                    "VisionAlgorithm::create returned a null algorithm port.");
            }

            auto hardwareOutcome = vision::hardware::createHikHardware(
                makeCameraSystemConfig(config),
                config.lightController,
                observer,
                diagnostics);
            if (const auto* error =
                    std::get_if<vision::core::Error>(&hardwareOutcome)) {
                return *error;
            }
            auto ports = std::move(std::get<
                vision::hardware::HikHardwarePorts>(hardwareOutcome));
            if (!ports.camera || !ports.light) {
                return factoryError(
                    vision::core::ErrorCode::InternalFailure,
                    "createHikHardware returned an incomplete port set.");
            }

            auto core = std::make_unique<vision::core::VisionCore>(
                std::move(ports.camera),
                std::move(ports.light),
                std::move(algorithm),
                diagnostics,
                frameObserver);
            const vision::core::CoreStatus status = core->status();
            if (status.state != vision::core::CoreState::Ready) {
                return status.lastError.value_or(factoryError(
                    vision::core::ErrorCode::InternalFailure,
                    "VisionCore was created without entering Ready."));
            }
            return core;
        }
        catch (const std::exception& exception) {
            return factoryError(
                vision::core::ErrorCode::InternalFailure,
                exception.what());
        }
        catch (...) {
            return factoryError(
                vision::core::ErrorCode::InternalFailure,
                "The production factory threw an unknown exception.");
        }
    };
}

} // namespace vision::app
