#include <VisionHardware/HikHardware.h>

#include "MvCamera.h"
#include "MvLightController.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace vision::hardware {
namespace {

using vision::core::CameraRole;
using vision::core::DiagnosticEvent;
using vision::core::DiagnosticSeverity;
using vision::core::Error;
using vision::core::ErrorCode;
using vision::core::FrameDiagnosticContext;
using vision::core::ImageFrame;
using vision::core::ImageFrames;
using vision::core::LocateMode;

constexpr std::size_t kCameraCount = 5;
constexpr auto kComponent = "VisionHardware";

struct ModelProfile {
    std::string_view baseName;
    unsigned int width;
    unsigned int height;
};

[[nodiscard]] ModelProfile profileFor(HikCameraModel model) noexcept
{
    switch (model) {
    case HikCameraModel::MvCh25090Gm:
        return {"MV-CH250-90GM", 5120U, 5120U};
    case HikCameraModel::MvCs12010Gm:
        return {"MV-CS120-10GM", 4024U, 3036U};
    }
    return {"unknown", 0U, 0U};
}

[[nodiscard]] std::string_view roleName(CameraRole role) noexcept
{
    switch (role) {
    case CameraRole::Center: return "Center";
    case CameraRole::TopLeft: return "TopLeft";
    case CameraRole::TopRight: return "TopRight";
    case CameraRole::BottomRight: return "BottomRight";
    case CameraRole::BottomLeft: return "BottomLeft";
    case CameraRole::Unknown: return "Unknown";
    }
    return "Unknown";
}

[[nodiscard]] std::optional<std::size_t> roleIndex(CameraRole role) noexcept
{
    switch (role) {
    case CameraRole::Center: return 0U;
    case CameraRole::TopLeft: return 1U;
    case CameraRole::TopRight: return 2U;
    case CameraRole::BottomRight: return 3U;
    case CameraRole::BottomLeft: return 4U;
    case CameraRole::Unknown: return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool isCorner(CameraRole role) noexcept
{
    return role == CameraRole::TopLeft || role == CameraRole::TopRight ||
        role == CameraRole::BottomRight || role == CameraRole::BottomLeft;
}

[[nodiscard]] Error makeError(
    ErrorCode code,
    std::string message,
    std::string diagnostic = {})
{
    return Error{code, std::move(message), std::move(diagnostic)};
}

[[nodiscard]] std::string sdkCodeText(int sdkCode)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex
           << static_cast<unsigned int>(sdkCode);
    return stream.str();
}

template <std::size_t N>
[[nodiscard]] std::string fixedString(const unsigned char (&text)[N])
{
    const auto* begin = reinterpret_cast<const char*>(text);
    const auto* end = std::find(begin, begin + N, '\0');
    return std::string(begin, end);
}

[[nodiscard]] bool matchesModelFamily(
    std::string_view actual,
    std::string_view expected) noexcept
{
    if (actual == expected) {
        return true;
    }
    if (!actual.starts_with(expected)) {
        return false;
    }

    auto suffix = actual.substr(expected.size());
    while (!suffix.empty() && suffix.front() == ' ') {
        suffix.remove_prefix(1);
    }
    if (suffix.empty() || suffix.front() != 'V') {
        return false;
    }
    suffix.remove_prefix(1);
    if (suffix.empty() || !std::isdigit(static_cast<unsigned char>(suffix.front()))) {
        return false;
    }
    return std::all_of(suffix.begin(), suffix.end(), [](char value) {
        return std::isdigit(static_cast<unsigned char>(value)) || value == '.';
    });
}

[[nodiscard]] bool isValidSdkFloat(double value) noexcept
{
    if (!std::isfinite(value) ||
        value < -static_cast<double>(std::numeric_limits<float>::max()) ||
        value > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    const auto converted = static_cast<float>(value);
    return std::isfinite(converted) && (value == 0.0 || converted != 0.0F);
}

[[nodiscard]] std::optional<Error> validateConfig(
    const HikCameraSystemConfig& config,
    const ICaptureObserver* observer)
{
    const auto timeout = config.captureTimeout.count();
    if (timeout < 1 ||
        static_cast<unsigned long long>(timeout) >
            std::numeric_limits<unsigned int>::max()) {
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Camera capture timeout is invalid.",
            "captureTimeout must be in [1ms, UINT_MAX ms]");
    }

    switch (config.trigger.mode) {
    case HikTriggerMode::Software:
        break;
    case HikTriggerMode::Hardware:
        if (observer == nullptr) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Hardware trigger requires a capture observer.",
                "captureObserver is null");
        }
        break;
    default:
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Camera trigger mode is invalid.");
    }

    if (config.trigger.inputLine != HikInputLine::Line0 &&
        config.trigger.inputLine != HikInputLine::Line2) {
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Camera trigger input line is invalid.");
    }
    if (config.trigger.activation != HikTriggerActivation::RisingEdge &&
        config.trigger.activation != HikTriggerActivation::FallingEdge) {
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Camera trigger activation is invalid.");
    }
    if (!isValidSdkFloat(config.trigger.delay.count()) ||
        config.trigger.delay.count() < 0.0) {
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Camera trigger delay is invalid.",
            "trigger delay must be finite, non-negative and representable by the SDK");
    }

    std::array<bool, kCameraCount> roles{};
    std::array<std::string_view, kCameraCount> serials{};
    std::size_t serialCount = 0;

    for (const auto& camera : config.cameras) {
        const auto index = roleIndex(camera.role);
        if (!index.has_value()) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera role is invalid.",
                "role=Unknown");
        }
        if (roles[*index]) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera role is duplicated.",
                "role=" + std::string(roleName(camera.role)));
        }
        roles[*index] = true;

        const bool modelMatches = camera.role == CameraRole::Center
            ? camera.model == HikCameraModel::MvCh25090Gm
            : isCorner(camera.role) &&
                camera.model == HikCameraModel::MvCs12010Gm;
        if (!modelMatches) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera model does not match its role.",
                "role=" + std::string(roleName(camera.role)));
        }

        if (camera.serialNumber.empty()) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera serial number is empty.",
                "role=" + std::string(roleName(camera.role)));
        }
        for (std::size_t i = 0; i < serialCount; ++i) {
            if (serials[i] == camera.serialNumber) {
                return makeError(
                    ErrorCode::InvalidConfiguration,
                    "Camera serial number is duplicated.",
                    "serialNumber=" + camera.serialNumber);
            }
        }
        serials[serialCount++] = camera.serialNumber;

        if (camera.exposureTime.has_value() &&
            (!isValidSdkFloat(camera.exposureTime->count()) ||
             camera.exposureTime->count() <= 0.0)) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera exposure time is invalid.",
                "role=" + std::string(roleName(camera.role)));
        }
        if (camera.gain.has_value() && !isValidSdkFloat(*camera.gain)) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera gain is invalid.",
                "role=" + std::string(roleName(camera.role)));
        }
    }

    if (std::any_of(roles.begin(), roles.end(), [](bool present) {
            return !present;
        })) {
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Five camera roles must all be configured.");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> lightRoleIndex(
    HikLightChannelRole role) noexcept
{
    switch (role) {
    case HikLightChannelRole::WhiteBacklight: return 0U;
    case HikLightChannelRole::CoaxialRed: return 1U;
    case HikLightChannelRole::CoaxialGreen: return 2U;
    case HikLightChannelRole::CoaxialBlue: return 3U;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error> validateLightConfig(
    const HikLightControllerConfig& config)
{
    if (config.serialNumber.empty()) {
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Light controller serial number is empty.");
    }

    std::array<bool, 4> roles{};
    std::array<std::string_view, 4> selectors{};
    std::size_t selectorCount = 0;
    bool anyCoaxialChannelEnabled = false;

    for (const auto& channel : config.channels) {
        const auto index = lightRoleIndex(channel.role);
        if (!index.has_value()) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Light channel role is invalid.");
        }
        if (roles[*index]) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Light channel role is duplicated.");
        }
        roles[*index] = true;

        if (channel.outputSelector.empty()) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Light output selector is empty.");
        }
        for (std::size_t i = 0; i < selectorCount; ++i) {
            if (selectors[i] == channel.outputSelector) {
                return makeError(
                    ErrorCode::InvalidConfiguration,
                    "Light output selector is duplicated.",
                    "outputSelector=" + channel.outputSelector);
            }
        }
        selectors[selectorCount++] = channel.outputSelector;

        if (!std::isfinite(channel.normalizedIntensity) ||
            channel.normalizedIntensity < 0.0 ||
            channel.normalizedIntensity > 1.0) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Light intensity is invalid.",
                "normalizedIntensity must be finite and in [0.0, 1.0]");
        }
        if (channel.role == HikLightChannelRole::WhiteBacklight &&
            channel.normalizedIntensity <= 0.0) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "White backlight intensity must be greater than zero.");
        }
        if (channel.role != HikLightChannelRole::WhiteBacklight &&
            channel.normalizedIntensity > 0.0) {
            anyCoaxialChannelEnabled = true;
        }
    }

    if (std::any_of(roles.begin(), roles.end(), [](bool present) {
            return !present;
        })) {
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Four light channel roles must all be configured.");
    }
    if (!anyCoaxialChannelEnabled) {
        return makeError(
            ErrorCode::InvalidConfiguration,
            "At least one coaxial RGB channel must have non-zero intensity.");
    }
    return std::nullopt;
}

