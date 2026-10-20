#include "mark/MarkGeometry.h"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>

#include <array>
#include <cmath>

namespace vision::test::algorithm::mark_pipeline {
namespace {

using vision::algorithm::detail::MarkQualityConfig;
using vision::algorithm::detail::MeasurementRangeConfig;


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


[[nodiscard]] std::array<cv::Point2d, 4> makeRectangleMarks(
    const cv::Point2d& center,
    const double width,
    const double height,
    const double angleDeg)
{
    const double radians = angleDeg * CV_PI / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const auto transform = [&](const cv::Point2d& local) {
        return cv::Point2d{
            center.x + local.x * cosine - local.y * sine,
            center.y + local.x * sine + local.y * cosine};
    };

    const double halfWidth = width * 0.5;
    const double halfHeight = height * 0.5;
    return {
        transform({-halfWidth, -halfHeight}),
        transform({halfWidth, -halfHeight}),
        transform({halfWidth, halfHeight}),
        transform({-halfWidth, halfHeight})
    };
}


void expectZeroPoseNg(const vision::core::AlgorithmOutput& output)
{
    EXPECT_FALSE(output.valid);
    EXPECT_DOUBLE_EQ(output.x, 0.0);
    EXPECT_DOUBLE_EQ(output.y, 0.0);
    EXPECT_DOUBLE_EQ(output.r, 0.0);
}

} // namespace


class Algorithm_MarkGeometryRotatedRectangleTests :
    public ::testing::TestWithParam<double> {};


TEST_P(
    Algorithm_MarkGeometryRotatedRectangleTests,
    EvaluateMarkPose_RotatedRectangle_ReturnsMidlineCenterAndSignedAngle)
{
    const double expectedAngleDeg = GetParam();
    const cv::Point2d expectedCenter(820.0, -640.0);
    const auto marks = makeRectangleMarks(
        expectedCenter,
        420.0,
        280.0,
        expectedAngleDeg);

    const auto output = vision::algorithm::detail::evaluateMarkPose(
        marks[0],
        marks[1],
        marks[2],
        marks[3],
        makeWideQuality());

    ASSERT_TRUE(output.valid);
    EXPECT_NEAR(output.x, expectedCenter.x, 1.0e-9);
    EXPECT_NEAR(output.y, expectedCenter.y, 1.0e-9);
    EXPECT_NEAR(output.r, expectedAngleDeg, 1.0e-9);
}


INSTANTIATE_TEST_CASE_P(
    SignedAngles,
    Algorithm_MarkGeometryRotatedRectangleTests,
    ::testing::Values(3.7, -3.7));


TEST(
    Algorithm_MarkGeometryTests,
    EvaluateMarkPose_AsymmetricMarks_UsesOppositeSideMidlineInsteadOfTopEdge)
{
    const cv::Point2d topLeft(100.0, 100.0);
    const cv::Point2d topRight(500.0, 120.0);
    const cv::Point2d bottomRight(460.0, 420.0);
    const cv::Point2d bottomLeft(120.0, 400.0);
    const double expectedMidlineAngleDeg =
        std::atan2(20.0, 370.0) * 180.0 / CV_PI;
    const double topEdgeAngleDeg =
        std::atan2(20.0, 400.0) * 180.0 / CV_PI;

    const auto output = vision::algorithm::detail::evaluateMarkPose(
        topLeft,
        topRight,
        bottomRight,
        bottomLeft,
        makeWideQuality());

    ASSERT_TRUE(output.valid);
    EXPECT_NEAR(output.x, 295.0, 1.0e-12);
    EXPECT_NEAR(output.y, 260.0, 1.0e-12);
    EXPECT_NEAR(output.r, expectedMidlineAngleDeg, 1.0e-12);
    EXPECT_GT(std::abs(output.r - topEdgeAngleDeg), 0.2);
}


TEST(
    Algorithm_MarkGeometryTests,
    EvaluateMarkPose_DegenerateHorizontalMidline_ReturnsZeroPoseNg)
{
    const auto output = vision::algorithm::detail::evaluateMarkPose(
        cv::Point2d(0.0, 0.0),
        cv::Point2d(10.0, 0.0),
        cv::Point2d(0.0, 10.0),
        cv::Point2d(10.0, 10.0),
        makeWideQuality());

    expectZeroPoseNg(output);
}

} // namespace vision::test::algorithm::mark_pipeline
