#pragma once

#include <atomic>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <optional>
#include <memory>

namespace vision::test::support {

inline std::optional<std::filesystem::path> environmentDirectory(const char* name) {
#ifdef _WIN32
    char* raw = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&raw, &length, name) != 0)
        throw std::runtime_error("Unable to read test directory environment");
    std::unique_ptr<char, decltype(&std::free)> value(raw, &std::free);
    if (!raw || length <= 1) return std::nullopt;
    return std::filesystem::path(reinterpret_cast<const char8_t*>(raw));
#else
    const char* value = std::getenv(name);
    if (!value || !*value) return std::nullopt;
    return std::filesystem::path(value);
#endif
}

// Allocation uses create_directory atomically. Never remove a pre-existing path.
inline std::filesystem::path uniqueTestDirectory(const std::string& label) {
    static std::atomic<unsigned long long> sequence{0};
    const auto configured = environmentDirectory("VISIONTEST_RESOURCE_ROOT");
    const auto base = configured ? *configured :
        std::filesystem::temp_directory_path() / "QtVisionApp-VisionTest";
    std::filesystem::create_directories(base);
    for (unsigned attempt = 0; attempt != 1000; ++attempt) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto path = base / (label + "-" + std::to_string(stamp) + "-" +
                                 std::to_string(sequence.fetch_add(1)));
        if (std::filesystem::create_directory(path)) return path;
    }
    throw std::runtime_error("Unable to allocate an isolated test directory");
}

class ScopedTestDirectory final {
public:
    explicit ScopedTestDirectory(const char* label) : path_(uniqueTestDirectory(label)) {}
    ~ScopedTestDirectory() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
    ScopedTestDirectory(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};
} // namespace vision::test::support
