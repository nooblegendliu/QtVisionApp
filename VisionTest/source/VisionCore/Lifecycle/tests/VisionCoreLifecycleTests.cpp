#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "VisionCore/support/CoreAssertions.h"
#include "VisionCore/support/CoreTestData.h"
#include "VisionCore/support/CoreTestDoubles.h"
#include "VisionCore/support/OutcomeAssertions.h"
#include "VisionCore/support/VisionCoreFixture.h"

namespace vision::test::core {
namespace {

using vision::core::CoreState;
using vision::core::ErrorCode;
using vision::core::LocateMode;
using vision::core::LocateOptions;
using vision::core::Status;
using vision::core::VisionCore;

class Core_VisionCoreLifecycleFixture : public VisionCoreFixture {};

TEST_F(Core_VisionCoreLifecycleFixture, Shutdown_ReadyState_ClosesPortsInOrderAndStops) {
    clearCalls();

    const Status status = core_->shutdown();

    EXPECT_TRUE(IsSuccess(status));
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightTurnOff,
                                         CallOperation::CameraShutdown,
                                         CallOperation::LightShutdown}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Stopped));
    EXPECT_TRUE(HasErrorCode(core_->locate(LocateOptions{LocateMode::Edge}), ErrorCode::NotReady));
    EXPECT_TRUE(HasErrorCode(core_->recover(), ErrorCode::NotReady));
}

TEST_F(Core_VisionCoreLifecycleFixture, Shutdown_FaultedState_StillClosesPortsAndStops) {
    light_->enqueueTurnOn(MakeError(ErrorCode::HardwareFailure, "force-fault"));
    ASSERT_TRUE(HasErrorCode(core_->locate(LocateOptions{LocateMode::Edge}), ErrorCode::HardwareFailure));
    clearCalls();

    const Status status = core_->shutdown();

    EXPECT_TRUE(IsSuccess(status));
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightTurnOff,
                                         CallOperation::CameraShutdown,
                                         CallOperation::LightShutdown}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Stopped, ErrorCode::HardwareFailure));
}

TEST_F(Core_VisionCoreLifecycleFixture, Shutdown_AllStepsFail_ReturnsLightTurnOffErrorAndStillStops) {
    const auto turnOffError = MakeError(ErrorCode::HardwareFailure, "shutdown-turn-off");
    const auto cameraError = MakeError(ErrorCode::HardwareFailure, "shutdown-camera");
    const auto lightError = MakeError(ErrorCode::HardwareFailure, "shutdown-light");
    light_->enqueueTurnOff(turnOffError);
    camera_->enqueueShutdown(cameraError);
    light_->enqueueShutdown(lightError);
    clearCalls();

    const Status status = core_->shutdown();

    EXPECT_TRUE(IsOriginalError(status, turnOffError));
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightTurnOff,
                                         CallOperation::CameraShutdown,
                                         CallOperation::LightShutdown}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Stopped, ErrorCode::HardwareFailure));
}

TEST_F(Core_VisionCoreLifecycleFixture, Shutdown_LightShutdownErrorTakesPrecedenceOverCameraShutdownError) {
    const auto cameraError = MakeError(ErrorCode::HardwareFailure, "shutdown-camera-only");
    const auto lightError = MakeError(ErrorCode::HardwareFailure, "shutdown-light-only");
    camera_->enqueueShutdown(cameraError);
    light_->enqueueShutdown(lightError);

    const Status status = core_->shutdown();

    EXPECT_TRUE(IsOriginalError(status, lightError));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Stopped, ErrorCode::HardwareFailure));
}

TEST_F(Core_VisionCoreLifecycleFixture, Shutdown_CalledTwice_SecondCallSucceedsWithoutTouchingPorts) {
    ASSERT_TRUE(IsSuccess(core_->shutdown()));
    const auto callsAfterFirstShutdown = callLog_.operations();

    const Status secondShutdown = core_->shutdown();

    EXPECT_TRUE(IsSuccess(secondShutdown));
    EXPECT_EQ(callLog_.operations(), callsAfterFirstShutdown);
}

TEST(Core_VisionCoreLifecycleTests, Destruction_WithoutExplicitShutdown_PerformsOneBestEffortShutdown) {
    CallLog callLog;
    LifetimeLog lifetimeLog;
    {
        auto camera = std::make_unique<FakeCamera>(callLog, &lifetimeLog);
        auto light = std::make_unique<FakeLight>(callLog, &lifetimeLog);
        auto algorithm = std::make_unique<FakeLocateAlgorithm>(callLog, &lifetimeLog);
        VisionCore core(std::move(camera), std::move(light), std::move(algorithm));
    }

    EXPECT_EQ(callLog.operations(), (std::vector<CallOperation>{
                                        CallOperation::LightTurnOff,
                                        CallOperation::CameraShutdown,
                                        CallOperation::LightShutdown}));
    EXPECT_EQ(lifetimeLog.cameraDestructions, 1U);
    EXPECT_EQ(lifetimeLog.lightDestructions, 1U);
    EXPECT_EQ(lifetimeLog.algorithmDestructions, 1U);
}

TEST(Core_VisionCoreLifecycleTests, Destruction_AfterExplicitShutdown_DoesNotRepeatPortShutdown) {
    CallLog callLog;
    LifetimeLog lifetimeLog;
    {
        auto camera = std::make_unique<FakeCamera>(callLog, &lifetimeLog);
        auto light = std::make_unique<FakeLight>(callLog, &lifetimeLog);
        auto algorithm = std::make_unique<FakeLocateAlgorithm>(callLog, &lifetimeLog);
        VisionCore core(std::move(camera), std::move(light), std::move(algorithm));
        ASSERT_TRUE(IsSuccess(core.shutdown()));
    }

    EXPECT_EQ(callLog.operations(), (std::vector<CallOperation>{
                                        CallOperation::LightTurnOff,
                                        CallOperation::CameraShutdown,
                                        CallOperation::LightShutdown}));
    EXPECT_EQ(lifetimeLog.cameraDestructions, 1U);
    EXPECT_EQ(lifetimeLog.lightDestructions, 1U);
    EXPECT_EQ(lifetimeLog.algorithmDestructions, 1U);
}

TEST(Core_VisionCoreLifecycleTests, Destruction_PortErrorsAndExceptions_DoNotEscapeNoexceptBoundary) {
    CallLog callLog;
    LifetimeLog lifetimeLog;

    EXPECT_NO_THROW({
        auto camera = std::make_unique<FakeCamera>(callLog, &lifetimeLog);
        auto light = std::make_unique<FakeLight>(callLog, &lifetimeLog);
        auto algorithm = std::make_unique<FakeLocateAlgorithm>(callLog, &lifetimeLog);
        light->enqueueTurnOff(MakeError(ErrorCode::HardwareFailure, "destructor-turn-off"));
        camera->enqueueShutdownException(ExceptionKind::Standard);
        light->enqueueShutdownException(ExceptionKind::NonStandard);
        VisionCore core(std::move(camera), std::move(light), std::move(algorithm));
    });

    EXPECT_EQ(lifetimeLog.cameraDestructions, 1U);
    EXPECT_EQ(lifetimeLog.lightDestructions, 1U);
    EXPECT_EQ(lifetimeLog.algorithmDestructions, 1U);
}

} // namespace
} // namespace vision::test::core
