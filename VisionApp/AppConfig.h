#pragma once

#include <VisionCore/Error.h>
#include <VisionHardware/HikHardware.h>
#include <VisionTcp/VisionTcpTypes.h>

#include <QMetaType>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

namespace vision::app {

struct LoggingConfig {
    std::filesystem::path directory;
    std::string level{"info"};
    std::size_t maximumFileBytes{0};
    std::size_t maximumFiles{0};
};

struct VisionRuntimeConfig {
    std::array<vision::hardware::HikCameraDeviceConfig, 5> cameras;
    vision::hardware::HikTriggerConfig productionTrigger;
    std::chrono::milliseconds captureTimeout{};
    vision::hardware::HikLightControllerConfig lightController;
    std::filesystem::path algorithmConfigPath;
};

struct ResultDeliveryPolicy {
    std::chrono::milliseconds acknowledgementInterval{
        std::chrono::seconds(1)};
    int maximumRetries{3};
};

enum class SimulationDecision {
    Ok,
    Ng
};

struct SimulationResultConfig {
    SimulationDecision decision{SimulationDecision::Ng};
    double x{0.0};
    double y{0.0};
    double r{0.0};
};

struct SimulationConfig {
    bool enabled{false};
    std::chrono::milliseconds locateDelay{};
    bool failFirstCapture{false};
    SimulationResultConfig edgeResult;
    SimulationResultConfig markPointResult;
};

struct ApplicationConfig {
    int schemaVersion{1};
    vision::tcp::TcpServerConfig tcp;
    VisionRuntimeConfig vision;
    LoggingConfig logging;
    ResultDeliveryPolicy resultDelivery;
    SimulationConfig simulation;
};

using ApplicationConfigOutcome =
    vision::core::Outcome<ApplicationConfig>;

[[nodiscard]] ApplicationConfigOutcome loadApplicationConfig(
    const std::filesystem::path& path);
[[nodiscard]] vision::core::Status validateApplicationConfig(
    const ApplicationConfig& config);
[[nodiscard]] vision::core::Status saveApplicationConfig(
    const std::filesystem::path& path,
    const ApplicationConfig& config);

} // namespace vision::app

Q_DECLARE_METATYPE(vision::app::VisionRuntimeConfig)
Q_DECLARE_METATYPE(vision::app::ApplicationConfig)
