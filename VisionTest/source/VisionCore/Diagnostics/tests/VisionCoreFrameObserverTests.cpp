#include "VisionCore/support/VisionCoreFixture.h"

#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "VisionCore/support/OutcomeAssertions.h"

namespace vision::test::core {
namespace {

using vision::core::ErrorCode;
using vision::core::ILocateFrameObserver;
using vision::core::ImageFrames;
using vision::core::LocateMode;
using vision::core::LocateOptions;

static_assert(noexcept(std::declval<ILocateFrameObserver&>().onFrames(
    std::declval<const ImageFrames&>())));

class Core_VisionCoreFrameObserverTests : public VisionCoreFixture {
protected:
    void SetUp() override {
        composeDefaultCore(true, true);
    }
};

TEST_F(Core_VisionCoreFrameObserverTests, Locate_NoObserver_PreservesResultAndPortSequence) {
    composeDefaultCore(true, false);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(outcome));
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightTurnOn,
                                         CallOperation::CameraCapture,
                                         CallOperation::LightTurnOff,
                                         CallOperation::AlgorithmLocate}));
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
}

TEST_F(Core_VisionCoreFrameObserverTests, Locate_ValidFrames_NotifiesAfterLightOffBeforeAlgorithm) {
    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(outcome));
    ASSERT_NE(frameObserver_, nullptr);
    EXPECT_EQ(frameObserver_->calls(), 1U);
    EXPECT_FALSE(frameObserver_->discardedObservation());
    EXPECT_EQ(callLog_.operations(), (std::vector<CallOperation>{
                                         CallOperation::LightTurnOn,
                                         CallOperation::CameraCapture,
                                         CallOperation::LightTurnOff,
                                         CallOperation::FrameObserved,
                                         CallOperation::AlgorithmLocate}));
}

TEST_F(Core_VisionCoreFrameObserverTests, Locate_ObserverReceivesSynchronousReadOnlyOriginalFrames) {
    auto frames = MakeEdgeFrames();
    const unsigned char* const expectedData = frames.frames.front().image.data;
    const std::vector<unsigned char> expectedPixels(
        frames.frames.front().image.data,
        frames.frames.front().image.data +
            frames.frames.front().image.total() * frames.frames.front().image.elemSize());
    camera_->setDefaultCapture(frames);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(outcome));
    ASSERT_EQ(frameObserver_->observations().size(), 1U);
    ASSERT_EQ(algorithm_->observations().size(), 1U);
    ASSERT_EQ(algorithm_->observations().front().images.size(), 1U);
    const auto& observed = frameObserver_->observations().front();
    const auto& algorithmImage = algorithm_->observations().front().images.front();
    EXPECT_EQ(observed.data, expectedData);
    EXPECT_EQ(algorithmImage.data, expectedData);
    EXPECT_EQ(observed.pixels, expectedPixels);
    EXPECT_EQ(algorithmImage.pixels, expectedPixels);
}

TEST_F(Core_VisionCoreFrameObserverTests, Locate_UnsupportedMode_DoesNotNotifyOrTouchPorts) {
    const auto outcome = core_->locate(LocateOptions{static_cast<LocateMode>(99)});

    EXPECT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidConfiguration));
    EXPECT_EQ(frameObserver_->calls(), 0U);
    EXPECT_TRUE(callLog_.operations().empty());
}

TEST_F(Core_VisionCoreFrameObserverTests, Locate_LightTurnOffFails_DoesNotNotify) {
    light_->enqueueTurnOff(MakeError(ErrorCode::HardwareFailure, "turn-off"));

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(HasErrorCode(outcome, ErrorCode::HardwareFailure));
    EXPECT_EQ(frameObserver_->calls(), 0U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
}

TEST_F(Core_VisionCoreFrameObserverTests, Locate_FrameValidationFails_DoesNotNotify) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().image = cv::Mat{};
    camera_->setDefaultCapture(std::move(frames));

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidImage));
    EXPECT_EQ(frameObserver_->calls(), 0U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
}

TEST_F(Core_VisionCoreFrameObserverTests, Locate_FrameSetValidationFails_DoesNotNotify) {
    camera_->setDefaultCapture(ImageFrames{});

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    EXPECT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidImage));
    EXPECT_EQ(frameObserver_->calls(), 0U);
    EXPECT_EQ(algorithm_->locateCalls(), 0U);
}

} // namespace
} // namespace vision::test::core
