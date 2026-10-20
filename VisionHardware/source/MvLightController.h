#pragma once

#include <MvCameraControl.h>

#include <cstdint>

namespace vision::hardware::hik {

class MvLightController final {
public:
    MvLightController() = default;
    ~MvLightController() noexcept;

    MvLightController(const MvLightController&) = delete;
    MvLightController& operator=(const MvLightController&) = delete;
    MvLightController(MvLightController&&) = delete;
    MvLightController& operator=(MvLightController&&) = delete;

    [[nodiscard]] int open(MV_INTERFACE_INFO& interfaceInfo) noexcept;
    [[nodiscard]] int close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept;

    [[nodiscard]] int getEnumValue(
        const char* key,
        MVCC_ENUMVALUE& value) const noexcept;
    [[nodiscard]] int setEnumValueByString(
        const char* key,
        const char* value) noexcept;
    [[nodiscard]] int getFloatValue(
        const char* key,
        MVCC_FLOATVALUE& value) const noexcept;
    [[nodiscard]] int setFloatValue(
        const char* key,
        float value) noexcept;
    [[nodiscard]] int getIntValue(
        const char* key,
        MVCC_INTVALUE_EX& value) const noexcept;
    [[nodiscard]] int setIntValue(
        const char* key,
        std::int64_t value) noexcept;
    [[nodiscard]] int getBoolValue(
        const char* key,
        bool& value) const noexcept;
    [[nodiscard]] int setBoolValue(
        const char* key,
        bool value) noexcept;
    [[nodiscard]] int getNodeAccessMode(
        const char* key,
        MV_XML_AccessMode& mode) const noexcept;

private:
    void* handle_{nullptr};
};

} // namespace vision::hardware::hik
