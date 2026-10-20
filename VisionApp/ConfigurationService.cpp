#include "ConfigurationService.h"

#include <VisionHardware/HikHardware.h>

#include <utility>
#include <variant>

namespace vision::app {

ConfigurationService::ConfigurationService(
    std::filesystem::path path,
    ApplicationConfig activeConfiguration)
    : path_(std::move(path)),
      active_(std::move(activeConfiguration))
{
}

const ApplicationConfig& ConfigurationService::active() const noexcept
{
    return active_;
}

const std::filesystem::path& ConfigurationService::path() const noexcept
{
    return path_;
}

std::uint64_t ConfigurationService::revision() const noexcept
{
    return revision_;
}

vision::core::Status ConfigurationService::validate(
    const ApplicationConfig& candidate) const
{
    auto status = validateApplicationConfig(candidate);
    if (std::holds_alternative<vision::core::Error>(status)) {
        return status;
    }
    vision::hardware::HikCameraSystemConfig camera;
    camera.cameras = candidate.vision.cameras;
    camera.trigger = candidate.vision.productionTrigger;
    camera.captureTimeout = candidate.vision.captureTimeout;
    return vision::hardware::validateHikHardwareConfiguration(
        camera, candidate.vision.lightController);
}

vision::core::Status ConfigurationService::commit(
    const ApplicationConfig& candidate)
{
    auto status = saveApplicationConfig(path_, candidate);
    if (std::holds_alternative<vision::core::Error>(status)) {
        return status;
    }
    active_ = candidate;
    ++revision_;
    return std::monostate{};
}

} // namespace vision::app
