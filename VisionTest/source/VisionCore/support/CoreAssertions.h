#pragma once

#include <chrono>
#include <cmath>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "VisionCore/Diagnostics.h"
#include "VisionCore/LocateTypes.h"

namespace vision::test::core {

inline ::testing::AssertionResult IsOkResult(const vision::core::LocateResult& result,
                                             double expectedX,
                                             double expectedY,
                                             double expectedR) {
    if (result.decision != vision::core::LocateDecision::Ok) {
        return ::testing::AssertionFailure() << "Expected LocateDecision::Ok.";
    }
    if (result.x != expectedX || result.y != expectedY || result.r != expectedR) {
        return ::testing::AssertionFailure()
               << "Expected OK pose {" << expectedX << ", " << expectedY << ", " << expectedR
               << "}, actual {" << result.x << ", " << result.y << ", " << result.r << "}.";
    }
    return ::testing::AssertionSuccess();
}

inline ::testing::AssertionResult IsNgResult(const vision::core::LocateResult& result) {
    if (result.decision != vision::core::LocateDecision::Ng) {
        return ::testing::AssertionFailure() << "Expected LocateDecision::Ng.";
    }
    if (result.x != 0.0 || result.y != 0.0 || result.r != 0.0) {
        return ::testing::AssertionFailure()
               << "Expected NG placeholder pose {0.0, 0.0, 0.0}, actual {" << result.x << ", "
               << result.y << ", " << result.r << "}.";
    }
    return ::testing::AssertionSuccess();
}

inline ::testing::AssertionResult HasStatus(const vision::core::CoreStatus& status,
                                            vision::core::CoreState expectedState,
                                            std::optional<vision::core::ErrorCode> expectedError = std::nullopt) {
    if (status.state != expectedState) {
        return ::testing::AssertionFailure()
               << "Expected CoreState=" << static_cast<int>(expectedState)
               << ", actual=" << static_cast<int>(status.state) << ".";
    }
    if (!expectedError.has_value()) {
        if (status.lastError.has_value()) {
            return ::testing::AssertionFailure()
                   << "Expected no lastError, actual ErrorCode="
                   << static_cast<int>(status.lastError->code) << ".";
        }
        return ::testing::AssertionSuccess();
    }
    if (!status.lastError.has_value()) {
        return ::testing::AssertionFailure()
               << "Expected lastError ErrorCode=" << static_cast<int>(*expectedError) << ", but it is empty.";
    }
    if (status.lastError->code != *expectedError) {
        return ::testing::AssertionFailure()
               << "Expected lastError ErrorCode=" << static_cast<int>(*expectedError)
               << ", actual=" << static_cast<int>(status.lastError->code) << ".";
    }
    return ::testing::AssertionSuccess();
}

inline ::testing::AssertionResult IsValidPublicFrame(const vision::core::ImageFrame& frame) {
    if (frame.image.empty()) {
        return ::testing::AssertionFailure() << "ImageFrame image is empty.";
    }
    if (frame.image.type() != CV_8UC1) {
        return ::testing::AssertionFailure() << "ImageFrame image is not CV_8UC1.";
    }
    if (!frame.image.isContinuous()) {
        return ::testing::AssertionFailure() << "ImageFrame image is not continuous.";
    }
    if (frame.capturedAt == std::chrono::steady_clock::time_point{}) {
        return ::testing::AssertionFailure() << "ImageFrame capturedAt is the default time point.";
    }
    if (frame.cameraId.empty()) {
        return ::testing::AssertionFailure() << "ImageFrame cameraId is empty.";
    }
    switch (frame.cameraRole) {
    case vision::core::CameraRole::Center:
    case vision::core::CameraRole::TopLeft:
    case vision::core::CameraRole::TopRight:
    case vision::core::CameraRole::BottomRight:
    case vision::core::CameraRole::BottomLeft:
        return ::testing::AssertionSuccess();
    case vision::core::CameraRole::Unknown:
    default:
        return ::testing::AssertionFailure() << "ImageFrame cameraRole is not a valid configured role.";
    }
}

inline ::testing::AssertionResult HasFrameContext(const vision::core::FrameDiagnosticContext& actual,
                                                  const vision::core::ImageFrame& expected) {
    if (actual.capturedAt != expected.capturedAt || actual.frameNumber != expected.frameNumber ||
        actual.cameraId != expected.cameraId || actual.cameraRole != expected.cameraRole) {
        return ::testing::AssertionFailure() << "Diagnostic frame context does not match ImageFrame metadata.";
    }
    return ::testing::AssertionSuccess();
}

inline ::testing::AssertionResult HasDiagnosticHeader(const vision::core::DiagnosticEvent& event,
                                                      vision::core::DiagnosticSeverity severity,
                                                      const std::string& operation) {
    if (event.severity != severity || event.component != "VisionCore" || event.operation != operation) {
        return ::testing::AssertionFailure()
               << "Expected diagnostic {severity=" << static_cast<int>(severity)
               << ", component='VisionCore', operation='" << operation << "'}, actual {severity="
               << static_cast<int>(event.severity) << ", component='" << event.component
               << "', operation='" << event.operation << "'}.";
    }
    return ::testing::AssertionSuccess();
}

} // namespace vision::test::core
