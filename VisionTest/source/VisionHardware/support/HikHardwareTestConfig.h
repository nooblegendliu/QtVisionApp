#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <VisionHardware/HikHardware.h>

namespace vision::test::hardware {

inline vision::hardware::HikCameraSystemConfig validHikCameraConfiguration() {
    using vision::core::CameraRole;
    using vision::hardware::HikCameraModel;

    vision::hardware::HikCameraSystemConfig config;
    config.cameras = {{
        {CameraRole::Center, HikCameraModel::MvCh25090Gm, "VISION_TEST_CENTER"},
        {CameraRole::TopLeft, HikCameraModel::MvCs12010Gm, "VISION_TEST_TOP_LEFT"},
        {CameraRole::TopRight, HikCameraModel::MvCs12010Gm, "VISION_TEST_TOP_RIGHT"},
        {CameraRole::BottomRight, HikCameraModel::MvCs12010Gm, "VISION_TEST_BOTTOM_RIGHT"},
        {CameraRole::BottomLeft, HikCameraModel::MvCs12010Gm, "VISION_TEST_BOTTOM_LEFT"}}};
    config.trigger.mode = vision::hardware::HikTriggerMode::Software;
    config.captureTimeout = std::chrono::milliseconds{100};
    return config;
}

inline vision::hardware::HikLightControllerConfig validHikLightConfiguration() {
    using vision::hardware::HikLightChannelConfig;
    using vision::hardware::HikLightChannelRole;

    vision::hardware::HikLightControllerConfig config;
    config.serialNumber = "VISION_TEST_MISSING_LIGHT_CONTROLLER";
    config.channels = {{
        HikLightChannelConfig{HikLightChannelRole::WhiteBacklight, "White", 0.5},
        HikLightChannelConfig{HikLightChannelRole::CoaxialRed, "Red", 0.5},
        HikLightChannelConfig{HikLightChannelRole::CoaxialGreen, "Green", 0.0},
        HikLightChannelConfig{HikLightChannelRole::CoaxialBlue, "Blue", 0.0}}};
    return config;
}

class RecordingHardwareDiagnostics final : public vision::core::IDiagnosticSink {
public:
    void submit(const vision::core::DiagnosticEvent& event) noexcept override {
        try {
            events.push_back(event);
        } catch (...) {
            discarded = true;
        }
    }

    std::size_t count(const std::string& operation) const {
        return static_cast<std::size_t>(std::count_if(
            events.begin(), events.end(), [&](const auto& event) {
                return event.operation == operation;
            }));
    }

    bool discarded{false};
    std::vector<vision::core::DiagnosticEvent> events;
};

} // namespace vision::test::hardware
