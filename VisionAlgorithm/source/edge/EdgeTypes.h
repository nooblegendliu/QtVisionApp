#pragma once

/*
	@brief: 集中保存抓边算法专用的数据结构，避免散落在多个文件中
*/
#include <opencv2/core.hpp>
#include <vector>

#include "QualityTypes.h"

namespace vision::algorithm::detail {

    /* @brief: 边缘极性 */
    enum class EdgePolarity {
        DarkToLight,
        LightToDark,
        Any
    };

    /*
        @brief: 四边
    */
    enum class EdgeSide : std::size_t {
        Top = 0,
        Right,
        Bottom,
        Left
    };
       


    /*
        @brief: 一个固定卡尺ROI

        - 所有坐标基于畸变校正后的图像
    */
    struct CaliperRoiConfig {
        // 整个卡尺ROI在图像中的中心坐标
        cv::Point2f centerPx;

        // scanAngleDeg:
        // - 0°表示沿图像 +X方向搜索
        // - 90°表示沿图像 +Y方向搜索
        double scanAngleDeg = 0.0;
        
        // 沿搜索方向的长度
        double searchLengthPx = 0.0;

        // 垂直搜索方向的覆盖长度
        double spanLengthPx = 0.0;

        // 在span范围内布置的测量线数量
        int sampleCount = 0;

        EdgePolarity polarity{ EdgePolarity::Any };
    };


    /*
        @brief: 同一条边的两个固定ROI
    */
    struct RoiPairConfig {
        CaliperRoiConfig first;
        CaliperRoiConfig second;
    };

    struct CaliperConfig {
        // 一维灰度曲线平滑核尺寸，必须为正奇数
        int smoothingKernelSize = 5;

        // 最小梯度响应
        double minimumGradient = 10.0;

        // 是否进行亚像素峰值计算
        bool enableSubpixel = true;

        // 每个ROI至少需要成功提取的边缘点数量
        int minimumPointsPerRoi = 4;
    };

    struct EdgeQualityConfig {
        MeasurementRangeConfig topLength;
        MeasurementRangeConfig rightLength;
        MeasurementRangeConfig bottomLength;
        MeasurementRangeConfig leftLength;

        MeasurementRangeConfig diagonalTopLeftToBottomRight;
        MeasurementRangeConfig diagonalTopRightToBottomLeft;

        MeasurementRangeConfig absoluteAngleDeg;
        double maximumCenterOffset{ 0.0 };
    };

    /*
    * 抓边算法实际运行参数。
    *
    * 只保存算法执行需要的参数，不保存参数文件来源路径。
    */
    struct EdgeAlgorithmConfig {
        RoiPairConfig top;
        RoiPairConfig right;
        RoiPairConfig bottom;
        RoiPairConfig left;

        CaliperConfig caliper;
        EdgeQualityConfig quality;
    };


    /*  
        单条电池片边缘检测结果
    */
    struct DetectedEdgeLine {
        cv::Vec4f line;

        /*
        * 参与直线拟合的边缘点
        */
        std::vector<cv::Point2f> edgePoints;
    };

    struct DetectedEdgeLines {
        DetectedEdgeLine top;
        DetectedEdgeLine right;
        DetectedEdgeLine bottom;
        DetectedEdgeLine left;
    };

}   // namespace vision::algorithm::detail







