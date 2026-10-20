#pragma once

#include <opencv2/core.hpp>
#include <optional>
#include <vector>

#include "EdgeTypes.h"

namespace vision::algorithm::detail {

    /*
        @brief 电池片单边检测器

        每条电池片边缘配置两个固定ROI。

        检测流程：
        1. 在每个ROI中均匀布置多条测量线；
        2. 每条测量线提取一个边缘点；
        3. 检查两个ROI的有效点数量；
        4. 合并两个ROI的边缘点；
        5. 使用cv::fitLine拟合边缘直线。
    */
    class EdgeLineDetector final {
    public:
        EdgeLineDetector() = default;

        /*
            @brief 检测一条电池片边缘

            @param rectifiedGray
                畸变校正后的灰度图，要求类型为CV_8UC1。

            @param roiPair
                同一条边上的两个固定ROI。

            @param caliperConfig
                卡尺公共参数。

            @return
                成功时返回拟合直线及所有有效边缘点；
                检测失败时返回std::nullopt。
        */
        [[nodiscard]]
        std::optional<DetectedEdgeLine> detect(
            const cv::Mat& rectifiedGray,
            const RoiPairConfig& roiPair,
            const CaliperConfig& caliperConfig) const;

    private:
        /*
            @brief 从一个固定ROI中提取边缘点

            ROI中每条测量线最多返回一个边缘点。
        */
        [[nodiscard]]
        std::vector<cv::Point2f> collectEdgePoints(
            const cv::Mat& rectifiedGray,
            const CaliperRoiConfig& roiConfig,
            const CaliperConfig& caliperConfig) const;

        /*
            @brief 从一条测量线上提取一个边缘点

            测量线以lineCenter为中心，沿scanDirection搜索。
        */
        [[nodiscard]]
        std::optional<cv::Point2f> measureEdgePoint(
            const cv::Mat& rectifiedGray,
            const cv::Point2f& lineCenter,
            const cv::Point2f& scanDirection,
            double searchLengthPx,
            EdgePolarity polarity,
            const CaliperConfig& caliperConfig) const;
    };







}	// namespace vision::algorithm::detail