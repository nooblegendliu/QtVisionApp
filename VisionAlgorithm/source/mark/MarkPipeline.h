#pragma once

#include <VisionCore/LocateTypes.h>

#include "AlgorithmConfig.h"
#include "AlgorithmInternal.h"
#include "CameraGeometry.h"
#include "ShapeMatcher.h"

#include <optional>

namespace vision::algorithm::detail {
    // 校验 MarkPoint 必需的图像、ROI、模板尺寸和角色标定尺寸契约。
    void validateMarkPipelineResources(
        const MarkAlgorithmConfig& config,
        cv::Size topLeftCalibrationSize,
        cv::Size topRightCalibrationSize,
        cv::Size bottomRightCalibrationSize,
        cv::Size bottomLeftCalibrationSize);

    class MarkPipeline final {
    public:
        MarkPipeline(
            MarkAlgorithmConfig config,
            CameraGeometry topLeftGeometry,
            CameraGeometry topRightGeometry,
            CameraGeometry bottomRightGeometry,
            CameraGeometry bottomLeftGeometry);

        [[nodiscard]] vision::core::AlgorithmOutcome locate(
            const vision::core::MarkPointAlgorithmInput& input,
            const ExecutionContext& context) const;

    private:
        [[nodiscard]] std::optional<cv::Point2d> detectMark(
            const cv::Mat& image,
            const cv::Rect& roi,
            const CameraGeometry& geometry) const;

        MarkAlgorithmConfig config_;
        CameraGeometry topLeftGeometry_;
        CameraGeometry topRightGeometry_;
        CameraGeometry bottomRightGeometry_;
        CameraGeometry bottomLeftGeometry_;
        ShapeMatcher shapeMatcher_;
    };

}	// namespace vision::algorithm::detail