struct HikCameraSubsystem {
    enum class State {
        Initializing,
        Ready,
        Capturing,
        Faulted,
        Stopped
    };

    struct Slot {
        HikCameraDeviceConfig config;
        hik::MvCamera camera;
        MV_CC_DEVICE_INFO deviceInfo{};
        bool hasDeviceInfo{false};
        bool grabbing{false};
        std::string actualModel;
        unsigned int configuredWidth{0};
        unsigned int configuredHeight{0};
    };

    struct TargetSet {
        std::array<Slot*, 4> slots{};
        std::size_t count{0};
    };

    HikCameraSystemConfig config;
    ICaptureObserver* captureObserver{nullptr};
    vision::core::IDiagnosticSink* diagnostics{nullptr};
    std::array<Slot, kCameraCount> slots;
    State state{State::Initializing};
    HikCameraSubsystem(
        HikCameraSystemConfig value,
        ICaptureObserver* observer,
        vision::core::IDiagnosticSink* sink)
        : config(std::move(value)),
          captureObserver(observer),
          diagnostics(sink)
    {
        for (const auto& camera : config.cameras) {
            slots[*roleIndex(camera.role)].config = camera;
        }
    }

    void emit(
        DiagnosticSeverity severity,
        std::string operation,
        std::string message,
        std::vector<FrameDiagnosticContext> frames = {}) const noexcept
    {
        if (diagnostics == nullptr) {
            return;
        }
        try {
            diagnostics->submit(DiagnosticEvent{
                severity,
                kComponent,
                std::move(operation),
                std::move(message),
                std::move(frames)});
        } catch (...) {
        }
    }

    [[nodiscard]] Error sdkError(
        ErrorCode code,
        std::string_view operation,
        const Slot* slot,
        int sdkCode,
        std::string_view extra = {}) const
    {
        std::ostringstream diagnostic;
        diagnostic << "operation=" << operation;
        if (slot != nullptr) {
            diagnostic << ", role=" << roleName(slot->config.role)
                       << ", cameraId=" << slot->config.serialNumber
                       << ", model=" << profileFor(slot->config.model).baseName;
        }
        diagnostic << ", sdkCode=" << sdkCodeText(sdkCode);
        if (!extra.empty()) {
            diagnostic << ", " << extra;
        }
        return makeError(code, "Camera SDK operation failed.", diagnostic.str());
    }

    [[nodiscard]] Error deviceError(
        std::string_view operation,
        const Slot& slot,
        int sdkCode,
        std::string_view extra = {}) const
    {
        const auto code = slot.camera.isDeviceConnected()
            ? ErrorCode::HardwareFailure
            : ErrorCode::HardwareUnavailable;
        return sdkError(code, operation, &slot, sdkCode, extra);
    }

