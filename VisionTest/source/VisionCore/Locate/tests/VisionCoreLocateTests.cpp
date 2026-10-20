#include "VisionCore/support/VisionCoreFixture.h"

#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "VisionCore/support/CoreAssertions.h"
#include "VisionCore/support/OutcomeAssertions.h"

namespace vision::test::core {
namespace {

using vision::core::AlgorithmOutput;
using vision::core::CameraRole;
using vision::core::CoreState;
using vision::core::ErrorCode;
using vision::core::LocateMode;
using vision::core::LocateOptions;

class Core_VisionCoreLocateTests : public VisionCoreFixture {};

TEST_F(Core_VisionCoreLocateTests, Locate_EdgeFramesAreValid_ReturnsOkAndUsesEdgeInput) {
    auto frames = MakeEdgeFrames();
    const unsigned char* const expectedData = frames.frames.front().image.data;
    const cv::Mat expectedImage = frames.frames.front().image;
    camera_->setDefaultCapture(frames);
    algorithm_->setDefaultOutcome(MakeOkOutput(11.5, -7.25, 89.0));

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(outcome));
    EXPECT_TRUE(IsOkResult(SuccessValue(outcome), 11.5, -7.25, 89.0));
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightTurnOn,
                                         CallOperation::CameraCapture,
                                         CallOperation::LightTurnOff,
                                         CallOperation::AlgorithmLocate}));
    ASSERT_EQ(camera_->captureModes().size(), 1U);
    EXPECT_EQ(camera_->captureModes().front(), LocateMode::Edge);
    ASSERT_EQ(light_->turnOnModes().size(), 1U);
    EXPECT_EQ(light_->turnOnModes().front(), LocateMode::Edge);
    ASSERT_EQ(algorithm_->observations().size(), 1U);
    const AlgorithmObservation& observation = algorithm_->observations().front();
    EXPECT_EQ(observation.kind, AlgorithmInputKind::Edge);
    ASSERT_EQ(observation.images.size(), 1U);
    EXPECT_EQ(observation.images.front().data, expectedData);
    EXPECT_EQ(observation.images.front().pixels,
              std::vector<unsigned char>(expectedImage.data,
                                         expectedImage.data + expectedImage.total() * expectedImage.elemSize()));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
    ASSERT_NE(sink_, nullptr);
    EXPECT_TRUE(sink_->events().empty());
}

TEST_F(Core_VisionCoreLocateTests, Locate_MarkPointFramesAreUnordered_ReturnsOkAndMapsByRole) {
    const std::vector<CameraRole> order{
        CameraRole::BottomLeft,
        CameraRole::TopRight,
        CameraRole::BottomRight,
        CameraRole::TopLeft};
    auto frames = MakeMarkPointFrames(order);
    camera_->setDefaultCapture(frames);
    algorithm_->setDefaultOutcome(MakeOkOutput(-1.5, 2.25, -3.75));

    const auto outcome = core_->locate(LocateOptions{LocateMode::MarkPoint});

    ASSERT_TRUE(IsSuccess(outcome));
    EXPECT_TRUE(IsOkResult(SuccessValue(outcome), -1.5, 2.25, -3.75));
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightTurnOn,
                                         CallOperation::CameraCapture,
                                         CallOperation::LightTurnOff,
                                         CallOperation::AlgorithmLocate}));
    ASSERT_EQ(camera_->captureModes().size(), 1U);
    EXPECT_EQ(camera_->captureModes().front(), LocateMode::MarkPoint);
    ASSERT_EQ(light_->turnOnModes().size(), 1U);
    EXPECT_EQ(light_->turnOnModes().front(), LocateMode::MarkPoint);
    ASSERT_EQ(algorithm_->observations().size(), 1U);
    const AlgorithmObservation& observation = algorithm_->observations().front();
    EXPECT_EQ(observation.kind, AlgorithmInputKind::MarkPoint);
    ASSERT_EQ(observation.images.size(), 4U);

    const std::vector<std::size_t> expectedIndices{3U, 1U, 2U, 0U};
    for (std::size_t index = 0U; index < expectedIndices.size(); ++index) {
        const cv::Mat& expected = frames.frames[expectedIndices[index]].image;
        EXPECT_EQ(observation.images[index].data, expected.data);
        EXPECT_EQ(observation.images[index].pixels,
                  std::vector<unsigned char>(expected.data, expected.data + expected.total() * expected.elemSize()));
    }
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
}

