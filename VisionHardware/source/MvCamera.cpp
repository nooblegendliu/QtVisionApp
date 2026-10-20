#include "MvCamera.h"

#include <limits>

namespace vision::hardware::hik {

MvCamera::~MvCamera() noexcept
{
    if (handle_ != nullptr) {
        static_cast<void>(MV_CC_StopGrabbing(handle_));
        static_cast<void>(close());
    }
}

int MvCamera::open(const MV_CC_DEVICE_INFO& deviceInfo) noexcept
{
    if (handle_ != nullptr) {
        return MV_E_CALLORDER;
    }

    void* newHandle = nullptr;
    int result = MV_CC_CreateHandle(&newHandle, &deviceInfo);
    if (result != MV_OK) {
        return result;
    }

    result = MV_CC_OpenDevice(newHandle, MV_ACCESS_Exclusive, 0);
    if (result != MV_OK) {
        static_cast<void>(MV_CC_DestroyHandle(newHandle));
        return result;
    }

    handle_ = newHandle;
    return MV_OK;
}

int MvCamera::close() noexcept
{
    if (handle_ == nullptr) {
        return MV_OK;
    }

    void* handle = handle_;
    handle_ = nullptr;

    const int closeResult = MV_CC_CloseDevice(handle);
    const int destroyResult = MV_CC_DestroyHandle(handle);
    return closeResult != MV_OK ? closeResult : destroyResult;
}

bool MvCamera::isOpen() const noexcept
{
    return handle_ != nullptr;
}

bool MvCamera::isDeviceConnected() const noexcept
{
    return handle_ != nullptr && MV_CC_IsDeviceConnected(handle_);
}

int MvCamera::startGrabbing() noexcept
{
    return handle_ != nullptr ? MV_CC_StartGrabbing(handle_) : MV_E_HANDLE;
}

int MvCamera::stopGrabbing() noexcept
{
    return handle_ != nullptr ? MV_CC_StopGrabbing(handle_) : MV_E_HANDLE;
}

int MvCamera::clearImageBuffer() noexcept
{
    return handle_ != nullptr ? MV_CC_ClearImageBuffer(handle_) : MV_E_HANDLE;
}

int MvCamera::getImageBuffer(
    MV_FRAME_OUT& frame,
    std::chrono::milliseconds timeout) noexcept
{
    const auto milliseconds = timeout.count();
    if (handle_ == nullptr) {
        return MV_E_HANDLE;
    }
    if (milliseconds <= 0 ||
        static_cast<unsigned long long>(milliseconds) >
            std::numeric_limits<unsigned int>::max()) {
        return MV_E_PARAMETER;
    }

    return MV_CC_GetImageBuffer(
        handle_,
        &frame,
        static_cast<unsigned int>(milliseconds));
}

int MvCamera::freeImageBuffer(MV_FRAME_OUT& frame) noexcept
{
    return handle_ != nullptr
        ? MV_CC_FreeImageBuffer(handle_, &frame)
        : MV_E_HANDLE;
}

int MvCamera::getOptimalPacketSize() const noexcept
{
    return handle_ != nullptr ? MV_CC_GetOptimalPacketSize(handle_) : MV_E_HANDLE;
}

int MvCamera::getIntValue(
    const char* key,
    MVCC_INTVALUE_EX& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetIntValueEx(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvCamera::setIntValue(const char* key, std::int64_t value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetIntValueEx(handle_, key, value)
        : MV_E_HANDLE;
}

int MvCamera::getEnumValue(
    const char* key,
    MVCC_ENUMVALUE& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetEnumValue(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvCamera::setEnumValue(const char* key, unsigned int value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetEnumValue(handle_, key, value)
        : MV_E_HANDLE;
}

int MvCamera::setEnumValueByString(
    const char* key,
    const char* value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetEnumValueByString(handle_, key, value)
        : MV_E_HANDLE;
}

int MvCamera::getFloatValue(
    const char* key,
    MVCC_FLOATVALUE& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetFloatValue(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvCamera::setFloatValue(const char* key, float value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetFloatValue(handle_, key, value)
        : MV_E_HANDLE;
}

int MvCamera::getBoolValue(const char* key, bool& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetBoolValue(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvCamera::setBoolValue(const char* key, bool value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetBoolValue(handle_, key, value)
        : MV_E_HANDLE;
}

int MvCamera::getStringValue(
    const char* key,
    MVCC_STRINGVALUE& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetStringValue(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvCamera::setStringValue(
    const char* key,
    const char* value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetStringValue(handle_, key, value)
        : MV_E_HANDLE;
}

int MvCamera::getNodeAccessMode(
    const char* key,
    MV_XML_AccessMode& mode) const noexcept
{
    return handle_ != nullptr
        ? MV_XML_GetNodeAccessMode(handle_, key, &mode)
        : MV_E_HANDLE;
}

int MvCamera::setGrabStrategy(MV_GRAB_STRATEGY strategy) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetGrabStrategy(handle_, strategy)
        : MV_E_HANDLE;
}

int MvCamera::executeCommand(const char* key) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetCommandValue(handle_, key)
        : MV_E_HANDLE;
}

} // namespace vision::hardware::hik
