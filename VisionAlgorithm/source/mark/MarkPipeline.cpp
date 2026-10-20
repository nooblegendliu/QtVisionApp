#include "MarkPipeline.h"

#include "MarkGeometry.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vision::algorithm::detail {

	namespace {
        [[nodiscard]] std::string sizeText(cv::Size size)
        {
            return std::to_string(size.width) + "x" +
                std::to_string(size.height);
        }

        void validateRoi(
            const cv::Rect& roi,
            cv::Size inputSize,
            cv::Size templateSize,
            std::string_view role)
        {
            if (roi.x < 0 || roi.y < 0 ||
                roi.width <= 0 || roi.height <= 0) {
                throw std::invalid_argument(
                    "MarkPoint " + std::string(role) +
                    " ROI must have a non-negative origin and positive size.");
            }

            const std::int64_t right =
                static_cast<std::int64_t>(roi.x) + roi.width;
            const std::int64_t bottom =
                static_cast<std::int64_t>(roi.y) + roi.height;

            if (right > inputSize.width || bottom > inputSize.height) {
                throw std::invalid_argument(
                    "MarkPoint " + std::string(role) +
                    " ROI must be inside the configured input image " +
                    sizeText(inputSize) + ".");
            }

            if (templateSize.width > roi.width ||
                templateSize.height > roi.height) {
                throw std::invalid_argument(
                    "MarkPoint template image " + sizeText(templateSize) +
                    " must fit inside the " + std::string(role) +
                    " ROI " + sizeText(roi.size()) + ".");
            }
        }

        void validateCalibrationSize(
            cv::Size calibrationSize,
            cv::Size inputSize,
            std::string_view role)
        {
            if (calibrationSize != inputSize) {
                throw std::invalid_argument(
                    "MarkPoint " + std::string(role) +
                    " calibration image size " +
                    sizeText(calibrationSize) +
                    " does not match the configured input image " +
                    sizeText(inputSize) + ".");
            }
        }

        [[nodiscard]] std::optional<vision::core::Error> validateImage(
            const cv::Mat& image,
            cv::Size expectedSize,
            std::string_view role)
        {
            if (image.empty()) {
                return makeInvalidImageError(
                    role,
                    "The input cv::Mat is empty.");
            }

            if (image.type() != CV_8UC1) {
                return makeInvalidImageError(role, "Expected CV_8UC1, type now: " +
                    std::to_string(image.type()) + ".");
            }

            if (image.size() != expectedSize) {
                return makeInvalidImageError(
                    role,
                    "Runtime size is " + std::to_string(image.cols) + "x" +
                    std::to_string(image.rows) + ", but calibration expects " +
                    std::to_string(expectedSize.width) + "x" +
                    std::to_string(expectedSize.height) + ".");
            }

            return std::nullopt;
        }
	}   // namespace

    void validateMarkPipelineResources(
        const MarkAlgorithmConfig& config,
        cv::Size topLeftCalibrationSize,
        cv::Size topRightCalibrationSize,
        cv::Size bottomRightCalibrationSize,
        cv::Size bottomLeftCalibrationSize)
    {
        const cv::Size inputSize = config.inputImageSizePx;
        if (inputSize.width <= 0 || inputSize.height <= 0) {
            throw std::invalid_argument(
                "MarkPoint input image size must be positive.");
        }

        const cv::Size templateSize =
            config.shapeMatcher.templateImageSizePx;
        if (templateSize.width <= 0 || templateSize.height <= 0) {
            throw std::invalid_argument(
                "MarkPoint template image size must be positive.");
        }

        validateRoi(
            config.topLeftCameraRoiPx,
            inputSize,
            templateSize,
            "TopLeft");
        validateRoi(
            config.topRightCameraRoiPx,
            inputSize,
            templateSize,
            "TopRight");
        validateRoi(
            config.bottomRightCameraRoiPx,
            inputSize,
            templateSize,
            "BottomRight");
        validateRoi(
            config.bottomLeftCameraRoiPx,
            inputSize,
            templateSize,
            "BottomLeft");

        validateCalibrationSize(topLeftCalibrationSize, inputSize, "TopLeft");
        validateCalibrationSize(topRightCalibrationSize, inputSize, "TopRight");
        validateCalibrationSize(bottomRightCalibrationSize, inputSize, "BottomRight");
        validateCalibrationSize(bottomLeftCalibrationSize, inputSize, "BottomLeft");
    }

    MarkPipeline::MarkPipeline(MarkAlgorithmConfig config,
        CameraGeometry topLeftGeometry,
        CameraGeometry topRightGeometry,
        CameraGeometry bottomRightGeometry,
        CameraGeometry bottomLeftGeometry)
        : config_(std::move(config)),
        topLeftGeometry_(std::move(topLeftGeometry)),
        topRightGeometry_(std::move(topRightGeometry)),
        bottomRightGeometry_(std::move(bottomRightGeometry)),
        bottomLeftGeometry_(std::move(bottomLeftGeometry)),
        shapeMatcher_((
            validateMarkPipelineResources(
                config_,
                topLeftGeometry_.imageSize(),
                topRightGeometry_.imageSize(),
                bottomRightGeometry_.imageSize(),
                bottomLeftGeometry_.imageSize()),
            config_.shapeMatcher))
    {

    }


    std::optional<cv::Point2d> MarkPipeline::detectMark(
        const cv::Mat& image,
        const cv::Rect& roi,
        const CameraGeometry& geometry) const
    {
        const cv::Mat rectifiedRoi = geometry.rectifyRoi(image, roi);
        const std::vector<MatchResult> matches =
            shapeMatcher_.match(rectifiedRoi);

        if (matches.empty()) {
            return std::nullopt;
        }

        const auto bestMatch = std::max_element(
            matches.begin(),
            matches.end(),
            [](const MatchResult& lhs, const MatchResult& rhs) {
                return lhs.similarity < rhs.similarity;
            });

        const cv::Point2d fullImagePointPx{
            static_cast<double>(bestMatch->matchPositionPx.x) + roi.x,
            static_cast<double>(bestMatch->matchPositionPx.y) + roi.y
        };

        return geometry.pixelToPhysical(fullImagePointPx);
    }


    vision::core::AlgorithmOutcome MarkPipeline::locate(
        const vision::core::MarkPointAlgorithmInput& input,
        const ExecutionContext& context) const
    {
        if (const auto error = checkDeadline(context, "mark.begin")) {
            return *error;
        }

        const cv::Mat& topLeft = input.topLeft.get();
        const cv::Mat& topRight = input.topRight.get();
        const cv::Mat& bottomRight = input.bottomRight.get();
        const cv::Mat& bottomLeft = input.bottomLeft.get();

        if (const auto error = validateImage(
            topLeft,
            topLeftGeometry_.imageSize(),
            "TopLeft")) {
            return *error;
        }
        if (const auto error = validateImage(
            topRight,
            topRightGeometry_.imageSize(),
            "TopRight")) {
            return *error;
        }
        if (const auto error = validateImage(
            bottomRight,
            bottomRightGeometry_.imageSize(),
            "BottomRight")) {
            return *error;
        }
        if (const auto error = validateImage(
            bottomLeft,
            bottomLeftGeometry_.imageSize(),
            "BottomLeft")) {
            return *error;
        }

        const auto topLeftMark = detectMark(
            topLeft,
            config_.topLeftCameraRoiPx,
            topLeftGeometry_);
        if (const auto error = checkDeadline(
            context,
            "mark.top_left.end")) {
            return *error;
        }
        if (!topLeftMark.has_value()) {
            return makeNoTargetOutput();
        }

        const auto topRightMark = detectMark(
            topRight,
            config_.topRightCameraRoiPx,
            topRightGeometry_);
        if (const auto error = checkDeadline(
            context,
            "mark.top_right.end")) {
            return *error;
        }
        if (!topRightMark.has_value()) {
            return makeNoTargetOutput();
        }

        const auto bottomRightMark = detectMark(
            bottomRight,
            config_.bottomRightCameraRoiPx,
            bottomRightGeometry_);
        if (const auto error = checkDeadline(
            context,
            "mark.bottom_right.end")) {
            return *error;
        }
        if (!bottomRightMark.has_value()) {
            return makeNoTargetOutput();
        }

        const auto bottomLeftMark = detectMark(
            bottomLeft,
            config_.bottomLeftCameraRoiPx,
            bottomLeftGeometry_);
        if (const auto error = checkDeadline(
            context,
            "mark.bottom_left.end")) {
            return *error;
        }
        if (!bottomLeftMark.has_value()) {
            return makeNoTargetOutput();
        }

        const cv::Point2d& topLeftPoint = *topLeftMark;
        const cv::Point2d& topRightPoint = *topRightMark;
        const cv::Point2d& bottomRightPoint = *bottomRightMark;
        const cv::Point2d& bottomLeftPoint = *bottomLeftMark;

        const vision::core::AlgorithmOutput pose = evaluateMarkPose(
            topLeftPoint,
            topRightPoint,
            bottomRightPoint,
            bottomLeftPoint,
            config_.quality);

        if (const auto error = checkDeadline(
            context,
            "mark.result_finalize")) {
            return *error;
        }

        return pose;
    }






}	// namespace vision::algorithm::detail
