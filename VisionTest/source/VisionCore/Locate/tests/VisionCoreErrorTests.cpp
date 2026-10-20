#include "VisionCore/support/VisionCoreFixture.h"

#include <vector>

#include <gtest/gtest.h>

#include "VisionCore/support/CoreAssertions.h"
#include "VisionCore/support/OutcomeAssertions.h"

namespace vision::test::core {
namespace {

using vision::core::CoreState;
using vision::core::ErrorCode;
using vision::core::LocateMode;
using vision::core::LocateOptions;

class Core_VisionCoreErrorTests : public VisionCoreFixture {};

TEST_F(Core_VisionCoreErrorTests, Locate_LightHardwareUnavailable_IsPropagatedWithoutWrapping) {
    const auto error = MakeError(ErrorCode::HardwareUnavailable, "light-unavailable");
    light_->enqueueTurnOn(error);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(outcome, error));
    const auto status = core_->status();
    ASSERT_TRUE(status.lastError.has_value());
    EXPECT_EQ(status.lastError->code, error.code);
    EXPECT_EQ(status.lastError->message, error.message);
    EXPECT_EQ(status.lastError->diagnostic, error.diagnostic);
    EXPECT_TRUE(HasStatus(status, CoreState::Faulted, ErrorCode::HardwareUnavailable));
}

TEST_F(Core_VisionCoreErrorTests, Locate_CameraHardwareFailuresAndTimeouts_ArePropagatedWithoutWrapping) {
    const std::vector<vision::core::Error> errors{
        MakeError(ErrorCode::HardwareFailure, "camera-failure"),
        MakeError(ErrorCode::HardwareTimeout, "camera-timeout")};

    for (const auto& error : errors) {
        camera_->enqueueCapture(error);
        const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});
        EXPECT_TRUE(IsOriginalError(outcome, error));
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, error.code));
        ASSERT_TRUE(IsSuccess(core_->recover()));
    }
}

TEST_F(Core_VisionCoreErrorTests, Locate_AlgorithmFailureAndTimeout_ArePropagatedAndRemainReady) {
    const auto failure = MakeError(ErrorCode::AlgorithmFailure, "algorithm-failure");
    const auto timeout = MakeError(ErrorCode::AlgorithmTimeout, "algorithm-timeout");
    algorithm_->setDefaultOutcome(failure);

    const auto edgeOutcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(edgeOutcome, failure));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready, ErrorCode::AlgorithmFailure));

    camera_->setDefaultCapture(MakeMarkPointFrames());
    algorithm_->setDefaultOutcome(timeout);
    const auto markPointOutcome = core_->locate(LocateOptions{LocateMode::MarkPoint});

    EXPECT_TRUE(IsOriginalError(markPointOutcome, timeout));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready, ErrorCode::AlgorithmTimeout));
}

TEST_F(Core_VisionCoreErrorTests, Locate_DeterministicPortWorkDoesNotAlterAlgorithmTimeoutSemantics) {
    std::size_t workUnits = 0U;
    light_->onTurnOn = [&workUnits] { workUnits += 3U; };
    camera_->onCapture = [&workUnits] { workUnits += 5U; };
    const auto timeout = MakeError(ErrorCode::AlgorithmTimeout, "algorithm-timeout-after-work");
    algorithm_->setDefaultOutcome(timeout);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_EQ(workUnits, 8U);
    EXPECT_TRUE(IsOriginalError(outcome, timeout));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready, ErrorCode::AlgorithmTimeout));
    EXPECT_EQ(light_->turnOffCalls(), 1U);
}

TEST_F(Core_VisionCoreErrorTests, Locate_TurnOffHardwareTimeout_AfterCaptureSkipsAlgorithmAndFaults) {
    const auto timeout = MakeError(ErrorCode::HardwareTimeout, "turn-off-timeout");
    light_->enqueueTurnOff(timeout);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(outcome, timeout));
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareTimeout));
}

TEST_F(Core_VisionCoreErrorTests, Locate_ErrorStatusSnapshotIsByValue) {
    const auto error = MakeError(ErrorCode::HardwareFailure, "snapshot-error");
    camera_->enqueueCapture(error);
    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});
    ASSERT_TRUE(IsOriginalError(outcome, error));

    auto snapshot = core_->status();
    ASSERT_TRUE(snapshot.lastError.has_value());
    snapshot.lastError->message = "mutated snapshot";
    snapshot.lastError->diagnostic = "mutated diagnostic";

    const auto freshSnapshot = core_->status();
    ASSERT_TRUE(freshSnapshot.lastError.has_value());
    EXPECT_EQ(freshSnapshot.lastError->message, error.message);
    EXPECT_EQ(freshSnapshot.lastError->diagnostic, error.diagnostic);
}

TEST_F(Core_VisionCoreErrorTests, Locate_InvalidImageCreatesCoreErrorAndFaultsService) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().cameraId.clear();
    camera_->setDefaultCapture(frames);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidImage));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InvalidImage));
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
}

} // namespace
} // namespace vision::test::core