TEST_F(Core_VisionCoreLocateTests, Locate_AlgorithmStartsOnlyAfterLightIsOff) {
    bool lightIsOn = false;
    light_->onTurnOn = [&lightIsOn] { lightIsOn = true; };
    light_->onTurnOff = [&lightIsOn] { lightIsOn = false; };
    algorithm_->onLocate = [&lightIsOn] { EXPECT_FALSE(lightIsOn); };

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(outcome));
    EXPECT_FALSE(lightIsOn);
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
}

TEST_F(Core_VisionCoreLocateTests, Locate_EdgeAlgorithmReturnsNg_ReturnsZeroPoseAndClearsLastError) {
    algorithm_->setDefaultOutcome(MakeNgOutput(std::numeric_limits<double>::quiet_NaN(),
                                                std::numeric_limits<double>::infinity(),
                                                -std::numeric_limits<double>::infinity()));

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(outcome));
    EXPECT_TRUE(IsNgResult(SuccessValue(outcome)));
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
}

TEST_F(Core_VisionCoreLocateTests, Locate_MarkPointAlgorithmReturnsNg_ReturnsZeroPose) {
    camera_->setDefaultCapture(MakeMarkPointFrames());
    algorithm_->setDefaultOutcome(MakeNgOutput(42.0, -8.0, 7.0));

    const auto outcome = core_->locate(LocateOptions{LocateMode::MarkPoint});

    ASSERT_TRUE(IsSuccess(outcome));
    EXPECT_TRUE(IsNgResult(SuccessValue(outcome)));
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
}

struct NonFiniteCoordinateCase {
    int coordinate;
    double value;
};

class Core_VisionCoreNonFiniteResultTests : public VisionCoreFixture,
                                       public ::testing::WithParamInterface<NonFiniteCoordinateCase> {};

TEST_P(Core_VisionCoreNonFiniteResultTests, Locate_ValidAlgorithmOutputIsNonFinite_ReturnsInvalidResult) {
    AlgorithmOutput output = MakeOkOutput(1.0, 2.0, 3.0);
    const NonFiniteCoordinateCase parameter = GetParam();
    if (parameter.coordinate == 0) {
        output.x = parameter.value;
    } else if (parameter.coordinate == 1) {
        output.y = parameter.value;
    } else {
        output.r = parameter.value;
    }
    algorithm_->setDefaultOutcome(output);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidResult));
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready, ErrorCode::InvalidResult));
}

INSTANTIATE_TEST_CASE_P(
    AllCoordinatesAndNonFiniteValues,
    Core_VisionCoreNonFiniteResultTests,
    ::testing::Values(
        NonFiniteCoordinateCase{0, std::numeric_limits<double>::quiet_NaN()},
        NonFiniteCoordinateCase{0, std::numeric_limits<double>::infinity()},
        NonFiniteCoordinateCase{0, -std::numeric_limits<double>::infinity()},
        NonFiniteCoordinateCase{1, std::numeric_limits<double>::quiet_NaN()},
        NonFiniteCoordinateCase{1, std::numeric_limits<double>::infinity()},
        NonFiniteCoordinateCase{1, -std::numeric_limits<double>::infinity()},
        NonFiniteCoordinateCase{2, std::numeric_limits<double>::quiet_NaN()},
        NonFiniteCoordinateCase{2, std::numeric_limits<double>::infinity()},
        NonFiniteCoordinateCase{2, -std::numeric_limits<double>::infinity()}));

TEST_F(Core_VisionCoreLocateTests, Locate_SubsequentSuccessAfterInvalidResult_ClearsLastError) {
    algorithm_->setDefaultOutcome(MakeOkOutput(std::numeric_limits<double>::quiet_NaN(), 1.0, 2.0));
    const auto invalidOutcome = core_->locate(LocateOptions{LocateMode::Edge});
    ASSERT_TRUE(HasErrorCode(invalidOutcome, ErrorCode::InvalidResult));
    ASSERT_TRUE(HasStatus(core_->status(), CoreState::Ready, ErrorCode::InvalidResult));

    algorithm_->setDefaultOutcome(MakeOkOutput(1.0, 2.0, 3.0));
    const auto successfulOutcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(successfulOutcome));
    EXPECT_TRUE(IsOkResult(SuccessValue(successfulOutcome), 1.0, 2.0, 3.0));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
}

