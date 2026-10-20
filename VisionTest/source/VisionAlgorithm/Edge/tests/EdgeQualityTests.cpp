#include "VisionAlgorithm/Edge/support/EdgePipelineTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

namespace vision::test::algorithm::edge_pipeline {
namespace {

using vision::algorithm::detail::EdgeAlgorithmConfig;
using vision::algorithm::detail::MeasurementRangeConfig;

enum class MeasurementTarget {
    Top,
    Right,
    Bottom,
    Left,
    DiagonalTopLeftToBottomRight,
    DiagonalTopRightToBottomLeft
};

enum class RangeFailure {
    BelowLower,
    AboveUpper
};

struct MeasurementRangeFailureCase {
    MeasurementTarget target;
    RangeFailure failure;
    const char* name;
};

[[nodiscard]] MeasurementRangeConfig& selectRange(
    EdgeAlgorithmConfig& config,
    const MeasurementTarget target)
{
    switch (target) {
    case MeasurementTarget::Top:
        return config.quality.topLength;
    case MeasurementTarget::Right:
        return config.quality.rightLength;
    case MeasurementTarget::Bottom:
        return config.quality.bottomLength;
    case MeasurementTarget::Left:
        return config.quality.leftLength;
    case MeasurementTarget::DiagonalTopLeftToBottomRight:
        return config.quality.diagonalTopLeftToBottomRight;
    case MeasurementTarget::DiagonalTopRightToBottomLeft:
        return config.quality.diagonalTopRightToBottomLeft;
    }
    throw std::logic_error("Unsupported Edge measurement target.");
}

[[nodiscard]] bool selectedMeasurementQualified(
    const EdgePostprocessMeasurements& measurements,
    const MeasurementTarget target)
{
    switch (target) {
    case MeasurementTarget::Top:
        return measurements.topQualified;
    case MeasurementTarget::Right:
        return measurements.rightQualified;
    case MeasurementTarget::Bottom:
        return measurements.bottomQualified;
    case MeasurementTarget::Left:
        return measurements.leftQualified;
    case MeasurementTarget::DiagonalTopLeftToBottomRight:
        return measurements.diagonalTopLeftToBottomRightQualified;
    case MeasurementTarget::DiagonalTopRightToBottomLeft:
        return measurements.diagonalTopRightToBottomLeftQualified;
    }
    return false;
}

[[nodiscard]] vision::core::AlgorithmOutcome locateWithConfig(
    const EdgeAlgorithmConfig& config,
    const cv::Mat& image,
    const std::shared_ptr<
        vision::algorithm::DemoEdgeVisualizationStore>& visualizationStore)
{
    auto pipeline = makePipeline(config, visualizationStore);
    return pipeline->locate(
        vision::core::EdgeAlgorithmInput{std::cref(image)},
        makeExecutionContext());
}

void expectNgWithZeroPose(const vision::core::AlgorithmOutput& output)
{
    EXPECT_FALSE(output.valid);
    EXPECT_DOUBLE_EQ(output.x, 0.0);
    EXPECT_DOUBLE_EQ(output.y, 0.0);
    EXPECT_DOUBLE_EQ(output.r, 0.0);
}

[[nodiscard]] vision::core::AlgorithmOutput requireAlgorithmOutput(
    const vision::core::AlgorithmOutcome& outcome)
{
    const auto* output = std::get_if<vision::core::AlgorithmOutput>(&outcome);
    EXPECT_NE(output, nullptr);
    return output != nullptr ? *output : vision::core::AlgorithmOutput{};
}

TEST(
    Algorithm_EdgeQualityConfigTests,
    LoadConfig_CurrentExample_LoadsWideFirstStageQualityRanges)
{
    const EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    const std::array<MeasurementRangeConfig, 7> ranges{
        config.quality.topLength,
        config.quality.rightLength,
        config.quality.bottomLength,
        config.quality.leftLength,
        config.quality.diagonalTopLeftToBottomRight,
        config.quality.diagonalTopRightToBottomLeft,
        config.quality.absoluteAngleDeg};

    for (std::size_t index = 0; index < 6U; ++index) {
        EXPECT_DOUBLE_EQ(ranges[index].lower, 0.0);
        EXPECT_DOUBLE_EQ(ranges[index].upper, 1000000000.0);
    }
    EXPECT_DOUBLE_EQ(config.quality.absoluteAngleDeg.lower, 0.0);
    EXPECT_DOUBLE_EQ(config.quality.absoluteAngleDeg.upper, 90.0);
    EXPECT_DOUBLE_EQ(config.quality.maximumCenterOffset, 1000000000.0);
}

class Algorithm_EdgeMeasurementRangeTests :
    public ::testing::TestWithParam<MeasurementRangeFailureCase> {};

TEST_P(
    Algorithm_EdgeMeasurementRangeTests,
    Locate_OnlySelectedSideOrDiagonalIsOutsideRange_ReturnsFinalNg)
{
    EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    const MeasurementRangeFailureCase testCase = GetParam();
    MeasurementRangeConfig& selectedRange = selectRange(config, testCase.target);
    selectedRange = testCase.failure == RangeFailure::BelowLower
        ? MeasurementRangeConfig{100000000.0, 1000000000.0}
        : MeasurementRangeConfig{0.0, 1.0};

    const cv::Mat image = makeFactoryScene();
    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    const auto output = requireAlgorithmOutput(
        locateWithConfig(config, image, visualizationStore));

    expectNgWithZeroPose(output);
    const auto measurements = evaluatePostprocess(
        visualizationStore->snapshot(),
        config);
    ASSERT_TRUE(measurements.geometryAvailable);
    EXPECT_FALSE(selectedMeasurementQualified(measurements, testCase.target));
    EXPECT_FALSE(measurements.edgeModeQualified);
    EXPECT_TRUE(measurements.offsetResultQualified);
    EXPECT_FALSE(measurements.finalQualified);
}

INSTANTIATE_TEST_CASE_P(
    EverySideAndDiagonal,
    Algorithm_EdgeMeasurementRangeTests,
    ::testing::Values(
        MeasurementRangeFailureCase{
            MeasurementTarget::Top,
            RangeFailure::BelowLower,
            "TopBelowLower"},
        MeasurementRangeFailureCase{
            MeasurementTarget::Top,
            RangeFailure::AboveUpper,
            "TopAboveUpper"},
        MeasurementRangeFailureCase{
            MeasurementTarget::Right,
            RangeFailure::BelowLower,
            "RightBelowLower"},
        MeasurementRangeFailureCase{
            MeasurementTarget::Right,
            RangeFailure::AboveUpper,
            "RightAboveUpper"},
        MeasurementRangeFailureCase{
            MeasurementTarget::Bottom,
            RangeFailure::BelowLower,
            "BottomBelowLower"},
        MeasurementRangeFailureCase{
            MeasurementTarget::Bottom,
            RangeFailure::AboveUpper,
            "BottomAboveUpper"},
        MeasurementRangeFailureCase{
            MeasurementTarget::Left,
            RangeFailure::BelowLower,
            "LeftBelowLower"},
        MeasurementRangeFailureCase{
            MeasurementTarget::Left,
            RangeFailure::AboveUpper,
            "LeftAboveUpper"},
        MeasurementRangeFailureCase{
            MeasurementTarget::DiagonalTopLeftToBottomRight,
            RangeFailure::BelowLower,
            "Diagonal1BelowLower"},
        MeasurementRangeFailureCase{
            MeasurementTarget::DiagonalTopLeftToBottomRight,
            RangeFailure::AboveUpper,
            "Diagonal1AboveUpper"},
        MeasurementRangeFailureCase{
            MeasurementTarget::DiagonalTopRightToBottomLeft,
            RangeFailure::BelowLower,
            "Diagonal2BelowLower"},
        MeasurementRangeFailureCase{
            MeasurementTarget::DiagonalTopRightToBottomLeft,
            RangeFailure::AboveUpper,
            "Diagonal2AboveUpper"}),
    [](const ::testing::TestParamInfo<MeasurementRangeFailureCase>& info) {
        return std::string(info.param.name);
    });

class Algorithm_EdgeAbsoluteAngleTests : public ::testing::TestWithParam<double> {};

TEST_P(
    Algorithm_EdgeAbsoluteAngleTests,
    Locate_PositiveOrNegativeAngleMagnitudeIsInsideRange_ReturnsOk)
{
    EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    config.quality.absoluteAngleDeg = {3.0, 4.5};
    const double angleDeg = GetParam();
    const cv::Mat image = makeFactoryScene(
        FactorySceneSpec{{kImageCenterX, kImageCenterY}, angleDeg});
    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    const auto output = requireAlgorithmOutput(
        locateWithConfig(config, image, visualizationStore));

    EXPECT_TRUE(output.valid);
    EXPECT_NEAR(output.r, angleDeg, 0.5);
    const auto measurements = evaluatePostprocess(
        visualizationStore->snapshot(),
        config);
    EXPECT_TRUE(measurements.angleQualified);
    EXPECT_TRUE(measurements.offsetResultQualified);
    EXPECT_TRUE(measurements.finalQualified);
}

INSTANTIATE_TEST_CASE_P(
    SignedAnglesUseMagnitude,
    Algorithm_EdgeAbsoluteAngleTests,
    ::testing::Values(3.7, -3.7));

TEST(
    Algorithm_EdgeQualityTests,
    Locate_AngleMagnitudeIsBelowLowerLimit_ReturnsNg)
{
    EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    config.quality.absoluteAngleDeg = {4.0, 90.0};
    const cv::Mat image = makeFactoryScene(
        FactorySceneSpec{{kImageCenterX, kImageCenterY}, 3.7});
    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    const auto output = requireAlgorithmOutput(
        locateWithConfig(config, image, visualizationStore));

    expectNgWithZeroPose(output);
    const auto measurements = evaluatePostprocess(
        visualizationStore->snapshot(),
        config);
    ASSERT_TRUE(measurements.geometryAvailable);
    EXPECT_TRUE(measurements.edgeModeQualified);
    EXPECT_FALSE(measurements.angleQualified);
    EXPECT_FALSE(measurements.offsetResultQualified);
}

TEST(
    Algorithm_EdgeQualityTests,
    Locate_AngleMagnitudeExceedsUpperLimit_ReturnsNgAndSavesRedAngleVisualization)
{
    EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    config.quality.absoluteAngleDeg = {0.0, 3.0};
    const cv::Mat image = makeFactoryScene(
        FactorySceneSpec{{kImageCenterX, kImageCenterY}, -3.7});
    const cv::Mat original = image.clone();
    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    const auto output = requireAlgorithmOutput(
        locateWithConfig(config, image, visualizationStore));

    expectNgWithZeroPose(output);
    const auto visualization = visualizationStore->snapshot();
    const auto measurements = evaluatePostprocess(visualization, config);
    ASSERT_TRUE(measurements.geometryAvailable);
    EXPECT_TRUE(measurements.edgeModeQualified);
    EXPECT_FALSE(measurements.angleQualified);
    EXPECT_TRUE(measurements.centerOffsetQualified);
    EXPECT_FALSE(measurements.offsetResultQualified);

    std::filesystem::path outputPath;
    ASSERT_TRUE(saveFactoryResult(
        original,
        visualization,
        config,
        output,
        "edge_postprocess_angle_ng.png",
        outputPath));
    std::cout << "EdgePipeline angle NG visualization: "
              << outputPath.string() << '\n';
}

TEST(
    Algorithm_EdgeQualityTests,
    Locate_CenterOffsetExceedsMaximum_ReturnsNgAndSavesRedOffsetVisualization)
{
    EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    config.quality.maximumCenterOffset = 50.0;
    const cv::Point2d shiftedCenter(kImageCenterX + 120.0, kImageCenterY - 80.0);
    const cv::Mat image = makeFactoryScene(
        FactorySceneSpec{shiftedCenter, 0.0});
    const cv::Mat original = image.clone();
    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    const auto output = requireAlgorithmOutput(
        locateWithConfig(config, image, visualizationStore));

    expectNgWithZeroPose(output);
    const auto visualization = visualizationStore->snapshot();
    const auto measurements = evaluatePostprocess(visualization, config);
    ASSERT_TRUE(measurements.geometryAvailable);
    EXPECT_TRUE(measurements.edgeModeQualified);
    EXPECT_TRUE(measurements.angleQualified);
    EXPECT_FALSE(measurements.centerOffsetQualified);
    EXPECT_FALSE(measurements.offsetResultQualified);
    EXPECT_NEAR(measurements.centerOffset, std::hypot(120.0, 80.0), 3.0);

    std::filesystem::path outputPath;
    ASSERT_TRUE(saveFactoryResult(
        original,
        visualization,
        config,
        output,
        "edge_postprocess_center_offset_ng.png",
        outputPath));
    std::cout << "EdgePipeline center-offset NG visualization: "
              << outputPath.string() << '\n';
}

TEST(
    Algorithm_EdgeQualityTests,
    Locate_CenterOffsetIsInsideMaximum_ReturnsActualShiftedPose)
{
    EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    config.quality.maximumCenterOffset = 200.0;
    const cv::Point2d shiftedCenter(kImageCenterX + 120.0, kImageCenterY - 80.0);
    const cv::Mat image = makeFactoryScene(
        FactorySceneSpec{shiftedCenter, 0.0});
    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    const auto output = requireAlgorithmOutput(
        locateWithConfig(config, image, visualizationStore));

    ASSERT_TRUE(output.valid);
    EXPECT_NEAR(output.x, shiftedCenter.x, 3.0);
    EXPECT_NEAR(output.y, shiftedCenter.y, 3.0);
    EXPECT_NEAR(output.r, 0.0, 0.3);
}

TEST(
    Algorithm_EdgeQualityTests,
    Locate_TopLengthExceedsUpperLimit_ReturnsNgAndSavesRedSideVisualization)
{
    EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    config.quality.topLength = {0.0, 100.0};
    const cv::Mat image = makeFactoryScene();
    const cv::Mat original = image.clone();
    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    const auto output = requireAlgorithmOutput(
        locateWithConfig(config, image, visualizationStore));

    expectNgWithZeroPose(output);
    const auto visualization = visualizationStore->snapshot();
    const auto measurements = evaluatePostprocess(visualization, config);
    ASSERT_TRUE(measurements.geometryAvailable);
    EXPECT_FALSE(measurements.topQualified);
    EXPECT_FALSE(measurements.edgeModeQualified);
    EXPECT_TRUE(measurements.offsetResultQualified);

    std::filesystem::path outputPath;
    ASSERT_TRUE(saveFactoryResult(
        original,
        visualization,
        config,
        output,
        "edge_postprocess_top_length_ng.png",
        outputPath));
    std::cout << "EdgePipeline top-length NG visualization: "
              << outputPath.string() << '\n';
}

TEST(
    Algorithm_EdgeQualityTests,
    Locate_DiagonalExceedsUpperLimit_ReturnsNgAndSavesRedDiagonalVisualization)
{
    EdgeAlgorithmConfig config = loadDefaultEdgeConfig();
    config.quality.diagonalTopLeftToBottomRight = {0.0, 100.0};
    const cv::Mat image = makeFactoryScene();
    const cv::Mat original = image.clone();
    const auto visualizationStore =
        std::make_shared<vision::algorithm::DemoEdgeVisualizationStore>();
    const auto output = requireAlgorithmOutput(
        locateWithConfig(config, image, visualizationStore));

    expectNgWithZeroPose(output);
    const auto visualization = visualizationStore->snapshot();
    const auto measurements = evaluatePostprocess(visualization, config);
    ASSERT_TRUE(measurements.geometryAvailable);
    EXPECT_FALSE(measurements.diagonalTopLeftToBottomRightQualified);
    EXPECT_FALSE(measurements.edgeModeQualified);
    EXPECT_TRUE(measurements.offsetResultQualified);

    std::filesystem::path outputPath;
    ASSERT_TRUE(saveFactoryResult(
        original,
        visualization,
        config,
        output,
        "edge_postprocess_diagonal_ng.png",
        outputPath));
    std::cout << "EdgePipeline diagonal NG visualization: "
              << outputPath.string() << '\n';
}

} // namespace
} // namespace vision::test::algorithm::edge_pipeline
