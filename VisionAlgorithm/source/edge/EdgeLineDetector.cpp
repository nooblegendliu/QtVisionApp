#include "EdgeLineDetector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace vision::algorithm::detail {

    namespace {

        /*
            @brief 判断浮点坐标是否位于图像有效范围内
        */
        [[nodiscard]]
        bool isPointInsideImage(
            const cv::Point2f& point,
            const cv::Size imageSize)
        {
            if (imageSize.width <= 0 ||
                imageSize.height <= 0) {
                return false;
            }

            return point.x >= 0.0F &&
                point.y >= 0.0F &&
                point.x <=
                static_cast<float>(imageSize.width - 1) &&
                point.y <=
                static_cast<float>(imageSize.height - 1);
        }

        /*
            @brief 根据边缘极性将梯度转换为正向响应

            对期望的边缘方向，响应应为正数；
            响应越大，表示边缘越明显。
        */
        [[nodiscard]]
        float calculatePolarityResponse(
            const float gradient,
            const EdgePolarity polarity)
        {
            switch (polarity) {
            case EdgePolarity::DarkToLight:
                return gradient;

            case EdgePolarity::LightToDark:
                return -gradient;

            case EdgePolarity::Any:
                return std::abs(gradient);
            }

            return 0.0F;
        }



    }   // namespace

    std::optional<DetectedEdgeLine>
        EdgeLineDetector::detect(
            const cv::Mat& rectifiedGray,
            const RoiPairConfig& roiPair,
            const CaliperConfig& caliperConfig) const
    {
        /*
            输入图像类型属于EdgePipeline的接口约束。

            这里仍然进行防御性检查，避免错误输入进入OpenCV算法。
        */
        if (rectifiedGray.empty() ||
            rectifiedGray.type() != CV_8UC1) {
            return std::nullopt;
        }

        if (caliperConfig.minimumPointsPerRoi <= 0) {
            return std::nullopt;
        }

        std::vector<cv::Point2f> firstPoints =
            collectEdgePoints(
                rectifiedGray,
                roiPair.first,
                caliperConfig);

        std::vector<cv::Point2f> secondPoints =
            collectEdgePoints(
                rectifiedGray,
                roiPair.second,
                caliperConfig);

        const std::size_t minimumPoints =
            static_cast<std::size_t>(
                caliperConfig.minimumPointsPerRoi);

        /*
            两个ROI必须分别满足最少有效点数。

            不能只判断合并后的总点数，否则一个ROI完全失效时，
            可能仅依靠另一个局部ROI拟合直线，导致角度不稳定。
        */
        if (firstPoints.size() < minimumPoints ||
            secondPoints.size() < minimumPoints) {
            return std::nullopt;
        }

        std::vector<cv::Point2f> allPoints;

        allPoints.reserve(
            firstPoints.size() +
            secondPoints.size());

        allPoints.insert(
            allPoints.end(),
            firstPoints.begin(),
            firstPoints.end());

        allPoints.insert(
            allPoints.end(),
            secondPoints.begin(),
            secondPoints.end());

        /*
            cv::fitLine拟合二维直线至少需要两个点。
        */
        if (allPoints.size() < 2U) {
            return std::nullopt;
        }

        cv::Vec4f fittedLine{};

        /*
            输出格式：
                fittedLine[0] = vx
                fittedLine[1] = vy
                fittedLine[2] = x0
                fittedLine[3] = y0

            使用DIST_HUBER降低少量异常点对拟合结果的影响。
        */
        cv::fitLine(
            allPoints,
            fittedLine,
            cv::DIST_HUBER,
            0.0,
            0.01,
            0.01);

        /*
            防止异常数值继续传递到几何计算阶段。
        */
        for (int index = 0;
            index < cv::Vec4f::channels;
            ++index) {

            if (!std::isfinite(fittedLine[index])) {
                return std::nullopt;
            }
        }

        const double directionNorm =
            std::hypot(
                static_cast<double>(fittedLine[0]),
                static_cast<double>(fittedLine[1]));

        if (directionNorm <=
            std::numeric_limits<double>::epsilon()) {
            return std::nullopt;
        }

        DetectedEdgeLine result;

        result.line = fittedLine;
        result.edgePoints = std::move(allPoints);

        return result;
    }


    std::vector<cv::Point2f>
        EdgeLineDetector::collectEdgePoints(
            const cv::Mat& rectifiedGray,
            const CaliperRoiConfig& roiConfig,
            const CaliperConfig& caliperConfig) const
    {
        std::vector<cv::Point2f> edgePoints;

        if (rectifiedGray.empty() ||
            rectifiedGray.type() != CV_8UC1) {
            return edgePoints;
        }

        if (roiConfig.sampleCount <= 0 ||
            roiConfig.searchLengthPx <= 0.0 ||
            roiConfig.spanLengthPx < 0.0) {
            return edgePoints;
        }

        edgePoints.reserve(
            static_cast<std::size_t>(
                roiConfig.sampleCount));

        const double angleRad =
            roiConfig.scanAngleDeg *
            CV_PI /
            180.0;

        /*
            图像坐标系中：
                0°   -> +X
                90°  -> +Y
                180° -> -X
                -90° -> -Y
        */
        const cv::Point2f scanDirection{
            static_cast<float>(std::cos(angleRad)),
            static_cast<float>(std::sin(angleRad))
        };

        /*
            spanDirection与scanDirection垂直。

            多条测量线沿spanDirection均匀排布。
        */
        const cv::Point2f spanDirection{
            -scanDirection.y,
            scanDirection.x
        };

        for (int index = 0;
            index < roiConfig.sampleCount;
            ++index) {

            double spanOffsetPx = 0.0;

            if (roiConfig.sampleCount > 1) {
                const double ratio =
                    static_cast<double>(index) /
                    static_cast<double>(
                        roiConfig.sampleCount - 1);

                spanOffsetPx =
                    -roiConfig.spanLengthPx * 0.5 +
                    ratio * roiConfig.spanLengthPx;
            }

            const cv::Point2f lineCenter =
                roiConfig.centerPx +
                spanDirection *
                static_cast<float>(spanOffsetPx);

            const std::optional<cv::Point2f> edgePoint =
                measureEdgePoint(
                    rectifiedGray,
                    lineCenter,
                    scanDirection,
                    roiConfig.searchLengthPx,
                    roiConfig.polarity,
                    caliperConfig);

            if (edgePoint.has_value()) {
                edgePoints.push_back(*edgePoint);
            }
        }

        return edgePoints;
    }


    std::optional<cv::Point2f>
        EdgeLineDetector::measureEdgePoint(
            const cv::Mat& rectifiedGray,
            const cv::Point2f& lineCenter,
            const cv::Point2f& scanDirection,
            const double searchLengthPx,
            const EdgePolarity polarity,
            const CaliperConfig& caliperConfig) const
    {
        if (rectifiedGray.empty() ||
            rectifiedGray.type() != CV_8UC1) {
            return std::nullopt;
        }

        if (searchLengthPx <= 0.0 ||
            caliperConfig.smoothingKernelSize <= 0 ||
            caliperConfig.smoothingKernelSize % 2 == 0 ||
            caliperConfig.minimumGradient <= 0.0) {
            return std::nullopt;
        }

        const double directionNorm =
            std::hypot(
                static_cast<double>(scanDirection.x),
                static_cast<double>(scanDirection.y));

        if (directionNorm <=
            std::numeric_limits<double>::epsilon()) {
            return std::nullopt;
        }

        /*
            理论上collectEdgePoints传入的方向已经归一化。
            这里再次归一化，避免该私有函数以后被其他位置调用时出错。
        */
        const cv::Point2f normalizedDirection{
            static_cast<float>(
                static_cast<double>(scanDirection.x) /
                directionNorm),

            static_cast<float>(
                static_cast<double>(scanDirection.y) /
                directionNorm)
        };

        const float halfLength =
            static_cast<float>(
                searchLengthPx * 0.5);

        const cv::Point2f lineStart =
            lineCenter -
            normalizedDirection * halfLength;

        const cv::Point2f lineEnd =
            lineCenter +
            normalizedDirection * halfLength;

        /*
            图像区域是凸集。

            当测量线两个端点都位于图像中时，
            整条线段也位于图像中。
        */
        if (!isPointInsideImage(
            lineStart,
            rectifiedGray.size()) ||
            !isPointInsideImage(
                lineEnd,
                rectifiedGray.size())) {
            return std::nullopt;
        }

        /*
            使用不超过约1像素的采样间隔。

            ceil(searchLengthPx) + 1能够保证相邻采样点距离
            不大于1像素。
        */
        const int profileLength =
            std::max(
                3,
                static_cast<int>(
                    std::ceil(searchLengthPx)) + 1);

        cv::Mat mapX(
            1,
            profileLength,
            CV_32FC1);

        cv::Mat mapY(
            1,
            profileLength,
            CV_32FC1);

        const cv::Point2f lineVector =
            lineEnd - lineStart;

        for (int index = 0;
            index < profileLength;
            ++index) {

            const float ratio =
                static_cast<float>(index) /
                static_cast<float>(
                    profileLength - 1);

            const cv::Point2f samplePoint =
                lineStart +
                lineVector * ratio;

            mapX.at<float>(0, index) =
                samplePoint.x;

            mapY.at<float>(0, index) =
                samplePoint.y;
        }

        /*
            将任意角度的测量线采样为一行一维灰度曲线。

            INTER_LINEAR提供双线性插值，
            因此即使不启用梯度峰值亚像素拟合，
            灰度采样本身仍然基于浮点坐标。
        */
        cv::Mat profile8u;

        cv::remap(
            rectifiedGray,
            profile8u,
            mapX,
            mapY,
            cv::INTER_LINEAR,
            cv::BORDER_REPLICATE);

        if (profile8u.empty() ||
            profile8u.cols < 3) {
            return std::nullopt;
        }

        cv::Mat profile32f;

        profile8u.convertTo(
            profile32f,
            CV_32FC1);

        cv::Mat smoothedProfile;

        if (caliperConfig.smoothingKernelSize > 1) {
            cv::GaussianBlur(
                profile32f,
                smoothedProfile,
                cv::Size{
                    caliperConfig.smoothingKernelSize,
                    1
                },
                0.0,
                0.0,
                cv::BORDER_REPLICATE);
        }
        else {
            smoothedProfile = profile32f;
        }

        /*
            profile是一行水平方向的数据。

            因此Sobel的dx=1就是沿卡尺搜索方向计算梯度。
        */
        cv::Mat gradient;

        cv::Sobel(
            smoothedProfile,
            gradient,
            CV_32FC1,
            1,
            0,
            3,
            1.0,
            0.0,
            cv::BORDER_REPLICATE);

        if (gradient.empty() ||
            gradient.cols < 3) {
            return std::nullopt;
        }

        int bestIndex = -1;

        float bestResponse =
            -std::numeric_limits<float>::infinity();

        float bestGradient = 0.0F;

        /*
            跳过首尾位置，保证后续亚像素拟合时能访问左右邻点。
        */
        for (int index = 1;
            index < gradient.cols - 1;
            ++index) {

            const float gradientValue =
                gradient.at<float>(0, index);

            const float response =
                calculatePolarityResponse(
                    gradientValue,
                    polarity);

            if (response > bestResponse) {
                bestResponse = response;
                bestGradient = gradientValue;
                bestIndex = index;
            }
        }

        if (bestIndex < 0 ||
            !std::isfinite(bestResponse) ||
            static_cast<double>(bestResponse) <
            caliperConfig.minimumGradient) {
            return std::nullopt;
        }

        double subpixelOffset = 0.0;

        if (caliperConfig.enableSubpixel &&
            bestIndex > 0 &&
            bestIndex < gradient.cols - 1) {

            /*
                亚像素拟合需要使用方向一致的响应。

                对Any极性，先根据最佳峰值的符号确定当前检测到的是
                正梯度还是负梯度，再对邻域统一变号。相比直接对三个
                梯度值取绝对值，这样能保留峰值曲线的连续性。
            */
            double responseSign = 1.0;

            switch (polarity) {
            case EdgePolarity::DarkToLight:
                responseSign = 1.0;
                break;

            case EdgePolarity::LightToDark:
                responseSign = -1.0;
                break;

            case EdgePolarity::Any:
                responseSign =
                    bestGradient >= 0.0F
                    ? 1.0
                    : -1.0;
                break;
            }

            const auto responseAt =
                [&](const int index) -> double {
                return responseSign *
                    static_cast<double>(
                        gradient.at<float>(
                            0,
                            index));
                };

            const double leftResponse =
                responseAt(bestIndex - 1);

            const double centerResponse =
                responseAt(bestIndex);

            const double rightResponse =
                responseAt(bestIndex + 1);

            const double denominator =
                leftResponse -
                2.0 * centerResponse +
                rightResponse;

            /*
                对局部最大值，抛物线二阶项应小于零。

                只有峰形有效时才进行亚像素修正，
                避免平坦区域或局部最小值产生异常偏移。
            */
            if (denominator < -1e-12) {
                subpixelOffset =
                    0.5 *
                    (leftResponse - rightResponse) /
                    denominator;

                subpixelOffset =
                    std::clamp(
                        subpixelOffset,
                        -1.0,
                        1.0);
            }
        }

        const double profilePosition =
            static_cast<double>(bestIndex) +
            subpixelOffset;

        const double profileRatio =
            profilePosition /
            static_cast<double>(
                profileLength - 1);

        const cv::Point2f edgePoint =
            lineStart +
            lineVector *
            static_cast<float>(profileRatio);

        if (!std::isfinite(edgePoint.x) ||
            !std::isfinite(edgePoint.y)) {
            return std::nullopt;
        }

        return edgePoint;
    }






}	// namespace vision::algorithm::detail