#include "AlgorithmConfig.h"
#include "mark/MarkGeometry.h"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace vision::test::algorithm::mark_pipeline {
namespace {

using vision::algorithm::detail::MarkQualityConfig;
using vision::algorithm::detail::MeasurementRangeConfig;

constexpr double kCenterX = 100.0;
constexpr double kCenterY = 200.0;
constexpr double kWidth = 400.0;
constexpr double kHeight = 200.0;
constexpr double kAngleDeg = 5.0;


enum class QualityTarget {
    Top,
    Right,
    Bottom,
    Left,
    DiagonalTopLeftToBottomRight,
    DiagonalTopRightToBottomLeft,
    CenterX,
    CenterY,
    AbsoluteAngle
};


enum class RangeFailure {
    BelowLower,
    AboveUpper
};


struct QualityFailureCase {
    QualityTarget target;
    RangeFailure failure;
    const char* name;
};


[[nodiscard]] MarkQualityConfig makeWideQuality()
{
    const MeasurementRangeConfig lengthRange{0.0, 1.0e9};
    const MeasurementRangeConfig coordinateRange{-1.0e9, 1.0e9};

    return MarkQualityConfig{
        .topLength = lengthRange,
        .rightLength = lengthRange,
        .bottomLength = lengthRange,
        .leftLength = lengthRange,
        .diagonalTopLeftToBottomRight = lengthRange,
        .diagonalTopRightToBottomLeft = lengthRange,
        .centerX = coordinateRange,
        .centerY = coordinateRange,
        .absoluteAngleDeg = {0.0, 90.0}
    };
}


[[nodiscard]] std::array<cv::Point2d, 4> makeMarks()
{
    const double radians = kAngleDeg * CV_PI / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const auto transform = [&](const cv::Point2d& local) {
        return cv::Point2d{
            kCenterX + local.x * cosine - local.y * sine,
            kCenterY + local.x * sine + local.y * cosine};
    };

    return {
        transform({-kWidth * 0.5, -kHeight * 0.5}),
        transform({kWidth * 0.5, -kHeight * 0.5}),
        transform({kWidth * 0.5, kHeight * 0.5}),
        transform({-kWidth * 0.5, kHeight * 0.5})
    };
}


[[nodiscard]] MeasurementRangeConfig& selectRange(
    MarkQualityConfig& quality,
    const QualityTarget target)
{
    switch (target) {
    case QualityTarget::Top:
        return quality.topLength;
    case QualityTarget::Right:
        return quality.rightLength;
    case QualityTarget::Bottom:
        return quality.bottomLength;
    case QualityTarget::Left:
        return quality.leftLength;
    case QualityTarget::DiagonalTopLeftToBottomRight:
        return quality.diagonalTopLeftToBottomRight;
    case QualityTarget::DiagonalTopRightToBottomLeft:
        return quality.diagonalTopRightToBottomLeft;
    case QualityTarget::CenterX:
        return quality.centerX;
    case QualityTarget::CenterY:
        return quality.centerY;
    case QualityTarget::AbsoluteAngle:
        return quality.absoluteAngleDeg;
    }
    throw std::logic_error("Unsupported Mark quality target.");
}


[[nodiscard]] double targetValue(const QualityTarget target)
{
    switch (target) {
    case QualityTarget::Top:
    case QualityTarget::Bottom:
        return kWidth;
    case QualityTarget::Right:
    case QualityTarget::Left:
        return kHeight;
    case QualityTarget::DiagonalTopLeftToBottomRight:
    case QualityTarget::DiagonalTopRightToBottomLeft:
        return std::hypot(kWidth, kHeight);
    case QualityTarget::CenterX:
        return kCenterX;
    case QualityTarget::CenterY:
        return kCenterY;
    case QualityTarget::AbsoluteAngle:
        return std::abs(kAngleDeg);
    }
    return 0.0;
}


[[nodiscard]] vision::core::AlgorithmOutput evaluate(
    const MarkQualityConfig& quality)
{
    const auto marks = makeMarks();
    return vision::algorithm::detail::evaluateMarkPose(
        marks[0],
        marks[1],
        marks[2],
        marks[3],
        quality);
}


void expectZeroPoseNg(const vision::core::AlgorithmOutput& output)
{
    EXPECT_FALSE(output.valid);
    EXPECT_DOUBLE_EQ(output.x, 0.0);
    EXPECT_DOUBLE_EQ(output.y, 0.0);
    EXPECT_DOUBLE_EQ(output.r, 0.0);
}


[[nodiscard]] std::filesystem::path findWorkspaceRoot()
{
    const std::array<std::filesystem::path, 2> startingPoints{
        std::filesystem::current_path(),
        std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path()};

    for (auto current : startingPoints) {
        while (!current.empty()) {
            if (std::filesystem::exists(current / "QtVisionApp.slnx")) {
                return current;
            }
            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
    }
    return {};
}

} // namespace


TEST(
    Algorithm_MarkQualityConfigTests,
    LoadConfig_CurrentExample_LoadsWideFirstStageQualityRanges)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());

    const auto config = vision::algorithm::detail::loadMarkAlgorithmConfig(
        workspaceRoot / "VisionAlgorithm" / "config" /
        "parameters" / "mark_point.example.yaml");

    const std::array<MeasurementRangeConfig, 6> lengthRanges{
        config.quality.topLength,
        config.quality.rightLength,
        config.quality.bottomLength,
        config.quality.leftLength,
        config.quality.diagonalTopLeftToBottomRight,
        config.quality.diagonalTopRightToBottomLeft};

    for (const auto& range : lengthRanges) {
        EXPECT_DOUBLE_EQ(range.lower, 0.0);
        EXPECT_DOUBLE_EQ(range.upper, 1.0e9);
    }
    EXPECT_DOUBLE_EQ(config.quality.centerX.lower, -1.0e9);
    EXPECT_DOUBLE_EQ(config.quality.centerX.upper, 1.0e9);
    EXPECT_DOUBLE_EQ(config.quality.centerY.lower, -1.0e9);
    EXPECT_DOUBLE_EQ(config.quality.centerY.upper, 1.0e9);
    EXPECT_DOUBLE_EQ(config.quality.absoluteAngleDeg.lower, 0.0);
    EXPECT_DOUBLE_EQ(config.quality.absoluteAngleDeg.upper, 90.0);
}


