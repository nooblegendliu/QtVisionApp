#include "EdgeGeometry.h"

#include <cmath>
#include <limits>

namespace vision::algorithm::detail {

    namespace {

        constexpr double kDirectionNormEpsilon = 1e-12;
        constexpr double kParallelEpsilon = 1e-10;


        /*
            @brief 判断cv::fitLine输出是否包含有效数值
        */
        [[nodiscard]]
        bool isFiniteLine(
            const cv::Vec4f& line) noexcept
        {
            for (int index = 0;
                index < cv::Vec4f::channels;
                ++index) {

                if (!std::isfinite(line[index])) {
                    return false;
                }
            }

            return true;
        }


        /*
            @brief 判断二维点是否为有限值
        */
        [[nodiscard]]
        bool isFinitePoint(
            const cv::Point2d& point) noexcept
        {
            return std::isfinite(point.x) &&
                std::isfinite(point.y);
        }


        /*
            @brief 计算二维向量叉积

            cross(first, second)
                = first.x * second.y
                - first.y * second.x
        */
        [[nodiscard]]
        double crossProduct(
            const cv::Point2d& first,
            const cv::Point2d& second) noexcept
        {
            return first.x * second.y -
                first.y * second.x;
        }


        /*
            @brief 计算二维向量长度
        */
        [[nodiscard]]
        double vectorNorm(
            const cv::Point2d& vector) noexcept
        {
            return std::hypot(
                vector.x,
                vector.y);
        }


        [[nodiscard]]
        std::optional<cv::Point2d> intersectPointDirections(
            const cv::Point2d& firstPoint,
            const cv::Point2d& firstDirection,
            const cv::Point2d& secondPoint,
            const cv::Point2d& secondDirection)
        {
            if (!isFinitePoint(firstPoint) ||
                !isFinitePoint(firstDirection) ||
                !isFinitePoint(secondPoint) ||
                !isFinitePoint(secondDirection)) {
                return std::nullopt;
            }

            const double firstDirectionNorm =
                vectorNorm(firstDirection);
            const double secondDirectionNorm =
                vectorNorm(secondDirection);

            if (firstDirectionNorm <= kDirectionNormEpsilon ||
                secondDirectionNorm <= kDirectionNormEpsilon) {
                return std::nullopt;
            }

            const double determinant =
                crossProduct(firstDirection, secondDirection);
            const double parallelThreshold =
                kParallelEpsilon *
                firstDirectionNorm *
                secondDirectionNorm;

            if (std::abs(determinant) <= parallelThreshold) {
                return std::nullopt;
            }

            const cv::Point2d pointDifference =
                secondPoint - firstPoint;
            const double firstParameter =
                crossProduct(pointDifference, secondDirection) /
                determinant;

            if (!std::isfinite(firstParameter)) {
                return std::nullopt;
            }

            const cv::Point2d intersection =
                firstPoint + firstDirection * firstParameter;
            if (!isFinitePoint(intersection)) {
                return std::nullopt;
            }

            return intersection;
        }


        /*
            @brief 将角度归一化到[-90°, 90°)

            直线方向具有180°周期：

                7°和187°表示同一条直线；
                97°和-83°表示同一条直线。

            对电池片姿态而言，使用[-90°, 90°)可以避免
            cv::fitLine方向翻转或角度跨越180°造成结果跳变。
        */
        [[nodiscard]]
        double normalizeLineAngleDeg(
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


    std::optional<EdgeGeometry::Result>
        EdgeGeometry::solve(
            const DetectedEdgeLines& lines) const
    {
        /*
            首先检查cv::fitLine输出。

            正常的边缘检测失败应该由EdgeLineDetector返回nullopt；
            这里的检查用于防止NaN、无穷大或无效方向继续传播。
        */
        if (!isFiniteLine(lines.top.line) ||
            !isFiniteLine(lines.right.line) ||
            !isFiniteLine(lines.bottom.line) ||
            !isFiniteLine(lines.left.line)) {

            return std::nullopt;
        }

        /*
            四个角点固定按照以下方式求交：

                左上角 = Top    ∩ Left
                右上角 = Top    ∩ Right
                右下角 = Bottom ∩ Right
                左下角 = Bottom ∩ Left

            角点仅在当前函数内部使用，不保存到共享数据结构中。
        */
        const std::optional<cv::Point2d> topLeft =
            intersect(
                lines.top.line,
                lines.left.line);

        const std::optional<cv::Point2d> topRight =
            intersect(
                lines.top.line,
                lines.right.line);

        const std::optional<cv::Point2d> bottomRight =
            intersect(
                lines.bottom.line,
                lines.right.line);

        const std::optional<cv::Point2d> bottomLeft =
            intersect(
                lines.bottom.line,
                lines.left.line);

        if (!topLeft.has_value() ||
            !topRight.has_value() ||
            !bottomRight.has_value() ||
            !bottomLeft.has_value()) {

            return std::nullopt;
        }

        const cv::Point2d topMidpoint =
            (*topLeft + *topRight) * 0.5;
        const cv::Point2d rightMidpoint =
            (*topRight + *bottomRight) * 0.5;
        const cv::Point2d bottomMidpoint =
            (*bottomLeft + *bottomRight) * 0.5;
        const cv::Point2d leftMidpoint =
            (*topLeft + *bottomLeft) * 0.5;

        const cv::Point2d horizontalDirection =
            rightMidpoint - leftMidpoint;
        const cv::Point2d verticalDirection =
            bottomMidpoint - topMidpoint;

        const std::optional<cv::Point2d> center =
            intersectPointDirections(
                leftMidpoint,
                horizontalDirection,
                topMidpoint,
                verticalDirection);
        if (!center.has_value() ||
            vectorNorm(horizontalDirection) <= kDirectionNormEpsilon) {
            return std::nullopt;
        }

        double angleDeg =
            std::atan2(
                horizontalDirection.y,
                horizontalDirection.x)
            * 180.0 /
            CV_PI;

        angleDeg =
            normalizeLineAngleDeg(angleDeg);

        if (!std::isfinite(angleDeg)) {
            return std::nullopt;
        }

        return Result{
            *center,
            angleDeg,
            {*topLeft, *topRight, *bottomRight, *bottomLeft}
        };
    }


    std::optional<cv::Point2d>
        EdgeGeometry::intersect(
            const cv::Vec4f& first,
            const cv::Vec4f& second)
    {
        if (!isFiniteLine(first) ||
            !isFiniteLine(second)) {

            return std::nullopt;
        }

        /*
            第一条直线：
                P + tR

            第二条直线：
                Q + uS
        */
        const cv::Point2d firstDirection{
            static_cast<double>(first[0]),
            static_cast<double>(first[1])
        };

        const cv::Point2d secondDirection{
            static_cast<double>(second[0]),
            static_cast<double>(second[1])
        };

        const cv::Point2d firstPoint{
            static_cast<double>(first[2]),
            static_cast<double>(first[3])
        };

        const cv::Point2d secondPoint{
            static_cast<double>(second[2]),
            static_cast<double>(second[3])
        };

        return intersectPointDirections(
            firstPoint,
            firstDirection,
            secondPoint,
            secondDirection);
    }

} // namespace vision::algorithm::detail
