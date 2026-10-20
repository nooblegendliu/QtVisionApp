#include "MarkGeometry.h"

#include "AlgorithmInternal.h"

#include <cmath>

namespace vision::algorithm::detail {

    namespace {

        constexpr double kMinimumGeometryLength = 1.0e-12;


        [[nodiscard]] bool isFinitePoint(
            const cv::Point2d& point) noexcept
        {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }


        [[nodiscard]] double pointDistance(
            const cv::Point2d& first,
            const cv::Point2d& second) noexcept
        {
            return std::hypot(
                second.x - first.x,
                second.y - first.y);
        }


        [[nodiscard]] bool isInRange(
            const double value,
            const MeasurementRangeConfig& range) noexcept
        {
            return std::isfinite(value) &&
                value >= range.lower &&
                value <= range.upper;
        }


        [[nodiscard]] double normalizeLineAngleDeg(
            double angleDeg) noexcept
        {
            while (angleDeg >= 90.0) {
                angleDeg -= 180.0;
            }
            while (angleDeg < -90.0) {
                angleDeg += 180.0;
            }
            return angleDeg;
        }

    } // namespace


    vision::core::AlgorithmOutput evaluateMarkPose(
        const cv::Point2d& topLeft,
        const cv::Point2d& topRight,
        const cv::Point2d& bottomRight,
        const cv::Point2d& bottomLeft,
        const MarkQualityConfig& quality) noexcept
    {
        if (!isFinitePoint(topLeft) ||
            !isFinitePoint(topRight) ||
            !isFinitePoint(bottomRight) ||
            !isFinitePoint(bottomLeft)) {
            return makeNoTargetOutput();
        }

        const cv::Point2d leftMidpoint =
            (topLeft + bottomLeft) * 0.5;
        const cv::Point2d rightMidpoint =
            (topRight + bottomRight) * 0.5;
        const cv::Point2d horizontalDirection =
            rightMidpoint - leftMidpoint;

        const double horizontalDirectionLength = std::hypot(
            horizontalDirection.x,
            horizontalDirection.y);
        if (!std::isfinite(horizontalDirectionLength) ||
            horizontalDirectionLength <= kMinimumGeometryLength) {
            return makeNoTargetOutput();
        }

        const cv::Point2d center =
            (leftMidpoint + rightMidpoint) * 0.5;
        double angleDeg = std::atan2(
            horizontalDirection.y,
            horizontalDirection.x) * 180.0 / CV_PI;
        angleDeg = normalizeLineAngleDeg(angleDeg);

        if (!isFinitePoint(center) || !std::isfinite(angleDeg)) {
            return makeNoTargetOutput();
        }

        const double topLength = pointDistance(topLeft, topRight);
        const double rightLength = pointDistance(topRight, bottomRight);
        const double bottomLength = pointDistance(bottomLeft, bottomRight);
        const double leftLength = pointDistance(topLeft, bottomLeft);
        const double diagonalTopLeftToBottomRight =
            pointDistance(topLeft, bottomRight);
        const double diagonalTopRightToBottomLeft =
            pointDistance(topRight, bottomLeft);

        const bool geometryIsNonDegenerate =
            topLength > kMinimumGeometryLength &&
            rightLength > kMinimumGeometryLength &&
            bottomLength > kMinimumGeometryLength &&
            leftLength > kMinimumGeometryLength &&
            diagonalTopLeftToBottomRight > kMinimumGeometryLength &&
            diagonalTopRightToBottomLeft > kMinimumGeometryLength;

        const bool geometryQualified =
            geometryIsNonDegenerate &&
            isInRange(topLength, quality.topLength) &&
            isInRange(rightLength, quality.rightLength) &&
            isInRange(bottomLength, quality.bottomLength) &&
            isInRange(leftLength, quality.leftLength) &&
            isInRange(
                diagonalTopLeftToBottomRight,
                quality.diagonalTopLeftToBottomRight) &&
            isInRange(
                diagonalTopRightToBottomLeft,
                quality.diagonalTopRightToBottomLeft);

        const bool poseQualified =
            isInRange(center.x, quality.centerX) &&
            isInRange(center.y, quality.centerY) &&
            isInRange(std::abs(angleDeg), quality.absoluteAngleDeg);

        if (!geometryQualified || !poseQualified) {
            return makeNoTargetOutput();
        }

        return vision::core::AlgorithmOutput{
            .x = center.x,
            .y = center.y,
            .r = angleDeg,
            .valid = true
        };
    }

} // namespace vision::algorithm::detail