    [[nodiscard]] std::optional<Error> initialize()
    {
        if (auto error = enumerateAndMapAll(); error.has_value()) {
            static_cast<void>(shutdownBestEffort());
            return error;
        }

        for (auto& slot : slots) {
            if (auto error = openAndConfigure(slot); error.has_value()) {
                static_cast<void>(shutdownBestEffort());
                return error;
            }
        }

        state = State::Ready;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> enumerateDevices(
        MV_CC_DEVICE_INFO_LIST& devices) const
    {
        std::memset(&devices, 0, sizeof(devices));
        const int result = MV_CC_EnumDevices(MV_GIGE_DEVICE, &devices);
        if (result != MV_OK) {
            return sdkError(
                ErrorCode::HardwareUnavailable,
                "MV_CC_EnumDevices",
                nullptr,
                result);
        }
        std::ostringstream message;
        message << "Enumerated " << devices.nDeviceNum << " GigE device(s).";
        emit(DiagnosticSeverity::Info, "device.enumerate", message.str());
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> assignDevice(
        Slot& slot,
        const MV_CC_DEVICE_INFO_LIST& devices)
    {
        const MV_CC_DEVICE_INFO* match = nullptr;
        std::string actualModel;

        for (unsigned int i = 0; i < devices.nDeviceNum; ++i) {
            const auto* info = devices.pDeviceInfo[i];
            if (info == nullptr || info->nTLayerType != MV_GIGE_DEVICE) {
                continue;
            }
            const auto& gige = info->SpecialInfo.stGigEInfo;
            if (fixedString(gige.chSerialNumber) == slot.config.serialNumber) {
                if (match != nullptr) {
                    return makeError(
                        ErrorCode::InvalidConfiguration,
                        "Camera serial number matches multiple devices.",
                        "serialNumber=" + slot.config.serialNumber);
                }
                match = info;
                actualModel = fixedString(gige.chModelName);
            }
        }

        if (match == nullptr) {
            return makeError(
                ErrorCode::HardwareUnavailable,
                "Configured camera was not enumerated.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", serialNumber=" + slot.config.serialNumber);
        }

        const auto expected = profileFor(slot.config.model);
        if (!matchesModelFamily(actualModel, expected.baseName)) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Enumerated camera model does not match configuration.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", serialNumber=" + slot.config.serialNumber +
                    ", expectedModel=" + std::string(expected.baseName) +
                    ", actualModel=" + actualModel);
        }

        slot.deviceInfo = *match;
        slot.hasDeviceInfo = true;
        slot.actualModel = std::move(actualModel);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> enumerateAndMapAll()
    {
        MV_CC_DEVICE_INFO_LIST devices{};
        if (auto error = enumerateDevices(devices); error.has_value()) {
            return error;
        }
        for (auto& slot : slots) {
            if (auto error = assignDevice(slot, devices); error.has_value()) {
                return error;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> ensureAccess(
        Slot& slot,
        const char* key,
        bool required,
        MV_XML_AccessMode& access)
    {
        const int result = slot.camera.getNodeAccessMode(key, access);
        if (result != MV_OK) {
            if (!required) {
                emit(
                    DiagnosticSeverity::Warning,
                    "camera.configure",
                    "Optional node access query failed: role=" +
                        std::string(roleName(slot.config.role)) +
                        ", node=" + key + ", sdkCode=" + sdkCodeText(result));
                return std::nullopt;
            }
            return deviceError("MV_XML_GetNodeAccessMode", slot, result, key);
        }
        if (access == AM_NI || access == AM_NA) {
            if (!required) {
                emit(
                    DiagnosticSeverity::Warning,
                    "camera.configure",
                    "Optional node is unavailable: role=" +
                        std::string(roleName(slot.config.role)) +
                        ", node=" + key);
                return std::nullopt;
            }
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera does not provide a required node.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", cameraId=" + slot.config.serialNumber + ", node=" + key);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> setRequiredEnum(
        Slot& slot,
        const char* key,
        const char* value,
        std::optional<unsigned int> expected)
    {
        MV_XML_AccessMode access = AM_NI;
        if (auto error = ensureAccess(slot, key, true, access); error.has_value()) {
            return error;
        }

        if (access == AM_RW) {
            const int result = slot.camera.setEnumValueByString(key, value);
            if (result != MV_OK) {
                return deviceError("MV_CC_SetEnumValueByString", slot, result, key);
            }
        } else if (access != AM_RO) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Required camera node is not writable.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key);
        }

        MVCC_ENUMVALUE actual{};
        const int readResult = slot.camera.getEnumValue(key, actual);
        if (readResult != MV_OK) {
            return deviceError("MV_CC_GetEnumValue", slot, readResult, key);
        }
        if (expected.has_value() && actual.nCurValue != *expected) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera enum node readback does not match configuration.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key + ", expected=" +
                    std::to_string(*expected) + ", actual=" +
                    std::to_string(actual.nCurValue));
        }
        if (access == AM_RO && !expected.has_value()) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Read-only enum node cannot be verified symbolically.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key + ", requiredValue=" + value);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> setRequiredInt(
        Slot& slot,
        const char* key,
        std::int64_t requested)
    {
        MV_XML_AccessMode access = AM_NI;
        if (auto error = ensureAccess(slot, key, true, access); error.has_value()) {
            return error;
        }

        MVCC_INTVALUE_EX details{};
        int result = slot.camera.getIntValue(key, details);
        if (result != MV_OK) {
            return deviceError("MV_CC_GetIntValueEx", slot, result, key);
        }
        const auto increment = std::max<std::int64_t>(details.nInc, 1);
        if (requested < details.nMin || requested > details.nMax ||
            (requested - details.nMin) % increment != 0) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera integer value is outside the node range.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key + ", requested=" +
                    std::to_string(requested));
        }

        if (access == AM_RW) {
            result = slot.camera.setIntValue(key, requested);
            if (result != MV_OK) {
                return deviceError("MV_CC_SetIntValueEx", slot, result, key);
            }
        } else if (access != AM_RO || details.nCurValue != requested) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera integer node cannot satisfy configuration.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key);
        }

        std::memset(&details, 0, sizeof(details));
        result = slot.camera.getIntValue(key, details);
        if (result != MV_OK) {
            return deviceError("MV_CC_GetIntValueEx", slot, result, key);
        }
        if (details.nCurValue != requested) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera integer node readback does not match configuration.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key + ", expected=" +
                    std::to_string(requested) + ", actual=" +
                    std::to_string(details.nCurValue));
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> setRequiredIntToMaximum(
        Slot& slot,
        const char* key,
        unsigned int& actual)
    {
        MV_XML_AccessMode access = AM_NI;
        if (auto error = ensureAccess(slot, key, true, access); error.has_value()) {
            return error;
        }

        MVCC_INTVALUE_EX details{};
        const int result = slot.camera.getIntValue(key, details);
        if (result != MV_OK) {
            return deviceError("MV_CC_GetIntValueEx", slot, result, key);
        }
        if (auto error = setRequiredInt(slot, key, details.nMax); error.has_value()) {
            return error;
        }
        actual = static_cast<unsigned int>(details.nMax);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> setRequiredBool(
        Slot& slot,
        const char* key,
        bool requested)
    {
        MV_XML_AccessMode access = AM_NI;
        if (auto error = ensureAccess(slot, key, true, access); error.has_value()) {
            return error;
        }

        bool actual = false;
        if (access == AM_RW) {
            const int result = slot.camera.setBoolValue(key, requested);
            if (result != MV_OK) {
                return deviceError("MV_CC_SetBoolValue", slot, result, key);
            }
        } else if (access != AM_RO) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Required camera boolean node is not writable.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key);
        }

        const int result = slot.camera.getBoolValue(key, actual);
        if (result != MV_OK) {
            return deviceError("MV_CC_GetBoolValue", slot, result, key);
        }
        if (actual != requested) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera boolean node readback does not match configuration.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> setRequiredFloat(
        Slot& slot,
        const char* key,
        double requested,
        double& actual)
    {
        MV_XML_AccessMode access = AM_NI;
        if (auto error = ensureAccess(slot, key, true, access); error.has_value()) {
            return error;
        }

        MVCC_FLOATVALUE details{};
        int result = slot.camera.getFloatValue(key, details);
        if (result != MV_OK) {
            return deviceError("MV_CC_GetFloatValue", slot, result, key);
        }
        if (requested < details.fMin || requested > details.fMax) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera floating-point value is outside the node range.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key + ", requested=" +
                    std::to_string(requested));
        }

        if (access == AM_RW) {
            result = slot.camera.setFloatValue(key, static_cast<float>(requested));
            if (result != MV_OK) {
                return deviceError("MV_CC_SetFloatValue", slot, result, key);
            }
        } else if (
            access != AM_RO ||
            details.fCurValue != static_cast<float>(requested)) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera floating-point node cannot satisfy configuration.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key);
        }

        std::memset(&details, 0, sizeof(details));
        result = slot.camera.getFloatValue(key, details);
        if (result != MV_OK) {
            return deviceError("MV_CC_GetFloatValue", slot, result, key);
        }
        actual = details.fCurValue;
        if (!std::isfinite(actual) || actual < details.fMin || actual > details.fMax) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera floating-point node readback is invalid.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", node=" + key);
        }
        return std::nullopt;
    }

    void setOptionalBool(Slot& slot, const char* key, bool value)
    {
        MV_XML_AccessMode access = AM_NI;
        const auto accessError = ensureAccess(slot, key, false, access);
        if (accessError.has_value() || access != AM_RW) {
            return;
        }
        const int result = slot.camera.setBoolValue(key, value);
        if (result != MV_OK) {
            emit(
                DiagnosticSeverity::Warning,
                "camera.configure",
                "Optional node write failed: role=" +
                    std::string(roleName(slot.config.role)) + ", node=" + key +
                    ", sdkCode=" + sdkCodeText(result));
        }
    }

    void readOptionalExposureAndGain(Slot& slot)
    {
        auto readEnum = [&](const char* key) {
            MVCC_ENUMVALUE value{};
            const int result = slot.camera.getEnumValue(key, value);
            if (result != MV_OK) {
                emit(
                    DiagnosticSeverity::Warning,
                    "camera.configurationReadback",
                    "Optional enum read failed: role=" +
                        std::string(roleName(slot.config.role)) + ", node=" + key +
                        ", sdkCode=" + sdkCodeText(result));
                return std::optional<unsigned int>{};
            }
            return std::optional<unsigned int>{value.nCurValue};
        };
        auto readFloat = [&](const char* key) {
            MVCC_FLOATVALUE value{};
            const int result = slot.camera.getFloatValue(key, value);
            if (result != MV_OK || !std::isfinite(value.fCurValue)) {
                emit(
                    DiagnosticSeverity::Warning,
                    "camera.configurationReadback",
                    "Optional float read failed: role=" +
                        std::string(roleName(slot.config.role)) + ", node=" + key +
                        ", sdkCode=" + sdkCodeText(result));
                return std::optional<double>{};
            }
            return std::optional<double>{value.fCurValue};
        };

        const auto exposureAuto = readEnum("ExposureAuto");
        const auto exposure = readFloat("ExposureTime");
        const auto gainAuto = readEnum("GainAuto");
        const auto gain = readFloat("Gain");

        std::ostringstream message;
        message << "role=" << roleName(slot.config.role);
        if (exposureAuto.has_value()) message << ", exposureAuto=" << *exposureAuto;
        if (exposure.has_value()) message << ", exposureUs=" << *exposure;
        if (gainAuto.has_value()) message << ", gainAuto=" << *gainAuto;
        if (gain.has_value()) message << ", gain=" << *gain;
        emit(DiagnosticSeverity::Info, "camera.configurationReadback", message.str());

        if (exposure.has_value() &&
            *exposure >= static_cast<double>(config.captureTimeout.count()) * 900.0) {
            emit(
                DiagnosticSeverity::Warning,
                "camera.configurationReadback",
                "Exposure time is close to or greater than capture timeout: role=" +
                    std::string(roleName(slot.config.role)));
        }

        if (slot.config.model == HikCameraModel::MvCs12010Gm) {
            MVCC_ENUMVALUE shutter{};
            const int shutterResult =
                slot.camera.getEnumValue("SensorShutterMode", shutter);
            emit(
                shutterResult == MV_OK ? DiagnosticSeverity::Info
                                       : DiagnosticSeverity::Warning,
                "camera.configurationReadback",
                shutterResult == MV_OK
                    ? "Sensor shutter mode read only: role=" +
                        std::string(roleName(slot.config.role)) + ", value=" +
                        std::to_string(shutter.nCurValue)
                    : "Optional sensor shutter mode read failed: role=" +
                        std::string(roleName(slot.config.role)) +
                        ", sdkCode=" + sdkCodeText(shutterResult));
        }
    }

    [[nodiscard]] std::optional<Error> configureSlot(Slot& slot)
    {
        if (slot.grabbing) {
            return makeError(
                ErrorCode::HardwareFailure,
                "Camera cannot be configured while grabbing.",
                "role=" + std::string(roleName(slot.config.role)));
        }

        if (auto error = setRequiredEnum(
                slot, "TriggerMode", "Off", MV_TRIGGER_MODE_OFF);
            error.has_value()) {
            return error;
        }

        const int packetSize = slot.camera.getOptimalPacketSize();
        if (packetSize <= 0) {
            return deviceError(
                "MV_CC_GetOptimalPacketSize", slot, packetSize);
        }
        if (auto error = setRequiredInt(
                slot, "GevSCPSPacketSize", packetSize);
            error.has_value()) {
            return error;
        }

        constexpr std::array<std::pair<const char*, std::int64_t>, 6> geometryDefaults{{
            {"OffsetX", 0},
            {"OffsetY", 0},
            {"BinningHorizontal", 1},
            {"BinningVertical", 1},
            {"DecimationHorizontal", 1},
            {"DecimationVertical", 1}}};
        for (const auto& [key, value] : geometryDefaults) {
            if (auto error = setRequiredInt(slot, key, value); error.has_value()) {
                return error;
            }
        }
        if (auto error = setRequiredInt(slot, "OffsetX", 0); error.has_value()) {
            return error;
        }
        if (auto error = setRequiredInt(slot, "OffsetY", 0); error.has_value()) {
            return error;
        }

        if (auto error = setRequiredIntToMaximum(
                slot, "Width", slot.configuredWidth);
            error.has_value()) {
            return error;
        }
        if (auto error = setRequiredIntToMaximum(
                slot, "Height", slot.configuredHeight);
            error.has_value()) {
            return error;
        }
        const auto profile = profileFor(slot.config.model);
        if (slot.configuredWidth != profile.width ||
            slot.configuredHeight != profile.height) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Camera full-frame dimensions do not match the configured model.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", expected=" + std::to_string(profile.width) + "x" +
                    std::to_string(profile.height) + ", actual=" +
                    std::to_string(slot.configuredWidth) + "x" +
                    std::to_string(slot.configuredHeight));
        }

        if (auto error = setRequiredBool(slot, "ReverseX", false); error.has_value()) {
            return error;
        }
        if (auto error = setRequiredBool(slot, "ReverseY", false); error.has_value()) {
            return error;
        }
        if (auto error = setRequiredEnum(
                slot, "PixelFormat", "Mono8", PixelType_Gvsp_Mono8);
            error.has_value()) {
            return error;
        }

        double actualExposure = 0.0;
        if (slot.config.exposureTime.has_value()) {
            if (auto error = setRequiredEnum(
                    slot, "ExposureAuto", "Off", MV_EXPOSURE_AUTO_MODE_OFF);
                error.has_value()) {
                return error;
            }
            if (auto error = setRequiredFloat(
                    slot,
                    "ExposureTime",
                    slot.config.exposureTime->count(),
                    actualExposure);
                error.has_value()) {
                return error;
            }
        }

        double actualGain = 0.0;
        if (slot.config.gain.has_value()) {
            if (auto error = setRequiredEnum(
                    slot, "GainAuto", "Off", MV_GAIN_MODE_OFF);
                error.has_value()) {
                return error;
            }
            if (auto error = setRequiredFloat(
                    slot, "Gain", *slot.config.gain, actualGain);
                error.has_value()) {
                return error;
            }
        }

        if (auto error = setRequiredEnum(
                slot,
                "AcquisitionMode",
                "Continuous",
                MV_ACQ_MODE_CONTINUOUS);
            error.has_value()) {
            return error;
        }
        if (auto error = setRequiredEnum(
                slot, "TriggerSelector", "FrameStart", 0U);
            error.has_value()) {
            return error;
        }

        std::optional<double> actualTriggerDelay;
        if (config.trigger.mode == HikTriggerMode::Software) {
            if (auto error = setRequiredEnum(
                    slot,
                    "TriggerSource",
                    "Software",
                    MV_TRIGGER_SOURCE_SOFTWARE);
                error.has_value()) {
                return error;
            }
        } else {
            const bool line0 = config.trigger.inputLine == HikInputLine::Line0;
            if (auto error = setRequiredEnum(
                    slot,
                    "TriggerSource",
                    line0 ? "Line0" : "Line2",
                    line0 ? MV_TRIGGER_SOURCE_LINE0 : MV_TRIGGER_SOURCE_LINE2);
                error.has_value()) {
                return error;
            }
            if (!line0) {
                if (auto error = setRequiredEnum(
                        slot, "LineSelector", "Line2", 2U);
                    error.has_value()) {
                    return error;
                }
                if (auto error = setRequiredEnum(
                        slot, "LineMode", "Input", 0U);
                    error.has_value()) {
                    return error;
                }
            }

            const bool rising =
                config.trigger.activation == HikTriggerActivation::RisingEdge;
            if (auto error = setRequiredEnum(
                    slot,
                    "TriggerActivation",
                    rising ? "RisingEdge" : "FallingEdge",
                    rising ? 0U : 1U);
                error.has_value()) {
                return error;
            }
            double actualDelay = 0.0;
            if (auto error = setRequiredFloat(
                    slot,
                    "TriggerDelay",
                    config.trigger.delay.count(),
                    actualDelay);
                error.has_value()) {
                return error;
            }
            actualTriggerDelay = actualDelay;
        }

        setOptionalBool(slot, "TriggerCacheEnable", false);
        setOptionalBool(slot, "AcquisitionFrameRateEnable", false);

        const int strategyResult =
            slot.camera.setGrabStrategy(MV_GrabStrategy_OneByOne);
        if (strategyResult != MV_OK) {
            return deviceError(
                "MV_CC_SetGrabStrategy", slot, strategyResult);
        }

        if (auto error = setRequiredEnum(
                slot, "TriggerMode", "On", MV_TRIGGER_MODE_ON);
            error.has_value()) {
            return error;
        }

        readOptionalExposureAndGain(slot);
        std::ostringstream message;
        message << "role=" << roleName(slot.config.role)
                << ", cameraId=" << slot.config.serialNumber
                << ", model=" << slot.actualModel
                << ", dimensions=" << slot.configuredWidth << 'x'
                << slot.configuredHeight << ", pixelFormat=Mono8"
                << ", packetSize=" << packetSize
                << ", trigger="
                << (config.trigger.mode == HikTriggerMode::Software
                        ? "Software"
                        : "Hardware");
        if (slot.config.exposureTime.has_value()) {
            message << ", requestedExposureUs="
                    << slot.config.exposureTime->count()
                    << ", actualExposureUs=" << actualExposure;
        }
        if (slot.config.gain.has_value()) {
            message << ", requestedGain=" << *slot.config.gain
                    << ", actualGain=" << actualGain;
        }
        if (actualTriggerDelay.has_value()) {
            message << ", triggerLine="
                    << (config.trigger.inputLine == HikInputLine::Line0
                            ? "Line0"
                            : "Line2")
                    << ", triggerActivation="
                    << (config.trigger.activation ==
                                HikTriggerActivation::RisingEdge
                            ? "RisingEdge"
                            : "FallingEdge")
                    << ", requestedTriggerDelayUs="
                    << config.trigger.delay.count()
                    << ", actualTriggerDelayUs=" << *actualTriggerDelay;
        }
        emit(DiagnosticSeverity::Info, "camera.configure", message.str());
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> openAndConfigure(Slot& slot)
    {
        if (!slot.hasDeviceInfo) {
            return makeError(
                ErrorCode::HardwareUnavailable,
                "Camera device information is unavailable.",
                "role=" + std::string(roleName(slot.config.role)));
        }

        const int openResult = slot.camera.open(slot.deviceInfo);
        if (openResult != MV_OK) {
            return sdkError(
                ErrorCode::HardwareUnavailable,
                "MV_CC_OpenDevice",
                &slot,
                openResult);
        }
        emit(
            DiagnosticSeverity::Info,
            "camera.open",
            "Opened camera: role=" + std::string(roleName(slot.config.role)) +
                ", cameraId=" + slot.config.serialNumber);

        if (auto error = configureSlot(slot); error.has_value()) {
            static_cast<void>(slot.camera.close());
            return error;
        }
        return std::nullopt;
    }

    [[nodiscard]] TargetSet targetsFor(LocateMode mode)
    {
        TargetSet target;
        if (mode == LocateMode::Edge) {
            target.slots[0] = &slots[0];
            target.count = 1;
        } else if (mode == LocateMode::MarkPoint) {
            target.slots[0] = &slots[1];
            target.slots[1] = &slots[2];
            target.slots[2] = &slots[3];
            target.slots[3] = &slots[4];
            target.count = 4;
        }
        return target;
    }

    [[nodiscard]] std::optional<Error> stopTargets(TargetSet target)
    {
        std::optional<Error> firstError;
        for (std::size_t i = target.count; i > 0; --i) {
            auto& slot = *target.slots[i - 1];
            if (!slot.grabbing) {
                continue;
            }
            const int result = slot.camera.stopGrabbing();
            slot.grabbing = false;
            if (result != MV_OK && !firstError.has_value()) {
                firstError = deviceError("MV_CC_StopGrabbing", slot, result);
            }
        }
        if (firstError.has_value()) {
            emit(
                DiagnosticSeverity::Error,
                "capture.cleanup",
                firstError->diagnostic);
        }
        return firstError;
    }

    void abortCaptureNoThrow() noexcept
    {
        for (auto& slot : slots) {
            if (slot.grabbing) {
                static_cast<void>(slot.camera.stopGrabbing());
                slot.grabbing = false;
            }
        }
        state = State::Faulted;
    }

    [[nodiscard]] std::chrono::milliseconds remainingTimeout(
        std::chrono::steady_clock::time_point deadline) const noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::chrono::milliseconds{0};
        }
        const auto remaining = deadline - now;
        auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        if (milliseconds < remaining) {
            ++milliseconds;
        }
        return std::max(milliseconds, std::chrono::milliseconds{1});
    }

    [[nodiscard]] std::optional<Error> receiveFrame(
        Slot& slot,
        std::chrono::steady_clock::time_point deadline,
        ImageFrames& images)
    {
        const auto remaining = remainingTimeout(deadline);
        if (remaining.count() <= 0) {
            emit(
                DiagnosticSeverity::Warning,
                "capture.timeout",
                "Capture deadline expired before requesting frame: role=" +
                    std::string(roleName(slot.config.role)));
            return makeError(
                ErrorCode::HardwareTimeout,
                "Camera capture timed out.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", cameraId=" + slot.config.serialNumber);
        }

        MV_FRAME_OUT sdkFrame{};
        const int getResult = slot.camera.getImageBuffer(sdkFrame, remaining);
        if (getResult != MV_OK) {
            if (getResult == MV_E_NODATA) {
                emit(
                    DiagnosticSeverity::Warning,
                    "capture.timeout",
                    "Camera frame wait timed out: role=" +
                        std::string(roleName(slot.config.role)));
                return sdkError(
                    ErrorCode::HardwareTimeout,
                    "MV_CC_GetImageBuffer",
                    &slot,
                    getResult);
            }
            return deviceError("MV_CC_GetImageBuffer", slot, getResult);
        }
        const auto receivedAt = std::chrono::steady_clock::now();

        std::optional<Error> frameError;
        const auto profile = profileFor(slot.config.model);
        const auto expectedLength =
            static_cast<std::uint64_t>(profile.width) * profile.height;
        if (sdkFrame.pBufAddr == nullptr ||
            sdkFrame.stFrameInfo.enPixelType != PixelType_Gvsp_Mono8 ||
            sdkFrame.stFrameInfo.nWidth != profile.width ||
            sdkFrame.stFrameInfo.nHeight != profile.height ||
            sdkFrame.stFrameInfo.nFrameLen < expectedLength) {
            frameError = makeError(
                ErrorCode::InvalidImage,
                "Camera returned an invalid image buffer.",
                "role=" + std::string(roleName(slot.config.role)) +
                    ", cameraId=" + slot.config.serialNumber +
                    ", width=" + std::to_string(sdkFrame.stFrameInfo.nWidth) +
                    ", height=" + std::to_string(sdkFrame.stFrameInfo.nHeight) +
                    ", frameLength=" +
                    std::to_string(sdkFrame.stFrameInfo.nFrameLen));
        }

        ImageFrame image;
        if (!frameError.has_value()) {
            try {
                const cv::Mat view(
                    static_cast<int>(profile.height),
                    static_cast<int>(profile.width),
                    CV_8UC1,
                    sdkFrame.pBufAddr);
                image.image = view.clone();
                if (image.image.empty() || !image.image.isContinuous()) {
                    frameError = makeError(
                        ErrorCode::InvalidImage,
                        "Camera image could not be copied to independent storage.",
                        "role=" + std::string(roleName(slot.config.role)));
                } else {
                    image.capturedAt = receivedAt;
                    image.frameNumber = sdkFrame.stFrameInfo.nFrameNum;
                    image.cameraId = slot.config.serialNumber;
                    image.cameraRole = slot.config.role;
                }
            } catch (const cv::Exception& exception) {
                frameError = makeError(
                    ErrorCode::InternalFailure,
                    "Camera image copy failed.",
                    "role=" + std::string(roleName(slot.config.role)) +
                        ", cvError=" + exception.what());
            } catch (const std::exception& exception) {
                frameError = makeError(
                    ErrorCode::InternalFailure,
                    "Camera image copy failed.",
                    "role=" + std::string(roleName(slot.config.role)) +
                        ", exception=" + exception.what());
            } catch (...) {
                frameError = makeError(
                    ErrorCode::InternalFailure,
                    "Camera image copy failed.",
                    "role=" + std::string(roleName(slot.config.role)));
            }
        }

        const int freeResult = slot.camera.freeImageBuffer(sdkFrame);
        if (freeResult != MV_OK) {
            return deviceError("MV_CC_FreeImageBuffer", slot, freeResult);
        }
        if (frameError.has_value()) {
            return frameError;
        }

        FrameDiagnosticContext frameContext{
            image.capturedAt,
            image.frameNumber,
            image.cameraId,
            image.cameraRole};
        images.frames.push_back(std::move(image));
        emit(
            DiagnosticSeverity::Info,
            "capture.frameReceived",
            "Received frame: role=" +
                std::string(roleName(frameContext.cameraRole)),
            {std::move(frameContext)});
        return std::nullopt;
    }

    [[nodiscard]] vision::core::CaptureOutcome capture(LocateMode mode)
    {
        if (state == State::Capturing) {
            return makeError(ErrorCode::Busy, "Camera capture is already running.");
        }
        if (state != State::Ready) {
            return makeError(ErrorCode::NotReady, "Camera system is not ready.");
        }
        const auto targets = targetsFor(mode);
        if (targets.count == 0) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Locate mode is invalid for camera capture.");
        }

        state = State::Capturing;
        std::optional<Error> primaryError;
        ImageFrames images;
        images.frames.reserve(targets.count);

        for (std::size_t i = 0; i < targets.count; ++i) {
            auto& slot = *targets.slots[i];
            if (!slot.camera.isOpen() || !slot.camera.isDeviceConnected()) {
                primaryError = makeError(
                    ErrorCode::HardwareUnavailable,
                    "Target camera is unavailable.",
                    "role=" + std::string(roleName(slot.config.role)) +
                        ", cameraId=" + slot.config.serialNumber);
                break;
            }
            if (slot.grabbing) {
                primaryError = makeError(
                    ErrorCode::HardwareFailure,
                    "Target camera has residual grabbing state.",
                    "role=" + std::string(roleName(slot.config.role)));
                break;
            }
            const int result = slot.camera.startGrabbing();
            if (result != MV_OK) {
                primaryError = deviceError("MV_CC_StartGrabbing", slot, result);
                break;
            }
            slot.grabbing = true;
        }

        if (!primaryError.has_value()) {
            for (std::size_t i = 0; i < targets.count; ++i) {
                auto& slot = *targets.slots[i];
                const int result = slot.camera.clearImageBuffer();
                if (result != MV_OK) {
                    primaryError = deviceError("MV_CC_ClearImageBuffer", slot, result);
                    break;
                }
            }
        }
        emit(
            primaryError.has_value() ? DiagnosticSeverity::Error
                                     : DiagnosticSeverity::Info,
            "capture.start",
            primaryError.has_value()
                ? primaryError->diagnostic
                : "All target cameras started and buffers cleared.");

        std::chrono::steady_clock::time_point deadline{};
        if (!primaryError.has_value() &&
            config.trigger.mode == HikTriggerMode::Hardware) {
            captureObserver->onPrepared(mode);
            emit(
                DiagnosticSeverity::Info,
                "capture.prepared",
                "All target cameras are prepared for one external trigger.");
            deadline = std::chrono::steady_clock::now() + config.captureTimeout;
        }

        if (!primaryError.has_value() &&
            config.trigger.mode == HikTriggerMode::Software) {
            deadline = std::chrono::steady_clock::now() + config.captureTimeout;
            for (std::size_t i = 0; i < targets.count; ++i) {
                auto& slot = *targets.slots[i];
                const int result = slot.camera.executeCommand("TriggerSoftware");
                if (result != MV_OK) {
                    primaryError = deviceError(
                        "MV_CC_SetCommandValue(TriggerSoftware)", slot, result);
                    break;
                }
                emit(
                    DiagnosticSeverity::Info,
                    "capture.softwareTrigger",
                    "Software trigger sent: role=" +
                        std::string(roleName(slot.config.role)));
            }
        }

        if (!primaryError.has_value()) {
            for (std::size_t i = 0; i < targets.count; ++i) {
                if (auto error = receiveFrame(
                        *targets.slots[i], deadline, images);
                    error.has_value()) {
                    primaryError = std::move(error);
                    images.frames.clear();
                    break;
                }
            }
        }

        const auto cleanupError = stopTargets(targets);
        if (cleanupError.has_value()) {
            Error finalError = *cleanupError;
            if (primaryError.has_value()) {
                finalError.diagnostic += "; originalError={" +
                    primaryError->diagnostic + "}";
            }
            state = State::Faulted;
            return finalError;
        }
        if (primaryError.has_value()) {
            state = State::Faulted;
            return *primaryError;
        }

        state = State::Ready;
        return images;
    }

    [[nodiscard]] std::optional<Error> refreshDisconnectedSlots()
    {
        bool needsEnumeration = false;
        for (auto& slot : slots) {
            if (!slot.camera.isOpen() || !slot.camera.isDeviceConnected()) {
                static_cast<void>(slot.camera.close());
                slot.hasDeviceInfo = false;
                needsEnumeration = true;
            }
        }
        if (!needsEnumeration) {
            return std::nullopt;
        }

        MV_CC_DEVICE_INFO_LIST devices{};
        if (auto error = enumerateDevices(devices); error.has_value()) {
            return error;
        }
        for (auto& slot : slots) {
            if (!slot.hasDeviceInfo) {
                if (auto error = assignDevice(slot, devices); error.has_value()) {
                    return error;
                }
                if (auto error = openAndConfigure(slot); error.has_value()) {
                    return error;
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> exerciseSlot(Slot& slot)
    {
        const int startResult = slot.camera.startGrabbing();
        if (startResult != MV_OK) {
            return deviceError("MV_CC_StartGrabbing(recover)", slot, startResult);
        }
        slot.grabbing = true;

        std::optional<Error> error;
        const int clearResult = slot.camera.clearImageBuffer();
        if (clearResult != MV_OK) {
            error = deviceError("MV_CC_ClearImageBuffer(recover)", slot, clearResult);
        }
        const int stopResult = slot.camera.stopGrabbing();
        slot.grabbing = false;
        if (stopResult != MV_OK) {
            auto stopError = deviceError(
                "MV_CC_StopGrabbing(recover)", slot, stopResult);
            if (error.has_value()) {
                stopError.diagnostic += "; originalError={" + error->diagnostic + "}";
            }
            return stopError;
        }
        return error;
    }

    [[nodiscard]] vision::core::Status recover()
    {
        if (state == State::Capturing) {
            return makeError(ErrorCode::Busy, "Camera capture is running.");
        }
        if (state == State::Stopped || state == State::Initializing) {
            return makeError(ErrorCode::NotReady, "Camera system cannot recover now.");
        }

        state = State::Faulted;
        std::optional<Error> firstError;
        for (auto& slot : slots) {
            if (!slot.grabbing) continue;
            const int result = slot.camera.stopGrabbing();
            slot.grabbing = false;
            if (result != MV_OK && !firstError.has_value()) {
                firstError = deviceError("MV_CC_StopGrabbing(recover)", slot, result);
            }
        }

        const auto refreshError = refreshDisconnectedSlots();
        if (refreshError.has_value() && !firstError.has_value()) {
            firstError = refreshError;
        }
        if (!refreshError.has_value()) {
            for (auto& slot : slots) {
                if (auto error = exerciseSlot(slot); error.has_value()) {
                    if (!firstError.has_value()) {
                        firstError = std::move(error);
                    }
                }
            }
        }

        if (firstError.has_value()) {
            emit(DiagnosticSeverity::Error, "camera.recover", firstError->diagnostic);
            return *firstError;
        }
        state = State::Ready;
        emit(DiagnosticSeverity::Info, "camera.recover", "Camera system recovered.");
        return std::monostate{};
    }

    [[nodiscard]] std::optional<Error> shutdownBestEffort()
    {
        std::optional<Error> firstError;
        for (auto& slot : slots) {
            if (slot.grabbing) {
                const int result = slot.camera.stopGrabbing();
                slot.grabbing = false;
                if (result != MV_OK && !firstError.has_value()) {
                    firstError = deviceError("MV_CC_StopGrabbing(shutdown)", slot, result);
                }
            }
        }
        for (auto& slot : slots) {
            if (!slot.camera.isOpen()) continue;
            const int result = slot.camera.close();
            emit(
                result == MV_OK ? DiagnosticSeverity::Info
                                : DiagnosticSeverity::Error,
                "camera.close",
                result == MV_OK
                    ? "Closed camera: role=" +
                        std::string(roleName(slot.config.role))
                    : "Camera close failed: role=" +
                        std::string(roleName(slot.config.role)) +
                        ", sdkCode=" + sdkCodeText(result));
            if (result != MV_OK && !firstError.has_value()) {
                firstError = sdkError(
                    ErrorCode::HardwareFailure,
                    "MV_CC_CloseDevice",
                    &slot,
                    result);
            }
        }
        state = State::Stopped;
        return firstError;
    }

    [[nodiscard]] vision::core::Status shutdown()
    {
        if (state == State::Stopped) {
            return std::monostate{};
        }
        if (auto error = shutdownBestEffort(); error.has_value()) {
            return *error;
        }
        return std::monostate{};
    }
};

void appendCleanupError(Error& primary, const Error& cleanup)
{
    primary.diagnostic += primary.diagnostic.empty() ? "" : "; ";
    primary.diagnostic += "cleanupError={" + cleanup.diagnostic + "}";
}

struct HikHardwareState final {
    enum class Lifecycle {
        Initializing,
        Ready,
        Faulted,
        ShuttingDown,
        Stopped
    };

    HikCameraSystemConfig cameraConfig;
    HikLightControllerConfig lightConfig;
    ICaptureObserver* captureObserver{nullptr};
    vision::core::IDiagnosticSink* diagnostics{nullptr};
    std::unique_ptr<HikCameraSubsystem> camera;
    hik::MvLightController lightController;
    Lifecycle lifecycle{Lifecycle::Initializing};
    bool sdkInitialized{false};
    bool cameraStopped{false};
    bool lightStopped{false};

    HikHardwareState(
        HikCameraSystemConfig cameraValue,
        HikLightControllerConfig lightValue,
        ICaptureObserver* observer,
        vision::core::IDiagnosticSink* sink)
        : cameraConfig(std::move(cameraValue)),
          lightConfig(std::move(lightValue)),
          captureObserver(observer),
          diagnostics(sink)
    {
    }

    ~HikHardwareState() noexcept
    {
        try {
            static_cast<void>(shutdownAll());
        } catch (...) {
        }
    }

    void emit(
        DiagnosticSeverity severity,
        std::string operation,
        std::string message) const noexcept
    {
        if (diagnostics == nullptr) {
            return;
        }
        try {
            diagnostics->submit(DiagnosticEvent{
                severity,
                kComponent,
                std::move(operation),
                std::move(message),
                {}});
        } catch (...) {
        }
    }

    [[nodiscard]] Error hardwareError(
        ErrorCode code,
        std::string_view operation,
        int sdkCode,
        std::string_view extra = {}) const
    {
        std::ostringstream diagnostic;
        diagnostic << "operation=" << operation
                   << ", controllerSerial=" << lightConfig.serialNumber
                   << ", sdkCode=" << sdkCodeText(sdkCode);
        if (!extra.empty()) {
            diagnostic << ", " << extra;
        }
        return makeError(
            code,
            "Hardware initialization failed.",
            diagnostic.str());
    }

    [[nodiscard]] std::optional<Error> initializeSdk()
    {
        const int result = MV_CC_Initialize();
        if (result != MV_OK) {
            lifecycle = Lifecycle::Stopped;
            return hardwareError(
                ErrorCode::HardwareFailure,
                "MV_CC_Initialize",
                result);
        }
        sdkInitialized = true;
        emit(DiagnosticSeverity::Info, "sdk.initialize", "MVS SDK initialized.");

        std::ostringstream version;
        version << "MVS SDK version=0x" << std::uppercase << std::hex
                << MV_CC_GetSDKVersion();
        emit(DiagnosticSeverity::Info, "sdk.version", version.str());
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Error> enumerateLightController()
    {
        MV_INTERFACE_INFO_LIST interfaces{};
        const int result = MV_CC_EnumInterfaces(MV_LC_INTERFACE, &interfaces);
        if (result != MV_OK) {
            return hardwareError(
                ErrorCode::HardwareUnavailable,
                "MV_CC_EnumInterfaces",
                result,
                "interfaceType=MV_LC_INTERFACE");
        }

        {
            std::ostringstream message;
            message << "Enumerated " << interfaces.nInterfaceNum
                    << " light controller interface(s).";
            emit(DiagnosticSeverity::Info, "light.enumerate", message.str());
        }

        const MV_INTERFACE_INFO* matched = nullptr;
        for (unsigned int i = 0; i < interfaces.nInterfaceNum; ++i) {
            const auto* candidate = interfaces.pInterfaceInfos[i];
            if (candidate != nullptr &&
                fixedString(candidate->chSerialNumber) == lightConfig.serialNumber) {
                matched = candidate;
                break;
            }
        }

        if (matched == nullptr) {
            return makeError(
                ErrorCode::HardwareUnavailable,
                "Configured light controller is unavailable.",
                "operation=MV_CC_EnumInterfaces, controllerSerial=" +
                    lightConfig.serialNumber +
                    ", interfaceType=MV_LC_INTERFACE");
        }

        std::ostringstream diagnostic;
        diagnostic << "controllerSerial=" << lightConfig.serialNumber
                   << ", model=" << fixedString(matched->chModelName)
                   << ", displayName=" << fixedString(matched->chDisplayName)
                   << ", deviceVersion=" << fixedString(matched->chDeviceVersion)
                   << ", required output selector, output switch and intensity "
                      "node profile has not been verified from the target device";
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Light controller node profile is not verified.",
            diagnostic.str());
    }

    [[nodiscard]] std::optional<Error> initialize()
    {
        if (auto error = initializeSdk(); error.has_value()) {
            return error;
        }
        if (auto error = enumerateLightController(); error.has_value()) {
            return error;
        }

        // This becomes reachable only after the target controller's real node
        // profile is recorded and implemented. Until then enumeration above
        // deliberately returns a stable error before any output is written.
        camera = std::make_unique<HikCameraSubsystem>(
            std::move(cameraConfig), captureObserver, diagnostics);
        if (auto error = camera->initialize(); error.has_value()) {
            return error;
        }
        lifecycle = Lifecycle::Ready;
        return std::nullopt;
    }

    [[nodiscard]] vision::core::CaptureOutcome capture(LocateMode mode)
    {
        if (lifecycle != Lifecycle::Ready || !camera || cameraStopped) {
            return makeError(
                ErrorCode::NotReady,
                "Camera port is not ready.");
        }
        try {
            return camera->capture(mode);
        } catch (const std::exception& exception) {
            camera->abortCaptureNoThrow();
            lifecycle = Lifecycle::Faulted;
            return makeError(
                ErrorCode::InternalFailure,
                "Camera capture failed unexpectedly.",
                exception.what());
        } catch (...) {
            camera->abortCaptureNoThrow();
            lifecycle = Lifecycle::Faulted;
            return makeError(
                ErrorCode::InternalFailure,
                "Camera capture failed unexpectedly.");
        }
    }

    [[nodiscard]] vision::core::Status recoverCamera()
    {
        if (lifecycle == Lifecycle::ShuttingDown ||
            lifecycle == Lifecycle::Stopped || !camera || cameraStopped) {
            return makeError(ErrorCode::NotReady, "Camera port cannot recover now.");
        }
        try {
            auto status = camera->recover();
            if (std::holds_alternative<Error>(status)) {
                lifecycle = Lifecycle::Faulted;
            }
            return status;
        } catch (const std::exception& exception) {
            lifecycle = Lifecycle::Faulted;
            return makeError(
                ErrorCode::InternalFailure,
                "Camera recovery failed unexpectedly.",
                exception.what());
        } catch (...) {
            lifecycle = Lifecycle::Faulted;
            return makeError(
                ErrorCode::InternalFailure,
                "Camera recovery failed unexpectedly.");
        }
    }

    [[nodiscard]] vision::core::Status unsupportedLightOperation(
        std::string_view operation)
    {
        lifecycle = Lifecycle::Faulted;
        return makeError(
            ErrorCode::InvalidConfiguration,
            "Light controller node profile is not verified.",
            "operation=" + std::string(operation) +
                ", controllerSerial=" + lightConfig.serialNumber);
    }

    [[nodiscard]] vision::core::Status turnOn(LocateMode mode)
    {
        if (mode != LocateMode::Edge && mode != LocateMode::MarkPoint) {
            return makeError(
                ErrorCode::InvalidConfiguration,
                "Locate mode is invalid for light control.");
        }
        if (lifecycle != Lifecycle::Ready || lightStopped) {
            return makeError(ErrorCode::NotReady, "Light port is not ready.");
        }
        return unsupportedLightOperation("light.turnOn");
    }

    [[nodiscard]] vision::core::Status turnOff()
    {
        if (lifecycle == Lifecycle::Stopped || lightStopped) {
            return std::monostate{};
        }
        if (!lightController.isOpen()) {
            return std::monostate{};
        }
        return unsupportedLightOperation("light.turnOff");
    }

    [[nodiscard]] vision::core::Status recoverLight()
    {
        if (lifecycle == Lifecycle::ShuttingDown ||
            lifecycle == Lifecycle::Stopped || lightStopped) {
            return makeError(ErrorCode::NotReady, "Light port cannot recover now.");
        }
        return unsupportedLightOperation("light.recover");
    }

    [[nodiscard]] std::optional<Error> finalizeSdk()
    {
        if (!sdkInitialized) {
            return std::nullopt;
        }
        const int result = MV_CC_Finalize();
        sdkInitialized = false;
        emit(
            result == MV_OK ? DiagnosticSeverity::Info
                            : DiagnosticSeverity::Error,
            "sdk.finalize",
            result == MV_OK
                ? "MVS SDK finalized."
                : "MVS SDK finalize failed: sdkCode=" + sdkCodeText(result));
        if (result != MV_OK) {
            return hardwareError(
                ErrorCode::HardwareFailure,
                "MV_CC_Finalize",
                result);
        }
        return std::nullopt;
    }

    [[nodiscard]] vision::core::Status finishShutdownIfPossible()
    {
        if (!cameraStopped || !lightStopped) {
            return std::monostate{};
        }
        if (auto error = finalizeSdk(); error.has_value()) {
            lifecycle = Lifecycle::Stopped;
            return *error;
        }
        lifecycle = Lifecycle::Stopped;
        return std::monostate{};
    }

    [[nodiscard]] vision::core::Status shutdownCamera()
    {
        if (cameraStopped) {
            return finishShutdownIfPossible();
        }
        lifecycle = Lifecycle::ShuttingDown;
        std::optional<Error> firstError;
        if (camera) {
            try {
                auto status = camera->shutdown();
                if (const auto* error = std::get_if<Error>(&status)) {
                    firstError = *error;
                }
            } catch (const std::exception& exception) {
                firstError = makeError(
                    ErrorCode::InternalFailure,
                    "Camera shutdown failed unexpectedly.",
                    exception.what());
            } catch (...) {
                firstError = makeError(
                    ErrorCode::InternalFailure,
                    "Camera shutdown failed unexpectedly.");
            }
        }
        cameraStopped = true;
        if (auto status = finishShutdownIfPossible();
            std::holds_alternative<Error>(status)) {
            const auto& cleanup = std::get<Error>(status);
            if (!firstError.has_value()) {
                firstError = cleanup;
            } else {
                appendCleanupError(*firstError, cleanup);
            }
        }
        return firstError.has_value()
            ? vision::core::Status{*firstError}
            : vision::core::Status{std::monostate{}};
    }

    [[nodiscard]] vision::core::Status shutdownLight()
    {
        if (lightStopped) {
            return finishShutdownIfPossible();
        }
        lifecycle = Lifecycle::ShuttingDown;
        std::optional<Error> firstError;
        const int closeResult = lightController.close();
        if (closeResult != MV_OK) {
            firstError = hardwareError(
                ErrorCode::HardwareFailure,
                "MV_CC_CloseInterface/MV_CC_DestroyInterface",
                closeResult);
        }
        lightStopped = true;
        if (auto status = finishShutdownIfPossible();
            std::holds_alternative<Error>(status)) {
            const auto& cleanup = std::get<Error>(status);
            if (!firstError.has_value()) {
                firstError = cleanup;
            } else {
                appendCleanupError(*firstError, cleanup);
            }
        }
        return firstError.has_value()
            ? vision::core::Status{*firstError}
            : vision::core::Status{std::monostate{}};
    }

    [[nodiscard]] vision::core::Status shutdownAll()
    {
        std::optional<Error> firstError;
        if (auto status = turnOff(); std::holds_alternative<Error>(status)) {
            firstError = std::get<Error>(status);
        }
        if (auto status = shutdownCamera();
            std::holds_alternative<Error>(status)) {
            const auto& error = std::get<Error>(status);
            if (!firstError.has_value()) firstError = error;
            else appendCleanupError(*firstError, error);
        }
        if (auto status = shutdownLight();
            std::holds_alternative<Error>(status)) {
            const auto& error = std::get<Error>(status);
            if (!firstError.has_value()) firstError = error;
            else appendCleanupError(*firstError, error);
        }
        return firstError.has_value()
            ? vision::core::Status{*firstError}
            : vision::core::Status{std::monostate{}};
    }
};

class CameraPort final : public vision::core::ICamera {
public:
    explicit CameraPort(std::shared_ptr<HikHardwareState> state)
        : state_(std::move(state))
    {
    }

    ~CameraPort() override
    {
        try {
            static_cast<void>(state_->shutdownCamera());
        } catch (...) {
        }
    }

    vision::core::CaptureOutcome capture(LocateMode mode) override
    {
        return state_->capture(mode);
    }

    vision::core::Status recover() override
    {
        return state_->recoverCamera();
    }

    vision::core::Status shutdown() override
    {
        return state_->shutdownCamera();
    }

private:
    std::shared_ptr<HikHardwareState> state_;
};

class LightPort final : public vision::core::ILight {
public:
    explicit LightPort(std::shared_ptr<HikHardwareState> state)
        : state_(std::move(state))
    {
    }

    ~LightPort() override
    {
        try {
            static_cast<void>(state_->shutdownLight());
        } catch (...) {
        }
    }

    vision::core::Status turnOn(LocateMode mode) override
    {
        return state_->turnOn(mode);
    }

    vision::core::Status turnOff() override
    {
        return state_->turnOff();
    }

    vision::core::Status recover() override
    {
        return state_->recoverLight();
    }

    vision::core::Status shutdown() override
    {
        return state_->shutdownLight();
    }

private:
    std::shared_ptr<HikHardwareState> state_;
};

} // namespace

vision::core::Status validateHikHardwareConfiguration(
    const HikCameraSystemConfig& cameraConfig,
    const HikLightControllerConfig& lightConfig)
{
    class ValidationObserver final : public ICaptureObserver {
    public:
        void onPrepared(vision::core::LocateMode) noexcept override {}
    } observer;

    if (auto error = validateConfig(cameraConfig, &observer);
        error.has_value()) {
        return *error;
    }
    if (auto error = validateLightConfig(lightConfig); error.has_value()) {
        return *error;
    }
    return std::monostate{};
}

HikHardwareCreateOutcome createHikHardware(
    HikCameraSystemConfig cameraConfig,
    HikLightControllerConfig lightConfig,
    ICaptureObserver* captureObserver,
    vision::core::IDiagnosticSink* diagnostics)
{
    if (auto error = validateConfig(cameraConfig, captureObserver);
        error.has_value()) {
        return *error;
    }
    if (auto error = validateLightConfig(lightConfig); error.has_value()) {
        return *error;
    }

    try {
        auto state = std::make_shared<HikHardwareState>(
            std::move(cameraConfig),
            std::move(lightConfig),
            captureObserver,
            diagnostics);
        if (auto error = state->initialize(); error.has_value()) {
            if (auto cleanup = state->shutdownAll();
                std::holds_alternative<Error>(cleanup)) {
                appendCleanupError(*error, std::get<Error>(cleanup));
            }
            return *error;
        }

        HikHardwarePorts ports;
        ports.camera = std::make_unique<CameraPort>(state);
        ports.light = std::make_unique<LightPort>(std::move(state));
        return HikHardwareCreateOutcome{std::move(ports)};
    } catch (const std::exception& exception) {
        return makeError(
            ErrorCode::InternalFailure,
            "Hardware creation failed unexpectedly.",
            exception.what());
    } catch (...) {
        return makeError(
            ErrorCode::InternalFailure,
            "Hardware creation failed unexpectedly.");
    }
}

} // namespace vision::hardware
