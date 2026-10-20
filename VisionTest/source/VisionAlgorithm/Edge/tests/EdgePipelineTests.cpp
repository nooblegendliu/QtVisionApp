#include "VisionAlgorithm/Edge/support/EdgePipelineTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <variant>
#include <vector>

namespace vision::test::algorithm::edge_pipeline {
namespace {

class Algorithm_EdgePipelineTests : public ::testing::Test {
protected:
    static void SetUpTestCase()
    {
        ASSERT_FALSE(findWorkspaceRoot().empty());
        config_ = loadDefaultEdgeConfig();
        visualizationStore_ =
            std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
        pipeline_ = makePipeline(config_, visualizationStore_);
    }

    static void TearDownTestCase()
    {
        pipeline_.reset();
        visualizationStore_.reset();
    }

    void SetUp() override
    {
        ASSERT_NE(pipeline_, nullptr);
        ASSERT_NE(visualizationStore_, nullptr);
        visualizationStore_->clear();
    }

    inline static vision::algorithm::detail::EdgeAlgorithmConfig config_;
    inline static std::shared_ptr<
        vision::algorithm::DemoEdgeVisualizationStore> visualizationStore_;
    inline static std::unique_ptr<
        vision::algorithm::detail::EdgePipeline> pipeline_;
};

TEST_F(
    Algorithm_EdgePipelineTests,
    Locate_AxisAlignedChamferedWaferWithArmShadows_ReturnsExpectedPoseAndSavesFactoryVisualization)
{
    const cv::Mat image = makeFactoryScene();
    const cv::Mat original = image.clone();
    const vision::core::EdgeAlgorithmInput input{std::cref(image)};

    const vision::core::AlgorithmOutcome outcome =
        pipeline_->locate(input, makeExecutionContext());

    ASSERT_TRUE(std::holds_alternative<vision::core::AlgorithmOutput>(outcome));
    const auto& output = std::get<vision::core::AlgorithmOutput>(outcome);
    ASSERT_TRUE(output.valid);
    EXPECT_NEAR(output.x, kImageCenterX, 2.0);
    EXPECT_NEAR(output.y, kImageCenterY, 2.0);
    EXPECT_NEAR(output.r, 0.0, 0.3);
    EXPECT_TRUE(imagesAreEqual(image, original));

    const auto visualization = visualizationStore_->snapshot();
    ASSERT_FALSE(visualization.rectifiedImage.empty());
    EXPECT_TRUE(imagesAreEqual(visualization.rectifiedImage, original));
    ASSERT_EQ(visualization.rois.size(), 8U);
    ASSERT_EQ(visualization.fittedLines.size(), 4U);
    ASSERT_EQ(visualization.corners.size(), 4U);

    const auto measurements = evaluatePostprocess(visualization, config_);
    ASSERT_TRUE(measurements.geometryAvailable);
    EXPECT_TRUE(measurements.edgeModeQualified);
    EXPECT_TRUE(measurements.offsetResultQualified);
    EXPECT_TRUE(measurements.finalQualified);

    std::filesystem::path outputPath;
    ASSERT_TRUE(saveFactoryResult(
        original,
        visualization,
        config_,
        output,
        "edge_postprocess_axis_ok.png",
        outputPath));
    std::cout << "EdgePipeline axis OK visualization: "
              << outputPath.string() << '\n';
}

TEST_F(
    Algorithm_EdgePipelineTests,
    Locate_RotatedChamferedWaferWithArmShadows_ReturnsExpectedPoseAndSavesFactoryVisualization)
{
    constexpr double expectedAngleDeg = -3.7;
    const cv::Mat image = makeFactoryScene(
        FactorySceneSpec{{kImageCenterX, kImageCenterY}, expectedAngleDeg});
    const cv::Mat original = image.clone();
    const vision::core::EdgeAlgorithmInput input{std::cref(image)};

    const vision::core::AlgorithmOutcome outcome =
        pipeline_->locate(input, makeExecutionContext());

    ASSERT_TRUE(std::holds_alternative<vision::core::AlgorithmOutput>(outcome));
    const auto& output = std::get<vision::core::AlgorithmOutput>(outcome);
    ASSERT_TRUE(output.valid);
    EXPECT_NEAR(output.x, kImageCenterX, 3.0);
    EXPECT_NEAR(output.y, kImageCenterY, 3.0);
    EXPECT_NEAR(output.r, expectedAngleDeg, 0.5);
    EXPECT_TRUE(imagesAreEqual(image, original));

    const auto visualization = visualizationStore_->snapshot();
    const auto measurements = evaluatePostprocess(visualization, config_);
    ASSERT_TRUE(measurements.geometryAvailable);
    EXPECT_TRUE(measurements.finalQualified);
    EXPECT_NEAR(measurements.angleDeg, expectedAngleDeg, 0.5);

    std::filesystem::path outputPath;
    ASSERT_TRUE(saveFactoryResult(
        original,
        visualization,
        config_,
        output,
        "edge_postprocess_rotated_ok.png",
        outputPath));
    std::cout << "EdgePipeline rotated OK visualization: "
              << outputPath.string() << '\n';
}

TEST(
    Algorithm_EdgePipelinePhysicalPoseTests,
    Locate_PerspectiveCalibration_RecomputesCenterAngleAndAngleQualityInPhysicalCoordinates)
{
    const cv::Mat imageToPhysical =
        (cv::Mat_<double>(3, 3) <<
            0.010, -0.002, -20.0,
            0.003,  0.015,  10.0,
            0.00002, -0.00001, 1.0);
    auto config = loadDefaultEdgeConfig();
    config.quality.absoluteAngleDeg = {10.0, 12.0};

    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    auto pipeline = makePipeline(
        config,
        loadGeneratedCenterGeometry(imageToPhysical),
        visualizationStore);
    const cv::Mat image = makeFactoryScene(
        FactorySceneSpec{{kImageCenterX, kImageCenterY}, 0.0, false});

    const vision::core::AlgorithmOutcome outcome = pipeline->locate(
        vision::core::EdgeAlgorithmInput{std::cref(image)},
        makeExecutionContext());

    ASSERT_TRUE(std::holds_alternative<vision::core::AlgorithmOutput>(outcome));
    const auto& output = std::get<vision::core::AlgorithmOutput>(outcome);
    ASSERT_TRUE(output.valid);

    const double halfWidth = kWaferWidth * 0.5;
    const double halfHeight = kWaferHeight * 0.5;
    const std::vector<cv::Point2d> pixelCorners{
        {kImageCenterX - halfWidth, kImageCenterY - halfHeight},
        {kImageCenterX + halfWidth, kImageCenterY - halfHeight},
        {kImageCenterX + halfWidth, kImageCenterY + halfHeight},
        {kImageCenterX - halfWidth, kImageCenterY + halfHeight}};
    std::vector<cv::Point2d> physicalCorners;
    cv::perspectiveTransform(
        pixelCorners,
        physicalCorners,
        imageToPhysical);
    ASSERT_EQ(physicalCorners.size(), 4U);

    const cv::Point2d expectedCenter =
        (physicalCorners[0] + physicalCorners[1] +
         physicalCorners[2] + physicalCorners[3]) * 0.25;
    const cv::Point2d leftMidpoint =
        (physicalCorners[0] + physicalCorners[3]) * 0.5;
    const cv::Point2d rightMidpoint =
        (physicalCorners[1] + physicalCorners[2]) * 0.5;
    const cv::Point2d expectedDirection = rightMidpoint - leftMidpoint;
    const double expectedAngleDeg = std::atan2(
        expectedDirection.y,
        expectedDirection.x) * 180.0 / CV_PI;

    EXPECT_NEAR(output.x, expectedCenter.x, 0.1);
    EXPECT_NEAR(output.y, expectedCenter.y, 0.1);
    EXPECT_NEAR(output.r, expectedAngleDeg, 0.2);

    std::vector<cv::Point2d> mappedImageCenter;
    cv::perspectiveTransform(
        std::vector<cv::Point2d>{{kImageCenterX, kImageCenterY}},
        mappedImageCenter,
        imageToPhysical);
    ASSERT_EQ(mappedImageCenter.size(), 1U);
    EXPECT_GT(std::abs(output.x - mappedImageCenter.front().x), 0.5);
}

TEST_F(Algorithm_EdgePipelineTests, Locate_UniformImage_ReturnsNgWithoutVisualization)
{
    const cv::Mat image(kImageHeight, kImageWidth, CV_8UC1, cv::Scalar(230));
    const vision::core::EdgeAlgorithmInput input{std::cref(image)};

    const vision::core::AlgorithmOutcome outcome =
        pipeline_->locate(input, makeExecutionContext());

    ASSERT_TRUE(std::holds_alternative<vision::core::AlgorithmOutput>(outcome));
    const auto& output = std::get<vision::core::AlgorithmOutput>(outcome);
    EXPECT_FALSE(output.valid);
    EXPECT_DOUBLE_EQ(output.x, 0.0);
    EXPECT_DOUBLE_EQ(output.y, 0.0);
    EXPECT_DOUBLE_EQ(output.r, 0.0);
    EXPECT_TRUE(visualizationStore_->snapshot().rectifiedImage.empty());
}

TEST_F(
    Algorithm_EdgePipelineTests,
    Locate_ImageSizeDoesNotMatchCalibration_ReturnsInvalidImageWithoutVisualization)
{
    const cv::Mat image(3036, 4024, CV_8UC1, cv::Scalar(230));
    const vision::core::EdgeAlgorithmInput input{std::cref(image)};

    const vision::core::AlgorithmOutcome outcome =
        pipeline_->locate(input, makeExecutionContext());

    const auto* error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidImage);
    EXPECT_TRUE(visualizationStore_->snapshot().rectifiedImage.empty());
}

TEST_F(
    Algorithm_EdgePipelineTests,
    Locate_DeadlineAlreadyExpired_ReturnsAlgorithmTimeoutWithoutVisualization)
{
    const cv::Mat image(kImageHeight, kImageWidth, CV_8UC1, cv::Scalar(230));
    const vision::core::EdgeAlgorithmInput input{std::cref(image)};
    const auto now = std::chrono::steady_clock::now();
    const vision::algorithm::detail::ExecutionContext expiredContext{
        now - std::chrono::milliseconds(2),
        now - std::chrono::milliseconds(1)};

    const vision::core::AlgorithmOutcome outcome =
        pipeline_->locate(input, expiredContext);

    const auto* error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::AlgorithmTimeout);
    EXPECT_TRUE(visualizationStore_->snapshot().rectifiedImage.empty());
}

} // namespace
} // namespace vision::test::algorithm::edge_pipeline
