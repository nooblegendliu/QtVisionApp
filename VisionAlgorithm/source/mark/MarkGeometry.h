#pragma once

#include <VisionCore/LocateTypes.h>

#include <opencv2/core.hpp>

#include "MarkTypes.h"

namespace vision::algorithm::detail {

    /*
        使用四个已经转换到同一物理坐标系的 Mark 中心形成最终位姿。

        计算得到的边长、对角线、中点和方向都只存在于本次调用中；
        返回值仍使用现有 AlgorithmOutput，不引入新的持久化结果类型。
    */
    [[nodiscard]] vision::core::AlgorithmOutput evaluateMarkPose(
        const cv::Point2d& topLeft,
        const cv::Point2d& topRight,
        const cv::Point2d& bottomRight,
        const cv::Point2d& bottomLeft,
        const MarkQualityConfig& quality) noexcept;

} // namespace vision::algorithm::detail
