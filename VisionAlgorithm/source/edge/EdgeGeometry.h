#pragma once

#include <opencv2/core.hpp>

#include <array>
#include <optional>

#include "EdgeTypes.h"

namespace vision::algorithm::detail {

    class EdgeGeometry final {
    public:
        struct Result {
            cv::Point2d center;
            double angleDeg{0.0};
            std::array<cv::Point2d, 4> corners;
        };

        [[nodiscard]]
        std::optional<Result> solve(
            const DetectedEdgeLines& lines) const;

    private:
        [[nodiscard]]
        static std::optional<cv::Point2d> intersect(
            const cv::Vec4f& first,
            const cv::Vec4f& second);
    };

} // namespace vision::algorithm::detail