TEST_F(Core_VisionCoreLocateTests, Locate_LightTurnOnFails_SkipsCaptureAndAlgorithmButTurnsOff) {
    const auto primaryError = MakeError(ErrorCode::HardwareUnavailable, "turn-on-unavailable");
    light_->enqueueTurnOn(primaryError);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(outcome, primaryError));
    ASSERT_EQ(light_->turnOnModes().size(), 1U);
    EXPECT_EQ(light_->turnOnModes().front(), LocateMode::Edge);
    EXPECT_EQ(camera_->captureCalls(), 0U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
    EXPECT_EQ(callLog_.operations(),
              (std::vector<CallOperation>{CallOperation::LightTurnOn, CallOperation::LightTurnOff}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareUnavailable));
}

TEST_F(Core_VisionCoreLocateTests, Locate_CameraCaptureFails_SkipsAlgorithmButTurnsOff) {
    const auto primaryError = MakeError(ErrorCode::HardwareFailure, "capture-failed");
    camera_->enqueueCapture(primaryError);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(outcome, primaryError));
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_EQ(callLog_.operations(),
              (std::vector<CallOperation>{CallOperation::LightTurnOn,
                                          CallOperation::CameraCapture,
                                          CallOperation::LightTurnOff}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
}

TEST_F(Core_VisionCoreLocateTests, Locate_CameraCaptureTimesOut_TurnsOffOnceAndReturnsOriginalError) {
    const auto timeout = MakeError(ErrorCode::HardwareTimeout, "capture-timeout");
    camera_->enqueueCapture(timeout);

    const auto outcome = core_->locate(LocateOptions{LocateMode::MarkPoint});

    EXPECT_TRUE(IsOriginalError(outcome, timeout));
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_EQ(callLog_.operations(),
              (std::vector<CallOperation>{CallOperation::LightTurnOn,
                                          CallOperation::CameraCapture,
                                          CallOperation::LightTurnOff}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareTimeout));
}

TEST_F(Core_VisionCoreLocateTests, Locate_AlgorithmFailsAfterLightOff_ReturnsOriginalError) {
    const auto primaryError = MakeError(ErrorCode::AlgorithmFailure, "algorithm-failed");
    algorithm_->setDefaultOutcome(primaryError);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(outcome, primaryError));
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_EQ(callLog_.operations(),
              (std::vector<CallOperation>{CallOperation::LightTurnOn,
                                          CallOperation::CameraCapture,
                                          CallOperation::LightTurnOff,
                                          CallOperation::AlgorithmLocate}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready, ErrorCode::AlgorithmFailure));
}

TEST_F(Core_VisionCoreLocateTests, Locate_UnsupportedMode_ReturnsInvalidConfigurationWithoutTouchingPorts) {
    const auto unsupportedMode = static_cast<LocateMode>(99);

    const auto outcome = core_->locate(LocateOptions{unsupportedMode});

    EXPECT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidConfiguration));
    EXPECT_EQ(light_->turnOnCalls(), 0U);
    EXPECT_EQ(light_->turnOffCalls(), 0U);
    EXPECT_EQ(camera_->captureCalls(), 0U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
    EXPECT_TRUE(callLog_.operations().empty());
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InvalidConfiguration));
}

TEST_F(Core_VisionCoreLocateTests, Locate_TurnOffFailsAfterPrimaryError_ReturnsCleanupErrorAndEmitsPriorityWarning) {
    const auto primaryError = MakeError(ErrorCode::HardwareFailure, "capture-failed");
    const auto cleanupError = MakeError(ErrorCode::HardwareFailure, "turn-off-failed");
    camera_->enqueueCapture(primaryError);
    light_->enqueueTurnOff(cleanupError);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(outcome, cleanupError));
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
    ASSERT_NE(sink_, nullptr);
    ASSERT_GE(sink_->events().size(), 3U);
    EXPECT_TRUE(HasDiagnosticHeader(sink_->events().back(),
                                    vision::core::DiagnosticSeverity::Warning,
                                    "locate.cleanupPriority"));
}

TEST_F(Core_VisionCoreLocateTests, Locate_TurnOffFailsAfterSuccess_ReturnsCleanupErrorWithoutPriorityWarning) {
    const auto cleanupError = MakeError(ErrorCode::HardwareFailure, "turn-off-failed-after-success");
    light_->enqueueTurnOff(cleanupError);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(outcome, cleanupError));
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
    EXPECT_EQ(callLog_.operations(),
              (std::vector<CallOperation>{CallOperation::LightTurnOn,
                                          CallOperation::CameraCapture,
                                          CallOperation::LightTurnOff}));
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
    ASSERT_NE(sink_, nullptr);
    for (const auto& event : sink_->events()) {
        EXPECT_NE(event.operation, "locate.cleanupPriority");
    }
}

} // namespace
} // namespace vision::test::core
