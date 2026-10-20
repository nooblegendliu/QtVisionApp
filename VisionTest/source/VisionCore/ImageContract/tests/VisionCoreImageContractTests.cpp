#include "VisionCore/support/VisionCoreFixture.h"

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "VisionCore/support/CoreAssertions.h"
#include "VisionCore/support/OutcomeAssertions.h"

namespace vision::test::core {
namespace {

using vision::core::CameraRole;
using vision::core::CoreState;
using vision::core::ErrorCode;
using vision::core::ImageFrames;
using vision::core::LocateMode;
using vision::core::LocateOptions;

class Core_VisionCoreImageContractTests : public VisionCoreFixture {
protected:
    void expectInvalidImage(LocateMode mode) {
        const auto outcome = core_->locate(LocateOptions{mode});
        ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidImage));
        EXPECT_EQ(algorithm_->locateCalls(), 0U);
        EXPECT_EQ(light_->turnOffCalls(), 1U);
        EXPECT_TRUE(HasStatus(core_->status(), CoreState::Faulted, ErrorCode::InvalidImage));
        ASSERT_NE(sink_, nullptr);
        ASSERT_FALSE(sink_->events().empty());
        EXPECT_EQ(sink_->events().front().severity, vision::core::DiagnosticSeverity::Error);
    }
};

TEST_F(Core_VisionCoreImageContractTests, Locate_EmptyImage_ReturnsInvalidImageWithoutCallingAlgorithm) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().image = cv::Mat{};
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_NonGrayImage_ReturnsInvalidImageWithoutCallingAlgorithm) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().image = cv::Mat(2, 2, CV_8UC3);
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_NonContinuousImage_ReturnsInvalidImageWithoutCallingAlgorithm) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().image = MakeNonContinuousGrayImage();
    ASSERT_FALSE(frames.frames.front().image.isContinuous());
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_DefaultTimestamp_ReturnsInvalidImageWithoutCallingAlgorithm) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().capturedAt = std::chrono::steady_clock::time_point{};
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_EmptyCameraId_ReturnsInvalidImageWithoutCallingAlgorithm) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().cameraId.clear();
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_UnknownCameraRole_ReturnsInvalidImageWithoutCallingAlgorithm) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().cameraRole = CameraRole::Unknown;
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_UnrecognizedCameraRole_ReturnsInvalidImageWithoutCallingAlgorithm) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().cameraRole = static_cast<CameraRole>(99);
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_ZeroFrameNumberAndSmallImage_AreValidPublicFrames) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().frameNumber = 0U;
    frames.frames.front().image = MakeGrayImage(1, 1, 17U);
    ASSERT_TRUE(IsValidPublicFrame(frames.frames.front()));
    camera_->setDefaultCapture(frames);

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(outcome));
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_EdgeFrameSetHasNoFrames_ReturnsInvalidImage) {
    camera_->setDefaultCapture(ImageFrames{});

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_EdgeFrameSetHasExtraFrames_ReturnsInvalidImage) {
    auto frames = MakeEdgeFrames();
    frames.frames.push_back(MakeFrame(CameraRole::TopLeft, "extra-camera"));
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_EdgeFrameIsNotCenter_ReturnsInvalidImage) {
    auto frames = MakeEdgeFrames();
    frames.frames.front().cameraRole = CameraRole::TopLeft;
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::Edge);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_MarkPointHasTooFewFrames_ReturnsInvalidImage) {
    auto frames = MakeMarkPointFrames();
    frames.frames.pop_back();
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::MarkPoint);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_MarkPointHasTooManyFrames_ReturnsInvalidImage) {
    auto frames = MakeMarkPointFrames();
    frames.frames.push_back(MakeFrame(CameraRole::Center, "center-extra"));
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::MarkPoint);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_MarkPointContainsCenter_ReturnsInvalidImage) {
    auto frames = MakeMarkPointFrames();
    frames.frames.front().cameraRole = CameraRole::Center;
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::MarkPoint);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_MarkPointContainsDuplicateRole_ReturnsInvalidImage) {
    auto frames = MakeMarkPointFrames();
    frames.frames.back().cameraRole = CameraRole::TopLeft;
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::MarkPoint);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_MarkPointHasDuplicateCameraId_ReturnsInvalidImage) {
    auto frames = MakeMarkPointFrames();
    frames.frames.back().cameraId = frames.frames.front().cameraId;
    camera_->setDefaultCapture(frames);

    expectInvalidImage(LocateMode::MarkPoint);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_MarkPointWithDifferentOrZeroFrameNumbers_UsesAlgorithm) {
    auto frames = MakeMarkPointFrames();
    frames.frames[0].frameNumber = 0U;
    frames.frames[1].frameNumber = 73U;
    frames.frames[2].frameNumber = 501U;
    frames.frames[3].frameNumber = 2U;
    camera_->setDefaultCapture(frames);

    const auto outcome = core_->locate(LocateOptions{LocateMode::MarkPoint});

    ASSERT_TRUE(IsSuccess(outcome));
    EXPECT_EQ(algorithm_->locateCalls(), 1U);
}

TEST_F(Core_VisionCoreImageContractTests, Locate_MarkPointIllegalFrameAtEveryPosition_NeverCallsAlgorithm) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        auto frames = MakeMarkPointFrames();
        frames.frames[index].cameraId.clear();
        camera_->setDefaultCapture(frames);

        const auto outcome = core_->locate(LocateOptions{LocateMode::MarkPoint});
        ASSERT_TRUE(HasErrorCode(outcome, ErrorCode::InvalidImage));
        EXPECT_EQ(algorithm_->locateCalls(), 0U);
        ASSERT_TRUE(IsSuccess(core_->recover()));
        algorithm_->clearObservations();
    }
}

TEST_F(Core_VisionCoreImageContractTests, Locate_NormalImagesUseOriginalBuffersAndRemainUnchanged) {
    cv::Mat mutableSource = MakeGrayImage(3, 4, 12U);
    auto frames = MakeEdgeFrames();
    frames.frames.front().image = mutableSource.clone();
    const std::vector<unsigned char> expectedPixels(
        frames.frames.front().image.data,
        frames.frames.front().image.data + frames.frames.front().image.total() * frames.frames.front().image.elemSize());
    const unsigned char* const expectedData = frames.frames.front().image.data;
    camera_->setDefaultCapture(frames);
    algorithm_->onLocate = [&mutableSource] { mutableSource.setTo(cv::Scalar(99)); };

    const auto outcome = core_->locate(LocateOptions{LocateMode::Edge});

    ASSERT_TRUE(IsSuccess(outcome));
    ASSERT_EQ(algorithm_->observations().size(), 1U);
    const ImageObservation& observation = algorithm_->observations().front().images.front();
    EXPECT_EQ(observation.data, expectedData);
    EXPECT_NE(observation.data, mutableSource.data);
    EXPECT_EQ(observation.pixels, expectedPixels);
    EXPECT_EQ(frames.frames.front().image.rows, observation.rows);
    EXPECT_EQ(frames.frames.front().image.cols, observation.columns);
    EXPECT_EQ(frames.frames.front().image.type(), observation.type);
    EXPECT_EQ(frames.frames.front().image.step, observation.step);
}

} // namespace
} // namespace vision::test::core
