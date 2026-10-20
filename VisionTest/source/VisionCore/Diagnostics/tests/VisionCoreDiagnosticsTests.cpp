#include "VisionCore/support/VisionCoreFixture.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "VisionCore/support/CoreAssertions.h"
#include "VisionCore/support/OutcomeAssertions.h"

namespace vision::test::core {
namespace {

using vision::core::CoreState;
using vision::core::DiagnosticEvent;
using vision::core::DiagnosticSeverity;
using vision::core::ErrorCode;
using vision::core::LocateMode;
using vision::core::LocateOptions;
using vision::core::VisionCore;

class Core_VisionCoreDiagnosticsFixture : public VisionCoreFixture {};

const DiagnosticEvent* FindEvent(const std::vector<DiagnosticEvent>& events, const std::string& operation) {
    const auto found = std::find_if(events.begin(), events.end(), [&operation](const DiagnosticEvent& event) {
        return event.operation == operation;
    });
    return found == events.end() ? nullptr : &*found;
}

TEST(Core_VisionCoreDiagnosticsTests, Construction_MissingPort_EmitsConstructionErrorWithoutFrameContext) {
    CallLog callLog;
    FakeDiagnosticSink sink;
    auto light = std::make_unique<FakeLight>(callLog);
    auto algorithm = std::make_unique<FakeLocateAlgorithm>(callLog);
    VisionCore core(nullptr, std::move(light), std::move(algorithm), &sink);

    ASSERT_EQ(sink.events().size(), 1U);
    EXPECT_TRUE(HasDiagnosticHeader(sink.events().front(), DiagnosticSeverity::Error, "construction"));
    EXPECT_TRUE(sink.events().front().frames.empty());
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_PortError_EmitsVisionCoreErrorWithStableOperation) {
    light_->enqueueTurnOn(MakeError(ErrorCode::HardwareUnavailable, "diagnostic-turn-on"));

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::HardwareUnavailable));
    ASSERT_NE(sink_, nullptr);
    const DiagnosticEvent* event = FindEvent(sink_->events(), "light.turnOn");
    ASSERT_NE(event, nullptr);
    EXPECT_TRUE(HasDiagnosticHeader(*event, DiagnosticSeverity::Error, "light.turnOn"));
    EXPECT_TRUE(event->frames.empty());
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_UnsupportedMode_EmitsOptionsErrorWithoutTouchingPorts) {
    const auto unsupportedMode = static_cast<LocateMode>(99);

    const auto outcome = core_->locate(LocateOptions{unsupportedMode});

    ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidConfiguration));
    ASSERT_NE(sink_, nullptr);
    const DiagnosticEvent* event = FindEvent(sink_->events(), "locate.options");
    ASSERT_NE(event, nullptr);
    EXPECT_TRUE(HasDiagnosticHeader(*event, DiagnosticSeverity::Error, "locate.options"));
    EXPECT_TRUE(event->frames.empty());
    EXPECT_TRUE(callLog_.operations().empty());
    EXPECT_EQ(light_->turnOnCalls(), 0U);
    EXPECT_EQ(light_->turnOffCalls(), 0U);
    EXPECT_EQ(camera_->captureCalls(), 0U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_SingleInvalidFrame_EmitsRelatedFrameDiagnosticContext) {
    auto frames = MakeEdgeFrames();
    const auto expectedFrame = frames.frames.front();
    frames.frames.front().cameraId.clear();
    const auto expectedInvalidFrame = frames.frames.front();
    camera_->setDefaultCapture(frames);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidImage));
    ASSERT_NE(sink_, nullptr);
    const DiagnosticEvent* event = FindEvent(sink_->events(), "image.validate");
    ASSERT_NE(event, nullptr);
    ASSERT_EQ(event->frames.size(), 1U);
    EXPECT_TRUE(HasFrameContext(event->frames.front(), expectedInvalidFrame));
    EXPECT_NE(expectedFrame.cameraId, expectedInvalidFrame.cameraId);
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_MarkPointAlgorithmError_EmitsAllFrameContexts) {
    auto frames = MakeMarkPointFrames();
    const auto expectedFrames = frames.frames;
    camera_->setDefaultCapture(frames);
    algorithm_->setDefaultOutcome(MakeError(ErrorCode::AlgorithmFailure, "markpoint-diagnostic"));

    const auto outcome = core_->locate(LocateOptions{LocateMode::MarkPoint});

    ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::AlgorithmFailure));
    ASSERT_NE(sink_, nullptr);
    const DiagnosticEvent* event = FindEvent(sink_->events(), "algorithm.locate");
    ASSERT_NE(event, nullptr);
    ASSERT_EQ(event->frames.size(), expectedFrames.size());
    for (std::size_t index = 0U; index < expectedFrames.size(); ++index) {
        EXPECT_TRUE(HasFrameContext(event->frames[index], expectedFrames[index]));
    }
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_NonFiniteResult_EmitsResultValidationEventWithEdgeFrameContext) {
    auto frames = MakeEdgeFrames();
    const auto expectedFrame = frames.frames.front();
    camera_->setDefaultCapture(frames);
    algorithm_->setDefaultOutcome(MakeOkOutput(std::numeric_limits<double>::infinity(), 2.0, 3.0));

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidResult));
    const DiagnosticEvent* event = FindEvent(sink_->events(), "result.validate");
    ASSERT_NE(event, nullptr);
    EXPECT_TRUE(HasDiagnosticHeader(*event, DiagnosticSeverity::Error, "result.validate"));
    ASSERT_EQ(event->frames.size(), 1U);
    EXPECT_TRUE(HasFrameContext(event->frames.front(), expectedFrame));
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_CleanupOverridesPrimaryError_EmitsPrimaryErrorAndPriorityWarning) {
    camera_->enqueueCapture(MakeError(ErrorCode::HardwareFailure, "primary-capture-error"));
    light_->enqueueTurnOff(MakeError(ErrorCode::HardwareFailure, "cleanup-turn-off-error"));

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::HardwareFailure));
    ASSERT_NE(sink_, nullptr);
    const DiagnosticEvent* primaryEvent = FindEvent(sink_->events(), "camera.capture");
    const DiagnosticEvent* cleanupEvent = FindEvent(sink_->events(), "light.turnOff");
    const DiagnosticEvent* warningEvent = FindEvent(sink_->events(), "locate.cleanupPriority");
    ASSERT_NE(primaryEvent, nullptr);
    ASSERT_NE(cleanupEvent, nullptr);
    ASSERT_NE(warningEvent, nullptr);
    EXPECT_TRUE(HasDiagnosticHeader(*primaryEvent, DiagnosticSeverity::Error, "camera.capture"));
    EXPECT_TRUE(HasDiagnosticHeader(*cleanupEvent, DiagnosticSeverity::Error, "light.turnOff"));
    EXPECT_TRUE(HasDiagnosticHeader(*warningEvent, DiagnosticSeverity::Warning, "locate.cleanupPriority"));
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_DiagnosticEventsAreCopiesOfFrameMetadata) {
    auto frames = MakeMarkPointFrames();
    const auto expectedFrames = frames.frames;
    camera_->setDefaultCapture(frames);
    algorithm_->setDefaultOutcome(MakeError(ErrorCode::AlgorithmFailure, "copy-diagnostic"));

    const auto outcome = core_->locate(LocateOptions{LocateMode::MarkPoint});
    ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::AlgorithmFailure));

    for (auto& frame : frames.frames) {
        frame.cameraId = "mutated-after-locate";
        frame.frameNumber = 0U;
    }
    const DiagnosticEvent* event = FindEvent(sink_->events(), "algorithm.locate");
    ASSERT_NE(event, nullptr);
    ASSERT_EQ(event->frames.size(), expectedFrames.size());
    for (std::size_t index = 0U; index < expectedFrames.size(); ++index) {
        EXPECT_TRUE(HasFrameContext(event->frames[index], expectedFrames[index]));
    }
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_DiscardingDiagnosticSinkWrites_DoesNotAlterOutcomeOrPortCalls) {
    ASSERT_NE(sink_, nullptr);
    sink_->setDiscardWrites(true);
    const auto expectedError = MakeError(ErrorCode::HardwareFailure, "discarded-diagnostic");
    camera_->enqueueCapture(expectedError);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(IsOriginalError(outcome, expectedError));
    EXPECT_EQ(camera_->captureCalls(), 1U);
    EXPECT_EQ(light_->turnOffCalls(), 1U);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::HardwareFailure));
    EXPECT_GT(sink_->submitAttempts(), 0U);
    EXPECT_TRUE(sink_->events().empty());
}

TEST_F(Core_VisionCoreDiagnosticsFixture, Locate_WithoutDiagnosticSink_PreservesBusinessOutcomeAndPortCalls) {
    const auto withSinkOutcome = core_->locate(LocateOptions{LocateMode::Edge});
    ASSERT_TRUE(IsSuccess(withSinkOutcome));
    const auto withSinkCalls = callLog_.operations();

    core_.reset();
    composeDefaultCore(false);
    const auto withoutSinkOutcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(withoutSinkOutcome));
    EXPECT_TRUE(IsOkResult(SuccessValue(withSinkOutcome),
                           SuccessValue(withoutSinkOutcome).x,
                           SuccessValue(withoutSinkOutcome).y,
                           SuccessValue(withoutSinkOutcome).r));
    EXPECT_EQ(callLog_.operations(), withSinkCalls);
    EXPECT_TRUE(HasStatus(core_->status(), CoreState::Ready));
}

} // namespace
} // namespace vision::test::core
