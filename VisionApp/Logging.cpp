#include "Logging.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace vision::app {
namespace {

[[nodiscard]] std::optional<spdlog::level::level_enum> parseLevel(
    const std::string& level)
{
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn") return spdlog::level::warn;
    if (level == "error") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    if (level == "off") return spdlog::level::off;
    return std::nullopt;
}

} // namespace

LoggerOutcome createApplicationLogger(const LoggingConfig& config)
{
    try {
        const auto level = parseLevel(config.level);
        if (config.directory.empty() || config.maximumFileBytes == 0 ||
            config.maximumFiles == 0 || !level.has_value()) {
            return vision::core::Error{
                vision::core::ErrorCode::InvalidConfiguration,
                "The logging configuration is invalid.",
                "A directory, known level, and positive rotation limits are required."};
        }
        std::filesystem::create_directories(config.directory);
        const std::filesystem::path logPath =
            config.directory / "vision_app.log";
        auto sink = std::make_shared<
            spdlog::sinks::rotating_file_sink_mt>(
                logPath.string(),
                config.maximumFileBytes,
                config.maximumFiles,
                false);
        auto logger = std::make_shared<spdlog::logger>(
            "vision_app",
            std::move(sink));
        logger->set_level(*level);
        logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] [t:%t] %v");
        logger->flush_on(spdlog::level::warn);
        return logger;
    }
    catch (const std::exception& exception) {
        return vision::core::Error{
            vision::core::ErrorCode::InternalFailure,
            "The application logger could not be initialized.",
            exception.what()};
    }
    catch (...) {
        return vision::core::Error{
            vision::core::ErrorCode::InternalFailure,
            "The application logger could not be initialized.",
            "Unknown logging initialization exception."};
    }
}

} // namespace vision::app
