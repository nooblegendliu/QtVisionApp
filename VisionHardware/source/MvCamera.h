#pragma once

#include <chrono>
#include <cstdint>

#include <MvCameraControl.h>

namespace vision::hardware::hik {

class MvCamera final {
public:
    MvCamera() = default;
    ~MvCamera() noexcept;

    MvCamera(const MvCamera&) = delete;
    MvCamera& operator=(const MvCamera&) = delete;
    MvCamera(MvCamera&&) = delete;
    MvCamera& operator=(MvCamera&&) = delete;

    [[nodiscard]] int open(const MV_CC_DEVICE_INFO& deviceInfo) noexcept;
    [[nodiscard]] int close() noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] bool isDeviceConnected() const noexcept;

    [[nodiscard]] int startGrabbing() noexcept;
    [[nodiscard]] int stopGrabbing() noexcept;
    [[nodiscard]] int clearImageBuffer() noexcept;
    [[nodiscard]] int getImageBuffer(
        MV_FRAME_OUT& frame,
        std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] int freeImageBuffer(MV_FRAME_OUT& frame) noexcept;

    [[nodiscard]] int getOptimalPacketSize() const noexcept;

    [[nodiscard]] int getIntValue(
        const char* key,
        MVCC_INTVALUE_EX& value) const noexcept;
    [[nodiscard]] int setIntValue(
        const char* key,
        std::int64_t value) noexcept;
    [[nodiscard]] int getEnumValue(
        const char* key,
        MVCC_ENUMVALUE& value) const noexcept;
    [[nodiscard]] int setEnumValue(
        const char* key,
        unsigned int value) noexcept;
    [[nodiscard]] int setEnumValueByString(
        const char* key,
        const char* value) noexcept;
    [[nodiscard]] int getFloatValue(
        const char* key,
        MVCC_FLOATVALUE& value) const noexcept;
    [[nodiscard]] int setFloatValue(
        const char* key,
        float value) noexcept;
    [[nodiscard]] int getBoolValue(
        const char* key,
        bool& value) const noexcept;
    [[nodiscard]] int setBoolValue(
        const char* key,
        bool value) noexcept;
    [[nodiscard]] int getStringValue(
        const char* key,
        MVCC_STRINGVALUE& value) const noexcept;
    [[nodiscard]] int setStringValue(
        const char* key,
        const char* value) noexcept;
    [[nodiscard]] int getNodeAccessMode(
        const char* key,
        MV_XML_AccessMode& mode) const noexcept;
    [[nodiscard]] int setGrabStrategy(
        MV_GRAB_STRATEGY strategy) noexcept;
    [[nodiscard]] int executeCommand(const char* key) noexcept;

private:
    void* handle_{nullptr};
};

} // namespace vision::hardware::hik
