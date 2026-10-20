#pragma once

#include "AppConfig.h"

#include <VisionCore/Error.h>

#include <memory>

namespace spdlog {
class logger;
}

namespace vision::app {

using LoggerOutcome = vision::core::Outcome<
    std::shared_ptr<spdlog::logger>>;

[[nodiscard]] LoggerOutcome createApplicationLogger(
    const LoggingConfig& config);

} // namespace vision::app
