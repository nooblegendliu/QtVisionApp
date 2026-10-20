#pragma once

#include "AppConfig.h"
#include "AppTypes.h"

#include <VisionCore/Diagnostics.h>
#include <VisionCore/Ports.h>
#include <VisionHardware/HikHardware.h>

#include <functional>
#include <memory>

namespace vision::app {

using CoreFactoryOutcome = vision::core::Outcome<
    std::unique_ptr<vision::core::VisionCore>>;
using CoreFactory = std::function<CoreFactoryOutcome(
    const VisionRuntimeConfig&,
    vision::hardware::ICaptureObserver*,
    vision::core::IDiagnosticSink*,
    vision::core::ILocateFrameObserver*)>;

[[nodiscard]] vision::hardware::HikCameraSystemConfig
makeCameraSystemConfig(
    const VisionRuntimeConfig& config);

[[nodiscard]] CoreFactory makeProductionCoreFactory();

} // namespace vision::app
