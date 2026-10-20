#include "VisionCore/support/VisionCoreFixture.h"

#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "VisionCore/support/CoreAssertions.h"
#include "VisionCore/support/OutcomeAssertions.h"

namespace vision::test::core {
namespace {

using vision::core::CoreState;
using vision::core::ErrorCode;
using vision::core::ICamera;
using vision::core::ILight;
using vision::core::ILocateAlgorithm;
using vision::core::LocateMode;
using vision::core::LocateOptions;
using vision::core::LocateOutcome;
using vision::core::Status;
using vision::core::VisionCore;

class Core_VisionCoreStateFixture : public VisionCoreFixture {};

TEST_F(Core_VisionCoreStateFixture, Construction_AllPortsPresent_StartsReady) {
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
}

TEST(Core_VisionCoreStateTests, Construction_AnyRequiredPortMissing_StartsFaultedWithInvalidConfiguration) {
    struct MissingPortCase {
        bool camera;
        bool light;
        bool algorithm;
    };
    const std::vector<MissingPortCase> cases{
        {true, false, false}, {false, true, false}, {false, false, true}, {true, true, false}};

    for (const MissingPortCase& missing : cases) {
        CallLog callLog;
        std::unique_ptr<ICamera> camera;
        std::unique_ptr<ILight> light;
        std::unique_ptr<ILocateAlgorithm> algorithm;
        if (!missing.camera) {
            camera = std::make_unique<FakeCamera>(callLog);
        }
        if (!missing.light) {
            light = std::make_unique<FakeLight>(callLog);
        }
        if (!missing.algorithm) {
            algorithm = std::make_unique<FakeLocateAlgorithm>(callLog);
        }

        VisionCore core(std::move(camera), std::move(light), std::move(algorithm));
        EXPECT_TRUE(HasStatus(core.status(), CoreState::Faulted, ErrorCode::InvalidConfiguration));
        EXPECT_TRUE(HasErrorCode(core.locate(LocateOptions{LocateMode::Edge}), ErrorCode::NotReady));
        EXPECT_TRUE(callLog.operations().empty());
    }
}

TEST_F(Core_VisionCoreStateFixture, Locate_FaultedState_ReturnsNotReadyWithoutOverwritingLastError) {
    const auto failure = MakeError(ErrorCode::HardwareFailure, "force-fault");
    light_->enqueueTurnOn(failure);
    ASSERT_TRUE(IsOriginalError(core_->locate(LocateOptions{LocateMode::Edge}), failure));
    const auto callsBefore = callLog_.operations().size();

    const auto rejected = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(HasErrorCode(rejected, ErrorCode::NotReady));
    EXPECT_EQ(callLog_.operations().size(), callsBefore);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
}

TEST_F(Core_VisionCoreStateFixture, ReadyRecover_IsIdempotentAndDoesNotTouchPorts) {
    clearCalls();

    const Status status = core_->recover();

    EXPECT_TRUE(IsSuccess(status));
    EXPECT_TRUE(callLog_.operations().empty());
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
}

TEST_F(Core_VisionCoreStateFixture, Locate_ReentrantLocateDuringCapture_ReturnsBusyAndOuterLocateCompletes) {
    std::optional<LocateOutcome> innerOutcome;
    camera_->onCapture = [this, &innerOutcome] {
        innerOutcome = core_->locate(LocateOptions{LocateMode::Edge});
    };

    const auto outerOutcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(innerOutcome.has_value());
    EXPECT_TRUE(HasErrorCode(*innerOutcome, ErrorCode::Busy));
    EXPECT_TRUE(IsSuccess(outerOutcome));
    EXPECT_EQ(camera_->captureCalls(), 1U);
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
}

TEST_F(Core_VisionCoreStateFixture, Locate_ReentrantRecoverDuringCapture_ReturnsBusyAndOuterLocateCompletes) {
    std::optional<Status> innerStatus;
    camera_->onCapture = [this, &innerStatus] { innerStatus = core_->recover(); };

    const auto outerOutcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(innerStatus.has_value());
    EXPECT_TRUE(HasErrorCode(*innerStatus, ErrorCode::Busy));
    EXPECT_TRUE(IsSuccess(outerOutcome));
    EXPECT_EQ(camera_->captureCalls(), 1U);
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
}

TEST_F(Core_VisionCoreStateFixture, Locate_ReentrantShutdownDuringCapture_ReturnsBusyAndOuterLocateCompletes) {
    std::optional<Status> innerStatus;
    camera_->onCapture = [this, &innerStatus] { innerStatus = core_->shutdown(); };

    const auto outerOutcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(innerStatus.has_value());
    EXPECT_TRUE(HasErrorCode(*innerStatus, ErrorCode::Busy));
    EXPECT_TRUE(IsSuccess(outerOutcome));
    EXPECT_EQ(camera_->captureCalls(), 1U);
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
}

TEST_F(Core_VisionCoreStateFixture, Shutdown_ReentrantOperationsDuringTurnOff_ReturnNotReadyAndOuterShutdownCompletes) {
    std::optional<Status> nestedShutdown;
    std::optional<Status> nestedRecover;
    std::optional<LocateOutcome> nestedLocate;
    light_->onTurnOff = [this, &nestedShutdown, &nestedRecover, &nestedLocate] {
        nestedShutdown = core_->shutdown();
        nestedRecover = core_->recover();
        nestedLocate = core_->locate(LocateOptions{LocateMode::Edge});
    };

    const Status outerStatus = core_->shutdown();

    EXPECT_TRUE(IsSuccess(outerStatus));
    ASSERT_TRUE(nestedShutdown.has_value());
    ASSERT_TRUE(nestedRecover.has_value());
    ASSERT_TRUE(nestedLocate.has_value());
    EXPECT_TRUE(HasErrorCode(*nestedShutdown, ErrorCode::NotReady));
    EXPECT_TRUE(HasErrorCode(*nestedRecover, ErrorCode::NotReady));
    EXPECT_TRUE(HasErrorCode(*nestedLocate, ErrorCode::NotReady));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Stopped));
}

