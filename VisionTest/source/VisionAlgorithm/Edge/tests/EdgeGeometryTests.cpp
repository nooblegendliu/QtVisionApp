#include "edge/EdgeGeometry.h"

#include <gtest/gtest.h>

#include <opencv2/core.hpp>

#include <array>
#include <cmath>

namespace vision::test::algorithm::edge_pipeline {
namespace {

[[nodiscard]] cv::Vec4f lineThrough(
    const cv::Point2d& first,
    const cv::Point2d& second)
{
    cv::Point2d direction = second - first;
    const double norm = std::hypot(direction.x, direction.y);
    direction *= 1.0 / norm;
    return {
        static_cast<float>(direction.x),
        static_cast<float>(direction.y),
        static_cast<float>(first.x),
        static_cast<float>(first.y)};
}

[[nodiscard]] vision::algorithm::detail::DetectedEdgeLines
makeDetectedQuadrilateral(
    const std::array<cv::Point2d, 4>& corners)
{
    vision::algorithm::detail::DetectedEdgeLines lines;
    lines.top.line = lineThrough(corners[0], corners[1]);
    lines.right.line = lineThrough(corners[1], corners[2]);
    lines.bottom.line = lineThrough(corners[3], corners[2]);
    lines.left.line = lineThrough(corners[0], corners[3]);
    return lines;
}

[[nodiscard]] std::array<cv::Point2d, 4> makeRectangleCorners(
    const cv::Point2d& center,
    const double width,
    const double height,
    const double angleDeg)
{
    const double radians = angleDeg * CV_PI / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const auto rotate = [&](const cv::Point2d& local) {
        return cv::Point2d{
            center.x + local.x * cosine - local.y * sine,
            center.y + local.x * sine + local.y * cosine};
    };
    const double halfWidth = width * 0.5;
    const double halfHeight = height * 0.5;
    return {
        rotate({-halfWidth, -halfHeight}),
        rotate({halfWidth, -halfHeight}),
        rotate({halfWidth, halfHeight}),
        rotate({-halfWidth, halfHeight})};
}

void expectPointNear(
    const cv::Point2d& actual,
    const cv::Point2d& expected,
    const double tolerance)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
}

TEST(
    Algorithm_EdgeGeometryTests,
    Solve_AxisAlignedRectangle_ReturnsCornersMidlineCenterAndZeroAngle)
{
    const std::array<cv::Point2d, 4> expectedCorners{
        cv::Point2d(100.0, 200.0),
        cv::Point2d(500.0, 200.0),
        cv::Point2d(500.0, 600.0),
        cv::Point2d(100.0, 600.0)};
    const auto lines = makeDetectedQuadrilateral(expectedCorners);

    const auto result = vision::algorithm::detail::EdgeGeometry{}.solve(lines);

    ASSERT_TRUE(result.has_value());
    expectPointNear(result->center, cv::Point2d(300.0, 400.0), 1e-5);
    EXPECT_NEAR(result->angleDeg, 0.0, 1e-5);
    for (std::size_t index = 0; index < expectedCorners.size(); ++index) {
        expectPointNear(result->corners[index], expectedCorners[index], 1e-5);
    }
}

class Algorithm_EdgeGeometryRotatedRectangleTests :
    public ::testing::TestWithParam<double> {};

TEST_P(
    Algorithm_EdgeGeometryRotatedRectangleTests,
    Solve_PositiveOrNegativeRotation_ReturnsMidlineCenterAndSignedAngle)
{
    const double expectedAngleDeg = GetParam();
    const cv::Point2d expectedCenter(820.0, 640.0);
    const auto expectedCorners = makeRectangleCorners(
        expectedCenter,
        420.0,
        280.0,
        expectedAngleDeg);

    const auto result = vision::algorithm::detail::EdgeGeometry{}.solve(
        makeDetectedQuadrilateral(expectedCorners));

    ASSERT_TRUE(result.has_value());
    expectPointNear(result->center, expectedCenter, 1e-4);
    EXPECT_NEAR(result->angleDeg, expectedAngleDeg, 1e-4);
}

INSTANTIATE_TEST_CASE_P(
    SignedAngles,
    Algorithm_EdgeGeometryRotatedRectangleTests,
    ::testing::Values(3.7, -3.7));

TEST(
    Algorithm_EdgeGeometryTests,
    Solve_AsymmetricQuadrilateral_UsesOppositeSideMidlinesInsteadOfTopEdge)
{
    const std::array<cv::Point2d, 4> corners{
        cv::Point2d(100.0, 100.0),
        cv::Point2d(500.0, 120.0),
        cv::Point2d(460.0, 420.0),
        cv::Point2d(120.0, 400.0)};
    const double expectedMidlineAngleDeg =
        std::atan2(20.0, 370.0) * 180.0 / CV_PI;
    const double topEdgeAngleDeg =
        std::atan2(20.0, 400.0) * 180.0 / CV_PI;

    const auto result = vision::algorithm::detail::EdgeGeometry{}.solve(
        makeDetectedQuadrilateral(corners));

    ASSERT_TRUE(result.has_value());
    expectPointNear(result->center, cv::Point2d(295.0, 260.0), 1e-5);
    EXPECT_NEAR(result->angleDeg, expectedMidlineAngleDeg, 1e-5);
    EXPECT_GT(std::abs(result->angleDeg - topEdgeAngleDeg), 0.2);
}

TEST(
    Algorithm_EdgeGeometryTests,
    Solve_AdjacentLinesAreParallel_ReturnsNoGeometry)
{
    vision::algorithm::detail::DetectedEdgeLines lines;
    lines.top.line = cv::Vec4f(1.0F, 0.0F, 0.0F, 100.0F);
    lines.right.line = cv::Vec4f(0.0F, 1.0F, 500.0F, 0.0F);
    lines.bottom.line = cv::Vec4f(1.0F, 0.0F, 0.0F, 600.0F);
    lines.left.line = cv::Vec4f(1.0F, 0.0F, 0.0F, 200.0F);

    const auto result = vision::algorithm::detail::EdgeGeometry{}.solve(lines);

    EXPECT_FALSE(result.has_value());
}

} // namespace
} // namespace vision::test::algorithm::edge_pipeline
