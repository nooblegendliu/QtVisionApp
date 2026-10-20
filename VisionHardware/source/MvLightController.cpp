#include "MvLightController.h"

namespace vision::hardware::hik {

MvLightController::~MvLightController() noexcept
{
    static_cast<void>(close());
}

int MvLightController::open(MV_INTERFACE_INFO& interfaceInfo) noexcept
{
    if (handle_ != nullptr) {
        return MV_E_CALLORDER;
    }

    void* newHandle = nullptr;
    int result = MV_CC_CreateInterface(&newHandle, &interfaceInfo);
    if (result != MV_OK) {
        return result;
    }

    result = MV_CC_OpenInterface(newHandle, nullptr);
    if (result != MV_OK) {
        static_cast<void>(MV_CC_DestroyInterface(newHandle));
        return result;
    }

    handle_ = newHandle;
    return MV_OK;
}

int MvLightController::close() noexcept
{
    if (handle_ == nullptr) {
        return MV_OK;
    }

    void* handle = handle_;
    handle_ = nullptr;
    const int closeResult = MV_CC_CloseInterface(handle);
    const int destroyResult = MV_CC_DestroyInterface(handle);
    return closeResult != MV_OK ? closeResult : destroyResult;
}

bool MvLightController::isOpen() const noexcept
{
    return handle_ != nullptr;
}

int MvLightController::getEnumValue(
    const char* key,
    MVCC_ENUMVALUE& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetEnumValue(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvLightController::setEnumValueByString(
    const char* key,
    const char* value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetEnumValueByString(handle_, key, value)
        : MV_E_HANDLE;
}

int MvLightController::getFloatValue(
    const char* key,
    MVCC_FLOATVALUE& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetFloatValue(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvLightController::setFloatValue(const char* key, float value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetFloatValue(handle_, key, value)
        : MV_E_HANDLE;
}

int MvLightController::getIntValue(
    const char* key,
    MVCC_INTVALUE_EX& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetIntValueEx(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvLightController::setIntValue(
    const char* key,
    std::int64_t value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetIntValueEx(handle_, key, value)
        : MV_E_HANDLE;
}

int MvLightController::getBoolValue(
    const char* key,
    bool& value) const noexcept
{
    return handle_ != nullptr
        ? MV_CC_GetBoolValue(handle_, key, &value)
        : MV_E_HANDLE;
}

int MvLightController::setBoolValue(const char* key, bool value) noexcept
{
    return handle_ != nullptr
        ? MV_CC_SetBoolValue(handle_, key, value)
        : MV_E_HANDLE;
}

int MvLightController::getNodeAccessMode(
    const char* key,
    MV_XML_AccessMode& mode) const noexcept
{
    return handle_ != nullptr
        ? MV_XML_GetNodeAccessMode(handle_, key, &mode)
        : MV_E_HANDLE;
}

} // namespace vision::hardware::hik