TEST_F(Core_VisionCoreStateFixture, Recover_AllStepsSucceed_ReturnsReadyAndAllowsNextLocate) {
    light_->enqueueTurnOn(MakeError(ErrorCode::HardwareFailure, "force-fault"));
    ASSERT_TRUE(HasErrorCode(core_->locate(LocateOptions{LocateMode::Edge}), ErrorCode::HardwareFailure));
    clearCalls();

    const Status recovery = core_->recover();

    EXPECT_TRUE(IsSuccess(recovery));
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightRecover,
                                         CallOperation::LightTurnOff,
                                         CallOperation::CameraRecover}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});
    EXPECT_TRUE(IsSuccess(outcome));
}

TEST_F(Core_VisionCoreStateFixture, Recover_MultipleErrors_ReturnsLightTurnOffErrorAndRemainsFaulted) {
    light_->enqueueTurnOn(MakeError(ErrorCode::HardwareFailure, "force-fault"));
    ASSERT_TRUE(HasErrorCode(core_->locate(LocateOptions{LocateMode::Edge}), ErrorCode::HardwareFailure));
    const auto lightRecoverError = MakeError(ErrorCode::HardwareFailure, "light-recover");
    const auto lightOffError = MakeError(ErrorCode::HardwareFailure, "light-turn-off");
    const auto cameraRecoverError = MakeError(ErrorCode::HardwareFailure, "camera-recover");
    light_->enqueueRecover(lightRecoverError);
    light_->enqueueTurnOff(lightOffError);
    camera_->enqueueRecover(cameraRecoverError);
    clearCalls();

    const Status recovery = core_->recover();

    EXPECT_TRUE(IsOriginalError(recovery, lightOffError));
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightRecover,
                                         CallOperation::LightTurnOff,
                                         CallOperation::CameraRecover}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
}

TEST_F(Core_VisionCoreStateFixture, Recover_LightRecoverErrorTakesPrecedenceOverCameraRecoverError) {
    light_->enqueueTurnOn(MakeError(ErrorCode::HardwareFailure, "force-fault"));
    ASSERT_TRUE(HasErrorCode(core_->locate(LocateOptions{LocateMode::Edge}), ErrorCode::HardwareFailure));
    const auto lightRecoverError = MakeError(ErrorCode::HardwareFailure, "light-recover-only");
    const auto cameraRecoverError = MakeError(ErrorCode::HardwareFailure, "camera-recover-only");
    light_->enqueueRecover(lightRecoverError);
    camera_->enqueueRecover(cameraRecoverError);

    const Status recovery = core_->recover();

    EXPECT_TRUE(IsOriginalError(recovery, lightRecoverError));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
}

TEST_F(Core_VisionCoreStateFixture, Recover_OnlyCameraRecoverFails_ReturnsCameraErrorAndRemainsFaulted) {
    light_->enqueueTurnOn(MakeError(ErrorCode::HardwareFailure, "force-fault"));
    ASSERT_TRUE(HasErrorCode(core_->locate(LocateOptions{LocateMode::Edge}), ErrorCode::HardwareFailure));
    const auto cameraRecoverError = MakeError(ErrorCode::HardwareFailure, "camera-recover-only");
    camera_->enqueueRecover(cameraRecoverError);

    const Status recovery = core_->recover();

    EXPECT_TRUE(IsOriginalError(recovery, cameraRecoverError));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
}

} // namespace
} // namespace vision::test::core
