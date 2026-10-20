#pragma once

#include <opencv2/core.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "QualityTypes.h"

namespace vision::algorithm::detail {

    /*
        line2Dup::Detector 构造参数。

        建模和在线匹配必须使用同一套参数。
    */
    struct DetectorConfig {
        // scale = 1.0时的基础特征点数量
        // 离线建模时会随着尺度进行缩放
        int numFeatures{ 128 };
        std::vector<int> pyramidT{ 4, 8 };
        float weakThreshold{ 30.0F };
        float strongThreshold{ 60.0F };
    };


    /*
        shapeInfo_producer 生成角度、尺度模板时使用的参数。

        生产流程固定使用这些代码值，不从 MarkPoint 参数文件读取。
        内部第三方兼容差分测试可以直接构造不同值。
    */
    struct TemplateBuildConfig {
        float angleMinDeg{ 0.0F };
        float angleMaxDeg{ 360.0F };
        float angleStepDeg{ 1.0F };

        float scaleMin{ 1.0F };
        float scaleMax{ 1.0F };
        float scaleStep{ 1.0F };

        int templatePadding{ 100 };
    };


    /*
        Detector::match 以及后续 ICP 所需的在线参数。

        生产流程固定使用这些代码值，不从 MarkPoint 参数文件读取。
        内部第三方兼容差分测试可以直接构造不同值。
    */
    struct TemplateMatchConfig {
        float similarityThreshold{ 90.0F };
        float nmsThreshold{ 0.3F };
        int maxResults{ 1 };
        int stride{ 16 };
        int sourcePadding{ 100 };
        bool enableIcpRefinement{ false };
    };


    /*
        ShapeMatcher 的完整配置。

        templateFilePattern 直接对应 Detector::readClasses/writeClasses 的
        format 参数，例如 "models/%s_templ.yaml"。

        templateInfoFile 直接对应 shapeInfo_producer::load_infos/save_infos
        使用的第二个模型文件。
    */
    struct ShapeMatcherConfig {
        std::string classId{ "mark" };
        std::filesystem::path templateFilePattern;
        std::filesystem::path templateInfoFile;

        /*
            原始、未补边模板图像尺寸。

            第一阶段固定使用该图像的中心作为匹配参考点。
        */
        cv::Size templateImageSizePx;

        DetectorConfig detector;
        TemplateBuildConfig build;
        TemplateMatchConfig match;
    };


    struct MarkQualityConfig {
        MeasurementRangeConfig topLength;
        MeasurementRangeConfig rightLength;
        MeasurementRangeConfig bottomLength;
        MeasurementRangeConfig leftLength;

        MeasurementRangeConfig diagonalTopLeftToBottomRight;
        MeasurementRangeConfig diagonalTopRightToBottomLeft;

        MeasurementRangeConfig centerX;
        MeasurementRangeConfig centerY;
        MeasurementRangeConfig absoluteAngleDeg;
    };


    struct MarkAlgorithmConfig {
        ShapeMatcherConfig shapeMatcher;

        cv::Size inputImageSizePx{ 4024, 3036 };

        // Camera-role ROIs. The role names do not describe positions in an image.
        cv::Rect topLeftCameraRoiPx;
        cv::Rect topRightCameraRoiPx;
        cv::Rect bottomRightCameraRoiPx;
        cv::Rect bottomLeftCameraRoiPx;

        MarkQualityConfig quality;
    };


    /*
        对库中一次 Match、对应 Info 以及 ICP RegistrationResult 的按值转换。

        matchPositionPx 是当前匹配模板一级 LINE2 特征包围盒中心在输入
        image 中的像素坐标，并且已经扣除在线匹配使用的 sourcePadding；
        ICP 开启时，该点已经应用 ICP 变换。

        icpTransformation、icpInlierRmse 和 icpFitness 分别对应库中的
        transformation_、inlier_rmse_ 和 fitness_。
    */
    struct MatchResult {
        std::string classId;
        int templateId{ 0 };
        float similarity{ 0.0F };

        cv::Point2f matchPositionPx;
        float angleDeg{ 0.0F };
        float scale{ 1.0F };

        cv::Matx33f icpTransformation{ cv::Matx33f::eye() };
        float icpInlierRmse{ 0.0F };
        float icpFitness{ 0.0F };
    };

}   // namespace vision::algorithm::detail