class Algorithm_MarkQualityRangeTests :
    public ::testing::TestWithParam<QualityFailureCase> {};


TEST_P(
    Algorithm_MarkQualityRangeTests,
    EvaluateMarkPose_OnlySelectedMeasurementIsOutsideRange_ReturnsZeroPoseNg)
{
    MarkQualityConfig quality = makeWideQuality();
    const QualityFailureCase testCase = GetParam();
    const double value = targetValue(testCase.target);
    MeasurementRangeConfig& selected = selectRange(quality, testCase.target);
    selected = testCase.failure == RangeFailure::BelowLower
        ? MeasurementRangeConfig{value + 1.0, value + 2.0}
        : MeasurementRangeConfig{value - 2.0, value - 1.0};

    expectZeroPoseNg(evaluate(quality));
}


INSTANTIATE_TEST_CASE_P(
    EveryGeometryAndPoseRange,
    Algorithm_MarkQualityRangeTests,
    ::testing::Values(
        QualityFailureCase{QualityTarget::Top, RangeFailure::BelowLower, "TopBelowLower"},
        QualityFailureCase{QualityTarget::Top, RangeFailure::AboveUpper, "TopAboveUpper"},
        QualityFailureCase{QualityTarget::Right, RangeFailure::BelowLower, "RightBelowLower"},
        QualityFailureCase{QualityTarget::Right, RangeFailure::AboveUpper, "RightAboveUpper"},
        QualityFailureCase{QualityTarget::Bottom, RangeFailure::BelowLower, "BottomBelowLower"},
        QualityFailureCase{QualityTarget::Bottom, RangeFailure::AboveUpper, "BottomAboveUpper"},
        QualityFailureCase{QualityTarget::Left, RangeFailure::BelowLower, "LeftBelowLower"},
        QualityFailureCase{QualityTarget::Left, RangeFailure::AboveUpper, "LeftAboveUpper"},
        QualityFailureCase{QualityTarget::DiagonalTopLeftToBottomRight, RangeFailure::BelowLower, "DiagonalTlBrBelowLower"},
        QualityFailureCase{QualityTarget::DiagonalTopLeftToBottomRight, RangeFailure::AboveUpper, "DiagonalTlBrAboveUpper"},
        QualityFailureCase{QualityTarget::DiagonalTopRightToBottomLeft, RangeFailure::BelowLower, "DiagonalTrBlBelowLower"},
        QualityFailureCase{QualityTarget::DiagonalTopRightToBottomLeft, RangeFailure::AboveUpper, "DiagonalTrBlAboveUpper"},
        QualityFailureCase{QualityTarget::CenterX, RangeFailure::BelowLower, "CenterXBelowLower"},
        QualityFailureCase{QualityTarget::CenterX, RangeFailure::AboveUpper, "CenterXAboveUpper"},
        QualityFailureCase{QualityTarget::CenterY, RangeFailure::BelowLower, "CenterYBelowLower"},
        QualityFailureCase{QualityTarget::CenterY, RangeFailure::AboveUpper, "CenterYAboveUpper"},
        QualityFailureCase{QualityTarget::AbsoluteAngle, RangeFailure::BelowLower, "AngleBelowLower"},
        QualityFailureCase{QualityTarget::AbsoluteAngle, RangeFailure::AboveUpper, "AngleAboveUpper"}),
    [](const ::testing::TestParamInfo<QualityFailureCase>& info) {
        return std::string(info.param.name);
    });


TEST(
    Algorithm_MarkQualityTests,
    EvaluateMarkPose_AllMeasurementsEqualInclusiveBoundaries_ReturnsValidPose)
{
    MarkQualityConfig quality = makeWideQuality();
    const double diagonal = std::hypot(kWidth, kHeight);

    quality.topLength = {kWidth, kWidth};
    quality.rightLength = {kHeight, kHeight};
    quality.bottomLength = {kWidth, kWidth};
    quality.leftLength = {kHeight, kHeight};
    quality.diagonalTopLeftToBottomRight = {diagonal, diagonal};
    quality.diagonalTopRightToBottomLeft = {diagonal, diagonal};
    quality.centerX = {kCenterX, kCenterX};
    quality.centerY = {kCenterY, kCenterY};
    quality.absoluteAngleDeg = {0.0, 0.0};

    const auto output = vision::algorithm::detail::evaluateMarkPose(
        cv::Point2d(-100.0, 100.0),
        cv::Point2d(300.0, 100.0),
        cv::Point2d(300.0, 300.0),
        cv::Point2d(-100.0, 300.0),
        quality);

    ASSERT_TRUE(output.valid);
    EXPECT_NEAR(output.x, kCenterX, 1.0e-12);
    EXPECT_NEAR(output.y, kCenterY, 1.0e-12);
    EXPECT_DOUBLE_EQ(output.r, 0.0);
}

} // namespace vision::test::algorithm::mark_pipeline
