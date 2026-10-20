#include "EdgePipeline.h"

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace vision::algorithm::detail {

	namespace {

		std::optional<vision::core::Error> validateImage(
			const cv::Mat& image, const cv::Size expectedSize)
		{
			if (image.empty()) {
				return makeInvalidImageError(
					"Center",
					"The input cv::Mat is empty.");
			}

			if (image.type() != CV_8UC1) {
				return makeInvalidImageError(
					"Center",
					"Expected a CV_8UC1 grayscale image, but runtime type is " +
					std::to_string(image.type()) + ".");
			}

			if (image.size() != expectedSize) {
				return makeInvalidImageError(
					"Center",
					"Runtime size is " + std::to_string(image.cols) + "x" + 
					std::to_string(image.rows) + ", but calibration expects " + 
					std::to_string(expectedSize.width) + "x" +
					std::to_string(expectedSize.height) + ".");
			}
			
			return std::nullopt;
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

		struct PhysicalEdgePose final {
			cv::Point2d center;
			double angleDeg{0.0};
		};

		[[nodiscard]] std::optional<PhysicalEdgePose> solvePhysicalPose(
			const std::array<cv::Point2d, 4>& corners) noexcept
		{
			const cv::Point2d& topLeft = corners[0];
			const cv::Point2d& topRight = corners[1];
			const cv::Point2d& bottomRight = corners[2];
			const cv::Point2d& bottomLeft = corners[3];
			const cv::Point2d leftMidpoint =
				(topLeft + bottomLeft) * 0.5;
			const cv::Point2d rightMidpoint =
				(topRight + bottomRight) * 0.5;
			const cv::Point2d horizontalDirection =
				rightMidpoint - leftMidpoint;
			const double directionNorm = std::hypot(
				horizontalDirection.x,
				horizontalDirection.y);

			if (!std::isfinite(directionNorm) ||
				directionNorm <= std::numeric_limits<double>::epsilon()) {
				return std::nullopt;
			}

			const cv::Point2d center =
				(topLeft + topRight + bottomRight + bottomLeft) * 0.25;
			double angleDeg = std::atan2(
				horizontalDirection.y,
				horizontalDirection.x) * 180.0 / CV_PI;
			angleDeg = normalizeLineAngleDeg(angleDeg);

			if (!std::isfinite(center.x) ||
				!std::isfinite(center.y) ||
				!std::isfinite(angleDeg)) {
				return std::nullopt;
			}

			return PhysicalEdgePose{center, angleDeg};
		}

		[[nodiscard]] DemoOverlayPoint makeOverlayPoint(
			const cv::Point2d& point) noexcept
		{
			return DemoOverlayPoint{point.x, point.y};
		}

		[[nodiscard]] DemoOverlayPoint makeOverlayPoint(
			const cv::Point2f& point) noexcept
		{
			return DemoOverlayPoint{
				static_cast<double>(point.x),
				static_cast<double>(point.y)};
		}

		[[nodiscard]] DemoOverlayRoi makeOverlayRoi(
			const CaliperRoiConfig& roi)
		{
			const double radians = roi.scanAngleDeg * CV_PI / 180.0;
			const cv::Point2d scanDirection{
				std::cos(radians),
				std::sin(radians)};
			const cv::Point2d spanDirection{
				-scanDirection.y,
				scanDirection.x};
			const cv::Point2d center{
				static_cast<double>(roi.centerPx.x),
				static_cast<double>(roi.centerPx.y)};
			const cv::Point2d scanOffset =
				scanDirection * (roi.searchLengthPx * 0.5);
			const cv::Point2d spanOffset =
				spanDirection * (roi.spanLengthPx * 0.5);

			return DemoOverlayRoi{{
				makeOverlayPoint(center - scanOffset - spanOffset),
				makeOverlayPoint(center + scanOffset - spanOffset),
				makeOverlayPoint(center + scanOffset + spanOffset),
				makeOverlayPoint(center - scanOffset + spanOffset)}};
		}

		void appendRoiPair(
			std::vector<DemoOverlayRoi>& destination,
			const RoiPairConfig& pair)
		{
			destination.push_back(makeOverlayRoi(pair.first));
			destination.push_back(makeOverlayRoi(pair.second));
		}

		[[nodiscard]] DemoOverlayLine makeOverlayLine(
			const cv::Vec4f& fittedLine,
			const cv::Size imageSize) noexcept
		{
			const double extent = 2.0 * std::hypot(
				static_cast<double>(imageSize.width),
				static_cast<double>(imageSize.height));
			const cv::Point2d direction{
				static_cast<double>(fittedLine[0]),
				static_cast<double>(fittedLine[1])};
			const cv::Point2d point{
				static_cast<double>(fittedLine[2]),
				static_cast<double>(fittedLine[3])};

			return DemoOverlayLine{
				makeOverlayPoint(point - direction * extent),
				makeOverlayPoint(point + direction * extent)};
		}

		[[nodiscard]] DemoEdgeVisualization makeBaseVisualization(
			const cv::Mat& rectifiedImage,
			const EdgeAlgorithmConfig& config)
		{
			DemoEdgeVisualization visualization;
			visualization.rectifiedImage = rectifiedImage.clone();
			visualization.rois.reserve(8U);
			appendRoiPair(visualization.rois, config.top);
			appendRoiPair(visualization.rois, config.right);
			appendRoiPair(visualization.rois, config.bottom);
			appendRoiPair(visualization.rois, config.left);
			return visualization;
		}

	}	// namespace


	EdgePipeline::EdgePipeline(
		EdgeAlgorithmConfig config,
		CameraGeometry centerGeometry,
		std::shared_ptr<DemoEdgeVisualizationStore> demoVisualization)
		: config_(std::move(config)),
		  centerGeometry_(std::move(centerGeometry)),
		  demoVisualization_(std::move(demoVisualization))
	{

	}


	vision::core::AlgorithmOutcome EdgePipeline::locate(
		const vision::core::EdgeAlgorithmInput& input, const ExecutionContext& context) const
	{
		/*
			阶段1：算法入口超时检查
		*/
		if (const auto error = checkDeadline(context, "edge.begin")) {
			return *error;
		}


		/*
			阶段2: 输入图像检查
		*/
		const cv::Mat& center = input.center.get();
		if (const auto error = validateImage(center, centerGeometry_.imageSize())) {
			return *error;
		}

		/*
			阶段3: 畸变校正
		*/
		const cv::Mat rectifiedCenter = centerGeometry_.rectify(center);
		if (const auto error =
			checkDeadline(context, "edge.rectify.end")) {
			return *error;
		}

		if (rectifiedCenter.empty() ||
			(rectifiedCenter.type() != CV_8UC1) ||
			rectifiedCenter.size() != centerGeometry_.imageSize()) {
			return makeAlgorithmError(
				"Edge image rectification failed",
				"check input data type or size"
			);
		}

		/*
			阶段4: 检测顶部边缘
		*/
		auto topResult =
			lineDetector_.detect(
				rectifiedCenter,
				config_.top,
				config_.caliper);
		if (const auto error =
			checkDeadline(context, "edge.top.end")) {
			return *error;
		}

		if (!topResult.has_value()) {
			return makeNoTargetOutput();
		}

		/*
			阶段5: 检测右部边缘
		*/
		auto rightResult =
			lineDetector_.detect(
				rectifiedCenter,
				config_.right,
				config_.caliper);
		if (const auto error =
			checkDeadline(context, "edge.right.end")) {
			return *error;
		}

		if (!rightResult.has_value()) {
			return makeNoTargetOutput();
		}

		/*
			阶段6: 检测底部边缘
		*/
		auto bottomResult =
			lineDetector_.detect(
				rectifiedCenter,
				config_.bottom,
				config_.caliper);
		if (const auto error =
			checkDeadline(context, "edge.bottom.end")) {
			return *error;
		}

		if (!bottomResult.has_value()) {
			return makeNoTargetOutput();
		}

		/*
			阶段7: 检测左部边缘
		*/
		auto leftResult =
			lineDetector_.detect(
				rectifiedCenter,
				config_.left,
				config_.caliper);
		if (const auto error =
			checkDeadline(context, "edge.left.end")) {
			return *error;
		}

		if (!leftResult.has_value()) {
			return makeNoTargetOutput();
		}

		/*
			阶段8: 组装检测结果
		*/
		DetectedEdgeLines detectedLines;

		detectedLines.top =
			std::move(*topResult);

		detectedLines.right =
			std::move(*rightResult);

		detectedLines.bottom =
			std::move(*bottomResult);

		detectedLines.left =
			std::move(*leftResult);


		/*
			阶段9: 几何计算
		*/
		const auto geometryResult =
			geometry_.solve(detectedLines);
		if (const auto error =
			checkDeadline(context, "edge.geometry.end")) {
			return *error;
		}

		if (!geometryResult.has_value()) {
			return makeNoTargetOutput();
		}

		std::array<cv::Point2d, 4> physicalCorners;
		for (std::size_t index = 0;
			index < geometryResult->corners.size();
			++index) {
			physicalCorners[index] =
				centerGeometry_.pixelToPhysical(
					geometryResult->corners[index]);
		}

		const auto physicalPose = solvePhysicalPose(physicalCorners);
		if (!physicalPose.has_value()) {
			return makeNoTargetOutput();
		}

		const double topLength =
			pointDistance(physicalCorners[0], physicalCorners[1]);
		const double rightLength =
			pointDistance(physicalCorners[1], physicalCorners[2]);
		const double bottomLength =
			pointDistance(physicalCorners[3], physicalCorners[2]);
		const double leftLength =
			pointDistance(physicalCorners[0], physicalCorners[3]);
		const double diagonalTopLeftToBottomRight =
			pointDistance(physicalCorners[0], physicalCorners[2]);
		const double diagonalTopRightToBottomLeft =
			pointDistance(physicalCorners[1], physicalCorners[3]);

		const bool edgeModeQualified =
			isInRange(topLength, config_.quality.topLength) &&
			isInRange(rightLength, config_.quality.rightLength) &&
			isInRange(bottomLength, config_.quality.bottomLength) &&
			isInRange(leftLength, config_.quality.leftLength) &&
			isInRange(
				diagonalTopLeftToBottomRight,
				config_.quality.diagonalTopLeftToBottomRight) &&
			isInRange(
				diagonalTopRightToBottomLeft,
				config_.quality.diagonalTopRightToBottomLeft);

		const cv::Point2d imageCenterPx{
			static_cast<double>(rectifiedCenter.cols) * 0.5,
			static_cast<double>(rectifiedCenter.rows) * 0.5};
		const cv::Point2d imageCenterPhysical =
			centerGeometry_.pixelToPhysical(imageCenterPx);
		const double centerOffset =
			pointDistance(physicalPose->center, imageCenterPhysical);
		const double absoluteAngleDeg = std::abs(physicalPose->angleDeg);

		const bool offsetResultQualified =
			centerOffset <= config_.quality.maximumCenterOffset &&
			isInRange(
				absoluteAngleDeg,
				config_.quality.absoluteAngleDeg);

		if (const auto error = checkDeadline(
			context,
			"edge.coordinate_transform.end")) {
			return *error;
		}

		if (demoVisualization_) {
			DemoEdgeVisualization visualization =
				makeBaseVisualization(rectifiedCenter, config_);
			visualization.fittedLines.reserve(4U);
			visualization.fittedLines.push_back(
				makeOverlayLine(detectedLines.top.line, rectifiedCenter.size()));
			visualization.fittedLines.push_back(
				makeOverlayLine(detectedLines.right.line, rectifiedCenter.size()));
			visualization.fittedLines.push_back(
				makeOverlayLine(detectedLines.bottom.line, rectifiedCenter.size()));
			visualization.fittedLines.push_back(
				makeOverlayLine(detectedLines.left.line, rectifiedCenter.size()));

			visualization.corners.reserve(geometryResult->corners.size());
			for (const cv::Point2d& corner : geometryResult->corners) {
				visualization.corners.push_back(makeOverlayPoint(corner));
			}
			demoVisualization_->publish(visualization);
		}

		if (!edgeModeQualified || !offsetResultQualified) {
			return makeNoTargetOutput();
		}

		if (const auto error =
			checkDeadline(context, "edge.result_finalize")) {
			return *error;
		}

		return makeValidOutput(
			physicalPose->center.x,
			physicalPose->center.y,
			physicalPose->angleDeg);
	}
}	// namespace vision::algorithm::detail
