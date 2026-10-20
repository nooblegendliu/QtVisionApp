#pragma once

#include <VisionCore/Diagnostics.h>
#include <VisionCore/Ports.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace vision::hardware {

enum class HikCameraModel {
    MvCh25090Gm,
    MvCs12010Gm
};

enum class HikTriggerMode {
    Software,
    Hardware
};

enum class HikInputLine {
    Line0,
    Line2
};

enum class HikTriggerActivation {
    RisingEdge,
    FallingEdge
};

using HikMicroseconds = std::chrono::duration<double, std::micro>;

struct HikCameraDeviceConfig {
    vision::core::CameraRole role{vision::core::CameraRole::Unknown};
    HikCameraModel model{HikCameraModel::MvCh25090Gm};
    std::string serialNumber;
    std::optional<HikMicroseconds> exposureTime;
    std::optional<double> gain;
};

struct HikTriggerConfig {
    HikTriggerMode mode{HikTriggerMode::Software};
    HikInputLine inputLine{HikInputLine::Line0};
    HikTriggerActivation activation{HikTriggerActivation::RisingEdge};
    HikMicroseconds delay{};
};

struct HikCameraSystemConfig {
    std::array<HikCameraDeviceConfig, 5> cameras;
    HikTriggerConfig trigger;
    std::chrono::milliseconds captureTimeout{};
};

enum class HikLightChannelRole {
    WhiteBacklight,
    CoaxialRed,
    CoaxialGreen,
    CoaxialBlue
};

struct HikLightChannelConfig {
    HikLightChannelRole role{HikLightChannelRole::WhiteBacklight};
    std::string outputSelector;
    double normalizedIntensity{0.0};
};

struct HikLightControllerConfig {
    std::string serialNumber;
    std::array<HikLightChannelConfig, 4> channels;
};

class ICaptureObserver {
public:
    virtual ~ICaptureObserver() = default;

    virtual void onPrepared(vision::core::LocateMode mode) noexcept = 0;
};

struct HikHardwarePorts {
    std::unique_ptr<vision::core::ICamera> camera;
    std::unique_ptr<vision::core::ILight> light;
};

using HikHardwareCreateOutcome = vision::core::Outcome<HikHardwarePorts>;

[[nodiscard]] vision::core::Status validateHikHardwareConfiguration(
    const HikCameraSystemConfig& cameraConfig,
    const HikLightControllerConfig& lightConfig);

[[nodiscard]] HikHardwareCreateOutcome createHikHardware(
    HikCameraSystemConfig cameraConfig,
    HikLightControllerConfig lightConfig,
    ICaptureObserver* captureObserver = nullptr,
    vision::core::IDiagnosticSink* diagnostics = nullptr);

} // namespace vision::hardware
