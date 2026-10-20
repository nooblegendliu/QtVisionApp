#include "VisionCore/support/VisionCoreFixture.h"

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "VisionCore/support/CoreAssertions.h"
#include "VisionCore/support/OutcomeAssertions.h"

namespace vision::test::core {
namespace {

using vision::core::CoreState;
using vision::core::Error;
using vision::core::ErrorCode;
using vision::core::LocateMode;
using vision::core::LocateOptions;

enum class ExceptionOperation {
    LightTurnOn,
    CameraCapture,
    AlgorithmLocate,
    LightTurnOff,
    LightRecover,
    CameraRecover,
    CameraShutdown,
    LightShutdown
};

struct ExceptionCase {
    ExceptionOperation operation;
    ExceptionKind kind;
};

class Core_VisionCoreExceptionTests : public VisionCoreFixture,
                                 public ::testing::WithParamInterface<ExceptionCase> {
protected:
    void forceFaultedState() {
        light_->enqueueTurnOn(MakeError(ErrorCode::HardwareFailure, "force-fault-for-exception"));
        ASSERT_TRUE(HasErrorCode(core_->locate(LocateOptions{LocateMode::Edge}), ErrorCode::HardwareFailure));
        ASSERT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
        clearCalls();
    }

    void expectStandardExceptionTextIfApplicable(const Error& error, ExceptionKind kind) {
        if (kind == ExceptionKind::Standard) {
            EXPECT_NE(error.diagnostic.find("fake exception"), std::string::npos);
        }
    }
};

TEST_P(Core_VisionCoreExceptionTests, PublicOperation_PortThrows_ReturnsInternalFailureWithoutEscaping) {
    const ExceptionCase parameter = GetParam();
    switch (parameter.operation) {
    case ExceptionOperation::LightTurnOn: {
        light_->enqueueTurnOnException(parameter.kind);
        const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});
        ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InternalFailure));
        expectStandardExceptionTextIfApplicable(std::get<Error>(outcome), parameter.kind);
        EXPECT_EQ(camera_->captureCalls(), 0U);
        EXPECT_EQ(light_->turnOffCalls(), 1U);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InternalFailure));
        break;
    }
    case ExceptionOperation::CameraCapture: {
        camera_->enqueueCaptureException(parameter.kind);
        const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});
        ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InternalFailure));
        expectStandardExceptionTextIfApplicable(std::get<Error>(outcome), parameter.kind);
        EXPECT_EQ(algorithm_->locateCalls(), 0U);
        EXPECT_EQ(light_->turnOffCalls(), 1U);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InternalFailure));
        break;
    }
    case ExceptionOperation::AlgorithmLocate: {
        algorithm_->enqueueException(parameter.kind);
        const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});
        ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InternalFailure));
        expectStandardExceptionTextIfApplicable(std::get<Error>(outcome), parameter.kind);
        EXPECT_EQ(light_->turnOffCalls(), 1U);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InternalFailure));
        break;
    }
    case ExceptionOperation::LightTurnOff: {
        light_->enqueueTurnOffException(parameter.kind);
        const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});
        ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InternalFailure));
        expectStandardExceptionTextIfApplicable(std::get<Error>(outcome), parameter.kind);
        EXPECT_EQ(algorithm_->locateCalls(), 0U);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InternalFailure));
        break;
    }
    case ExceptionOperation::LightRecover: {
        forceFaultedState();
        light_->enqueueRecoverException(parameter.kind);
        const auto status = core_->recover();
        ASSERT_TRUE(HasErrorCode(status, ErrorCode::InternalFailure));
        expectStandardExceptionTextIfApplicable(std::get<Error>(status), parameter.kind);
        EXPECT_EQ(camera_->recoverCalls(), 1U);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InternalFailure));
        break;
    }
    case ExceptionOperation::CameraRecover: {
        forceFaultedState();
        camera_->enqueueRecoverException(parameter.kind);
        const auto status = core_->recover();
        ASSERT_TRUE(HasErrorCode(status, ErrorCode::InternalFailure));
        expectStandardExceptionTextIfApplicable(std::get<Error>(status), parameter.kind);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InternalFailure));
        break;
    }
    case ExceptionOperation::CameraShutdown: {
        camera_->enqueueShutdownException(parameter.kind);
        const auto status = core_->shutdown();
        ASSERT_TRUE(HasErrorCode(status, ErrorCode::InternalFailure));
        expectStandardExceptionTextIfApplicable(std::get<Error>(status), parameter.kind);
        EXPECT_EQ(light_->shutdownCalls(), 1U);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Stopped, ErrorCode::InternalFailure));
        break;
    }
    case ExceptionOperation::LightShutdown: {
        light_->enqueueShutdownException(parameter.kind);
        const auto status = core_->shutdown();
        ASSERT_TRUE(HasErrorCode(status, ErrorCode::InternalFailure));
        expectStandardExceptionTextIfApplicable(std::get<Error>(status), parameter.kind);
        EXPECT_EQ(camera_->shutdownCalls(), 1U);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Stopped, ErrorCode::InternalFailure));
        break;
    }
    }
}

INSTANTIATE_TEST_CASE_P(
    StandardAndNonStandardExceptions,
    Core_VisionCoreExceptionTests,
    ::testing::Values(
        ExceptionCase{ExceptionOperation::LightTurnOn, ExceptionKind::Standard},
        ExceptionCase{ExceptionOperation::LightTurnOn, ExceptionKind::NonStandard},
        ExceptionCase{ExceptionOperation::CameraCapture, ExceptionKind::Standard},
        ExceptionCase{ExceptionOperation::CameraCapture, ExceptionKind::NonStandard},
        ExceptionCase{ExceptionOperation::AlgorithmLocate, ExceptionKind::Standard},
        ExceptionCase{ExceptionOperation::AlgorithmLocate, ExceptionKind::NonStandard},
        ExceptionCase{ExceptionOperation::LightTurnOff, ExceptionKind::Standard},
        ExceptionCase{ExceptionOperation::LightTurnOff, ExceptionKind::NonStandard},
        ExceptionCase{ExceptionOperation::LightRecover, ExceptionKind::Standard},
        ExceptionCase{ExceptionOperation::LightRecover, ExceptionKind::NonStandard},
        ExceptionCase{ExceptionOperation::CameraRecover, ExceptionKind::Standard},
        ExceptionCase{ExceptionOperation::CameraRecover, ExceptionKind::NonStandard},
        ExceptionCase{ExceptionOperation::CameraShutdown, ExceptionKind::Standard},
        ExceptionCase{ExceptionOperation::CameraShutdown, ExceptionKind::NonStandard},
        ExceptionCase{ExceptionOperation::LightShutdown, ExceptionKind::Standard},
        ExceptionCase{ExceptionOperation::LightShutdown, ExceptionKind::NonStandard}));

} // namespace
} // namespace vision::test::core
