#include "VisionCore/support/VisionCoreFixture.h"
#include <array>
#include <variant>

namespace vision::test::core {
using namespace vision::core;
class Core_CalibrationCapture : public VisionCoreFixture {};

TEST_F(Core_CalibrationCapture, BothModesReturnOwnedFramesWithoutCallingLocateAlgorithm) {
    for (const auto mode : {LocateMode::Edge, LocateMode::MarkPoint}) {
        SCOPED_TRACE(static_cast<int>(mode));
        auto frames = mode == LocateMode::Edge ? MakeEdgeFrames() : MakeMarkPointFrames();
        camera_->setDefaultCapture(frames);
        clearCalls();
        auto outcome = core_->captureForCalibration(mode);
        ASSERT_TRUE(std::holds_alternative<ImageFrames>(outcome));
        const auto& captured = std::get<ImageFrames>(outcome);
        ASSERT_EQ(captured.frames.size(), frames.frames.size());
        EXPECT_EQ(algorithm_->locateCalls(), 0U);
        EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
            CallOperation::LightTurnOn, CallOperation::CameraCapture, CallOperation::LightTurnOff}));
        EXPECT_EQ(light_->turnOnModes().back(), mode);
        EXPECT_EQ(camera_->captureModes().back(), mode);
        EXPECT_EQ(core_->status().state, CoreState::Ready);
        for (std::size_t i = 0; i < captured.frames.size(); ++i) {
            EXPECT_EQ(captured.frames[i].cameraRole, frames.frames[i].cameraRole);
            EXPECT_EQ(captured.frames[i].image.data, frames.frames[i].image.data);
        }
    }
}

TEST_F(Core_CalibrationCapture, CleanupFailureTakesPriorityAndRecoveryAllowsAnotherCapture) {
    camera_->enqueueCapture(MakeError(ErrorCode::HardwareTimeout, "capture"));
    const auto cleanup = MakeError(ErrorCode::HardwareFailure, "light-off");
    light_->enqueueTurnOff(cleanup);
    const auto failed = core_->captureForCalibration(LocateMode::Edge);
    ASSERT_TRUE(std::holds_alternative<Error>(failed));
    EXPECT_EQ(std::get<Error>(failed).code, cleanup.code);
    EXPECT_EQ(std::get<Error>(failed).message, cleanup.message);
    ASSERT_FALSE(sink_->events().empty());
    EXPECT_EQ(sink_->events().back().operation, "calibration.capture.cleanupPriority");
    EXPECT_EQ(core_->status().state, CoreState::Faulted);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(core_->recover()));
    EXPECT_TRUE(std::holds_alternative<ImageFrames>(core_->captureForCalibration(LocateMode::Edge)));
    EXPECT_FALSE(core_->status().lastError);
}

TEST_F(Core_CalibrationCapture, RejectsInvalidModeBeforeAnyPortAndInvalidFrameAfterLightOff) {
    auto invalidMode = core_->captureForCalibration(static_cast<LocateMode>(-17));
    ASSERT_TRUE(std::holds_alternative<Error>(invalidMode));
    EXPECT_EQ(std::get<Error>(invalidMode).code, ErrorCode::InvalidConfiguration);
    EXPECT_TRUE(callLog_.operations().empty());
    composeDefaultCore(true);
    auto frames = MakeMarkPointFrames();
    frames.frames[3].cameraRole = frames.frames[0].cameraRole;
    camera_->setDefaultCapture(frames);
    auto invalidFrame = core_->captureForCalibration(LocateMode::MarkPoint);
    ASSERT_TRUE(std::holds_alternative<Error>(invalidFrame));
    EXPECT_EQ(std::get<Error>(invalidFrame).code, ErrorCode::InvalidImage);
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
}

TEST_F(Core_CalibrationCapture, InFlightCaptureRejectsAllReentrantBusinessOperations) {
    camera_->onCapture = [&] {
        EXPECT_EQ(std::get<Error>(core_->locate(LocateOptions{LocateMode::Edge})).code, ErrorCode::Busy);
        EXPECT_EQ(std::get<Error>(core_->captureForCalibration(LocateMode::Edge)).code, ErrorCode::Busy);
        EXPECT_EQ(std::get<Error>(core_->recover()).code, ErrorCode::Busy);
        EXPECT_EQ(std::get<Error>(core_->shutdown()).code, ErrorCode::Busy);
    };
    EXPECT_TRUE(std::holds_alternative<ImageFrames>(core_->captureForCalibration(LocateMode::Edge)));
    EXPECT_EQ(camera_->captureCalls(), 1U);
}
} // namespace vision::test::core
