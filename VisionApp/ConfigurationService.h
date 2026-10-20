#pragma once

#include "AppConfig.h"

#include <cstdint>
#include <filesystem>

namespace vision::app {

class ConfigurationService final {
public:
    ConfigurationService(
        std::filesystem::path path,
        ApplicationConfig activeConfiguration);

    [[nodiscard]] const ApplicationConfig& active() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;

    [[nodiscard]] vision::core::Status validate(
        const ApplicationConfig& candidate) const;
    [[nodiscard]] vision::core::Status commit(
        const ApplicationConfig& candidate);

private:
    std::filesystem::path path_;
    ApplicationConfig active_;
    std::uint64_t revision_{1};
};

} // namespace vision::app
