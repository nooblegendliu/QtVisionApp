#include <VisionAlgorithm/Calibration.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "AlgorithmConfig.h"
#include "CameraGeometry.h"

namespace vision::algorithm {
	namespace {

		using vision::core::Error;
		using vision::core::ErrorCode;

		Error makeInvalidConfigurationError(std::string_view diagnostic)
		{
			return Error{
				ErrorCode::InvalidConfiguration,
				"CalibrationAlgorithm configuration or resources are invalid.",
				std::string(diagnostic)
			};
		}

		Error makeInitializationFailureError(std::string_view diagnostic)
		{
			return Error{
				ErrorCode::InternalFailure,
				"CalibrationAlgorithm initialization failed unexpectedly.",
				std::string(diagnostic)
			};
		}

		Error makeUnknownInitializationFailureError()
		{
			return Error{
				ErrorCode::InternalFailure,
				"CalibrationAlgorithm initialization failed unexpectedly.",
				"A non-standard exception escaped from calibration initialization."
			};
		}

		cv::SimpleBlobDetector::Params makeBlobParameters(
			const detail::CircleDetectionConfig& configuration)
		{
			cv::SimpleBlobDetector::Params parameters;
			parameters.minThreshold = configuration.minimumThreshold;
			parameters.maxThreshold = configuration.maximumThreshold;
			parameters.thresholdStep = configuration.thresholdStep;
			parameters.minRepeatability = configuration.minimumRepeatability;
			parameters.minDistBetweenBlobs = configuration.minimumDistancePx;
			parameters.filterByColor = true;
			parameters.blobColor = configuration.blobColor ==
				detail::CalibrationBlobColor::Dark ? 0 : 255;
			parameters.filterByArea = true;
			parameters.minArea = configuration.minimumAreaPx2;
			parameters.maxArea = configuration.maximumAreaPx2;
			parameters.filterByCircularity = true;
			parameters.minCircularity = configuration.minimumCircularity;
			parameters.filterByConvexity = false;
			parameters.filterByInertia = false;
			parameters.collectContours = false;
			// Keep OpenCV defaults for all other fields, including maxCircularity.
			return parameters;
		}

		bool isValidOrientation(CalibrationGridOrientation orientation) noexcept
		{
			switch (orientation) {
			case CalibrationGridOrientation::P1TopLeftXRight:
			case CalibrationGridOrientation::P1TopLeftXDown:
			case CalibrationGridOrientation::P1TopRightXLeft:
			case CalibrationGridOrientation::P1TopRightXDown:
			case CalibrationGridOrientation::P1BottomRightXLeft:
			case CalibrationGridOrientation::P1BottomRightXUp:
			case CalibrationGridOrientation::P1BottomLeftXRight:
			case CalibrationGridOrientation::P1BottomLeftXUp:
				return true;
			default:
				return false;
			}
		}

		std::size_t orientedImageIndex(int side, std::size_t pointIndex,
			CalibrationGridOrientation orientation) noexcept
		{
			const int x = static_cast<int>(pointIndex % static_cast<std::size_t>(side));
			const int y = static_cast<int>(pointIndex / static_cast<std::size_t>(side));
			int row = 0;
			int column = 0;
			switch (orientation) {
			case CalibrationGridOrientation::P1TopLeftXRight:
				row = y; column = x; break;
			case CalibrationGridOrientation::P1TopLeftXDown:
				row = x; column = y; break;
			case CalibrationGridOrientation::P1TopRightXLeft:
				row = y; column = side - 1 - x; break;
			case CalibrationGridOrientation::P1TopRightXDown:
				row = x; column = side - 1 - y; break;
			case CalibrationGridOrientation::P1BottomRightXLeft:
				row = side - 1 - y; column = side - 1 - x; break;
			case CalibrationGridOrientation::P1BottomRightXUp:
				row = side - 1 - x; column = side - 1 - y; break;
			case CalibrationGridOrientation::P1BottomLeftXRight:
				row = side - 1 - y; column = x; break;
			case CalibrationGridOrientation::P1BottomLeftXUp:
				row = side - 1 - x; column = y; break;
			default:
				break;
			}
			return static_cast<std::size_t>(row * side + column);
		}

		bool containsDuplicate(const std::vector<cv::Point2d>& points) noexcept
		{
			for (std::size_t index = 0; index < points.size(); ++index) {
				for (std::size_t previous = 0; previous < index; ++previous) {
					if (points[index] == points[previous]) {
						return true;
					}
				}
			}
			return false;
		}

		bool containsNonCollinearTriple(
			const std::vector<cv::Point2d>& points) noexcept
		{
			if (points.size() < 3) {
				return false;
			}

			std::size_t first = 0;
			std::size_t second = 0;
			double maximumDistanceSquared = 0.0;
			for (std::size_t left = 0; left < points.size(); ++left) {
				for (std::size_t right = left + 1; right < points.size(); ++right) {
					const double dx = points[right].x - points[left].x;
					const double dy = points[right].y - points[left].y;
					const double distanceSquared = dx * dx + dy * dy;
					if (!std::isfinite(distanceSquared)) {
						return false;
					}
					if (distanceSquared > maximumDistanceSquared) {
						maximumDistanceSquared = distanceSquared;
						first = left;
						second = right;
					}
				}
			}
			if (maximumDistanceSquared == 0.0) {
				return false;
			}

			const cv::Point2d baseline = points[second] - points[first];
			double maximumCrossProduct = 0.0;
			for (const auto& point : points) {
				const cv::Point2d offset = point - points[first];
				const double crossProduct = std::abs(
					baseline.x * offset.y - baseline.y * offset.x);
				if (!std::isfinite(crossProduct)) {
					return false;
				}
				maximumCrossProduct = std::max(maximumCrossProduct, crossProduct);
			}

			const double tolerance = 64.0 * std::numeric_limits<double>::epsilon() *
				std::max(1.0, maximumDistanceSquared);
			return maximumCrossProduct > tolerance;
		}

	} // namespace

	struct CalibrationAlgorithm::Resources final {
		detail::CalibrationAlgorithmConfig configuration;
		detail::CameraGeometry center;
		detail::CameraGeometry topLeft;
		detail::CameraGeometry topRight;
		detail::CameraGeometry bottomRight;
		detail::CameraGeometry bottomLeft;

		explicit Resources(const detail::VisionAlgorithmConfig& config)
			: configuration(detail::loadCalibrationAlgorithmConfig(
				config.calibration.parameterPath)),
			center(detail::CameraGeometry::loadFromFile(
				config.calibration.centerPath)),
			topLeft(detail::CameraGeometry::loadFromFile(
				config.calibration.topLeftPath)),
			topRight(detail::CameraGeometry::loadFromFile(
				config.calibration.topRightPath)),
			bottomRight(detail::CameraGeometry::loadFromFile(
				config.calibration.bottomRightPath)),
			bottomLeft(detail::CameraGeometry::loadFromFile(
				config.calibration.bottomLeftPath))
		{
		}
	};

	CalibrationAlgorithm::CreateOutcome CalibrationAlgorithm::create(
		const std::filesystem::path& mainConfigPath)
	{
		try {
			return std::unique_ptr<CalibrationAlgorithm>(
				new CalibrationAlgorithm(mainConfigPath));
		}
		catch (const std::invalid_argument& exception) {
			return makeInvalidConfigurationError(exception.what());
		}
		catch (const std::exception& exception) {
			return makeInitializationFailureError(exception.what());
		}
		catch (...) {
			return makeUnknownInitializationFailureError();
		}
	}

	CalibrationAlgorithm::CalibrationAlgorithm(
		const std::filesystem::path& mainConfigPath)
		: resources_(std::make_unique<Resources>(
			detail::loadVisionAlgorithmConfig(mainConfigPath)))
	{
	}

	CalibrationAlgorithm::~CalibrationAlgorithm() = default;

	CalibrationDetectionOutcome CalibrationAlgorithm::detect(
		vision::core::CameraRole cameraRole,
		const cv::Mat& rawImage) const
	{
		using Role = vision::core::CameraRole;
		std::string_view stage = "resources";
		cv::Size expectedSize;
		const auto failure = [&](ErrorCode code, std::string_view message,
			std::string_view detail) {
			return Error{ code, std::string(message),
				"CalibrationAlgorithm::detect; camera_role=" +
				std::to_string(static_cast<int>(cameraRole)) +
				"; stage=" + std::string(stage) +
				"; raw_size=" + std::to_string(rawImage.cols) + "x" +
				std::to_string(rawImage.rows) +
				"; raw_dims=" + std::to_string(rawImage.dims) +
				"; expected_size=" + std::to_string(expectedSize.width) + "x" +
				std::to_string(expectedSize.height) + "; " + std::string(detail) };
		};

		try {
			if (!resources_) {
				return failure(ErrorCode::InternalFailure,
					"Calibration detection resources are unavailable.",
					"The initialized resources are missing.");
			}

			stage = "select_role";
			const detail::CameraGeometry* geometry = nullptr;
			const detail::CircleDetectionConfig* configuration = nullptr;
			switch (cameraRole) {
			case Role::Center:
				geometry = &resources_->center;
				configuration = &resources_->configuration.center;
				break;
			case Role::TopLeft:
				geometry = &resources_->topLeft;
				configuration = &resources_->configuration.topLeft;
				break;
			case Role::TopRight:
				geometry = &resources_->topRight;
				configuration = &resources_->configuration.topRight;
				break;
			case Role::BottomRight:
				geometry = &resources_->bottomRight;
				configuration = &resources_->configuration.bottomRight;
				break;
			case Role::BottomLeft:
				geometry = &resources_->bottomLeft;
				configuration = &resources_->configuration.bottomLeft;
				break;
			case Role::Unknown:
			default:
				return failure(ErrorCode::InvalidConfiguration,
					"Calibration camera role is invalid.",
					"A known calibration camera role is required.");
			}

			stage = "validate_input";
			expectedSize = geometry->imageSize();
			if (rawImage.empty() || rawImage.dims != 2 ||
				rawImage.type() != CV_8UC1 || !rawImage.isContinuous() ||
				rawImage.size() != expectedSize) {
				return failure(ErrorCode::InvalidImage,
					"Calibration input image is invalid.",
					"Expected a non-empty, two-dimensional, continuous CV_8UC1 image "
					"matching the camera calibration size.");
			}

			stage = "rectify";
			CalibrationDetection result;
			result.cameraRole = cameraRole;
			result.rectifiedImage = geometry->rectify(rawImage);
			if (!result.rectifiedImage.isContinuous()) {
				result.rectifiedImage = result.rectifiedImage.clone();
			}

			stage = "create_blob_detector";
			const auto detector = cv::SimpleBlobDetector::create(
				makeBlobParameters(*configuration));
			stage = "find_circles_grid";
			const int side = resources_->configuration.gridSidePoints;
			const auto expectedPointCount = static_cast<std::size_t>(side * side);
			cv::CirclesGridFinderParameters gridParameters;
			if (side == 3) {
				// A 3x3 grid has only six adjacent displacement vectors per direction.
				gridParameters.minDensity = 6;
			}
			std::vector<cv::Point2f> centers;
			const bool found = cv::findCirclesGrid(result.rectifiedImage,
				cv::Size{ side, side }, centers, cv::CALIB_CB_SYMMETRIC_GRID,
				detector, gridParameters);
			if (!found) {
				// OpenCV may leave partial centers; no partial grid is usable.
				return result;
			}

			stage = "validate_centers";
			if (centers.size() != expectedPointCount) {
				return failure(ErrorCode::AlgorithmFailure,
					"Calibration grid detection returned invalid points.",
					"Expected " + std::to_string(expectedPointCount) +
					" centers for grid_side_points=" + std::to_string(side) +
					"; detected=" + std::to_string(centers.size()) + ".");
			}
			for (std::size_t index = 0; index < centers.size(); ++index) {
				const auto& point = centers[index];
				if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
					point.x < 0 || point.y < 0 ||
					point.x >= result.rectifiedImage.cols ||
					point.y >= result.rectifiedImage.rows) {
					return failure(ErrorCode::AlgorithmFailure,
						"Calibration grid detection returned invalid points.",
						"A center is non-finite or outside the rectified image.");
				}
				for (std::size_t previous = 0; previous < index; ++previous) {
					if (point == centers[previous]) {
						return failure(ErrorCode::AlgorithmFailure,
							"Calibration grid detection returned invalid points.",
							"The detected centers contain a duplicate.");
					}
				}
			}
			result.imagePoints.reserve(centers.size());
			for (const auto& center : centers) {
				result.imagePoints.emplace_back(center.x, center.y);
			}
			result.usableForSolving = true;
			return result;
		}
		catch (const cv::Exception& exception) {
			return failure(ErrorCode::AlgorithmFailure,
				"Calibration image processing failed.", exception.what());
		}
		catch (const std::exception& exception) {
			return failure(ErrorCode::InternalFailure,
				"Calibration detection failed unexpectedly.", exception.what());
		}
		catch (...) {
			return failure(ErrorCode::InternalFailure,
				"Calibration detection failed unexpectedly.",
				"A non-standard exception escaped from calibration detection.");
		}
	}

	CalibrationSolutionOutcome CalibrationAlgorithm::solve(
		const CalibrationDetection& detection,
		const PhysicalPointSet& physicalPoints,
		CalibrationGridOrientation orientation) const
	{
		using Role = vision::core::CameraRole;
		std::string_view stage = "resources";
		int side = 0;
		cv::Size expectedSize;
		const auto failure = [&](ErrorCode code, std::string_view message,
			std::string_view detail) {
			return Error{ code, std::string(message),
				"CalibrationAlgorithm::solve; camera_role=" +
				std::to_string(static_cast<int>(detection.cameraRole)) +
				"; orientation=" + std::to_string(static_cast<int>(orientation)) +
				"; stage=" + std::string(stage) +
				"; grid_side_points=" + std::to_string(side) +
				"; image_points=" + std::to_string(detection.imagePoints.size()) +
				"; physical_points=" + std::to_string(physicalPoints.size()) +
				"; expected_size=" + std::to_string(expectedSize.width) + "x" +
				std::to_string(expectedSize.height) + "; " + std::string(detail) };
		};

		try {
			if (!resources_) {
				return failure(ErrorCode::InternalFailure,
					"Calibration solving resources are unavailable.",
					"The initialized resources are missing.");
			}
			side = resources_->configuration.gridSidePoints;
			const auto expectedPointCount = static_cast<std::size_t>(side * side);

			stage = "validate_orientation";
			if (!isValidOrientation(orientation)) {
				return failure(ErrorCode::InvalidConfiguration,
					"Calibration grid orientation is invalid.",
					"One of the eight supported grid orientations is required.");
			}

			stage = "validate_detection_role";
			const detail::CameraGeometry* geometry = nullptr;
			switch (detection.cameraRole) {
			case Role::Center: geometry = &resources_->center; break;
			case Role::TopLeft: geometry = &resources_->topLeft; break;
			case Role::TopRight: geometry = &resources_->topRight; break;
			case Role::BottomRight: geometry = &resources_->bottomRight; break;
			case Role::BottomLeft: geometry = &resources_->bottomLeft; break;
			case Role::Unknown:
			default:
				return failure(ErrorCode::InvalidResult,
					"Calibration detection result is invalid.",
					"The detection does not contain a known camera role.");
			}

			stage = "validate_detection_state";
			if (!detection.usableForSolving) {
				return failure(ErrorCode::InvalidResult,
					"Calibration detection result is not usable for solving.",
					"A complete validated grid detection is required.");
			}

			stage = "validate_rectified_image";
			expectedSize = geometry->imageSize();
			if (detection.rectifiedImage.empty() ||
				detection.rectifiedImage.dims != 2 ||
				detection.rectifiedImage.type() != CV_8UC1 ||
				!detection.rectifiedImage.isContinuous() ||
				detection.rectifiedImage.size() != expectedSize) {
				return failure(ErrorCode::InvalidImage,
					"Calibration rectified image is invalid.",
					"Expected a non-empty, two-dimensional, continuous CV_8UC1 image "
					"matching the camera calibration size.");
			}

			stage = "validate_image_points";
			if (detection.imagePoints.size() != expectedPointCount) {
				return failure(ErrorCode::InvalidResult,
					"Calibration detection points are invalid.",
					"The detection point count does not match the configured grid.");
			}
			for (const auto& point : detection.imagePoints) {
				if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
					point.x < 0.0 || point.y < 0.0 ||
					point.x >= detection.rectifiedImage.cols ||
					point.y >= detection.rectifiedImage.rows) {
					return failure(ErrorCode::InvalidResult,
						"Calibration detection points are invalid.",
						"An image point is non-finite or outside the rectified image.");
				}
			}
			if (containsDuplicate(detection.imagePoints) ||
				!containsNonCollinearTriple(detection.imagePoints)) {
				return failure(ErrorCode::InvalidResult,
					"Calibration detection points are invalid.",
					"Image points must be unique and contain a non-collinear triple.");
			}

			stage = "validate_physical_points";
			if (physicalPoints.size() != expectedPointCount) {
				return failure(ErrorCode::InvalidConfiguration,
					"Calibration physical points are invalid.",
					"The physical point count does not match the configured grid.");
			}
			for (const auto& point : physicalPoints) {
				if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
					return failure(ErrorCode::InvalidConfiguration,
						"Calibration physical points are invalid.",
						"Physical coordinates must be finite.");
				}
			}
			if (containsDuplicate(physicalPoints) ||
				!containsNonCollinearTriple(physicalPoints)) {
				return failure(ErrorCode::InvalidConfiguration,
					"Calibration physical points are invalid.",
					"Physical points must be unique and contain a non-collinear triple.");
			}

			stage = "map_orientation";
			std::vector<cv::Point2d> orderedImagePoints;
			orderedImagePoints.reserve(expectedPointCount);
			for (std::size_t pointIndex = 0;
				pointIndex < expectedPointCount; ++pointIndex) {
				orderedImagePoints.push_back(detection.imagePoints[
					orientedImageIndex(side, pointIndex, orientation)]);
			}

			stage = "find_homography";
			cv::Mat inlierMask;
			const cv::Mat homography = cv::findHomography(
				orderedImagePoints, physicalPoints, cv::RANSAC,
				resources_->configuration.ransacThresholdMm, inlierMask);
			if (homography.empty() || homography.rows != 3 || homography.cols != 3 ||
				homography.channels() != 1) {
				return failure(ErrorCode::AlgorithmFailure,
					"Calibration homography could not be solved.",
					"OpenCV did not return a 3x3 homography matrix.");
			}

			cv::Mat homography64;
			homography.convertTo(homography64, CV_64F);
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
					if (!std::isfinite(homography64.at<double>(row, column))) {
						return failure(ErrorCode::AlgorithmFailure,
							"Calibration homography is invalid.",
							"The solved matrix contains NaN or infinity.");
					}
				}
			}
			constexpr double minimumDeterminant = 1.0e-12;
			if (std::abs(cv::determinant(homography64)) <= minimumDeterminant) {
				return failure(ErrorCode::AlgorithmFailure,
					"Calibration homography is invalid.",
					"The solved matrix is singular.");
			}

			if (inlierMask.total() != expectedPointCount ||
				inlierMask.type() != CV_8UC1) {
				return failure(ErrorCode::AlgorithmFailure,
					"Calibration RANSAC result is invalid.",
					"The inlier mask does not match the configured grid.");
			}
			const cv::Mat flatMask = inlierMask.reshape(1, 1);
			for (std::size_t index = 0; index < expectedPointCount; ++index) {
				const auto value = flatMask.at<unsigned char>(0, static_cast<int>(index));
				if (value != 0 && value != 1) {
					return failure(ErrorCode::AlgorithmFailure,
						"Calibration RANSAC result is invalid.",
						"The inlier mask contains an unsupported value.");
				}
			}

			stage = "compute_residuals";
			std::vector<cv::Point2d> projectedPoints;
			cv::perspectiveTransform(
				orderedImagePoints, projectedPoints, homography64);
			if (projectedPoints.size() != expectedPointCount) {
				return failure(ErrorCode::AlgorithmFailure,
					"Calibration residual calculation failed.",
					"The projected point count does not match the configured grid.");
			}

			CalibrationSolution solution;
			solution.cameraRole = detection.cameraRole;
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
					solution.imageToPhysical(row, column) =
						homography64.at<double>(row, column);
				}
			}
			solution.points.reserve(expectedPointCount);
			for (std::size_t pointIndex = 0;
				pointIndex < expectedPointCount; ++pointIndex) {
				const auto& projected = projectedPoints[pointIndex];
				if (!std::isfinite(projected.x) || !std::isfinite(projected.y)) {
					return failure(ErrorCode::AlgorithmFailure,
						"Calibration residual calculation failed.",
						"A transformed coordinate is NaN or infinity.");
				}
				const double residual = cv::norm(
					projected - physicalPoints[pointIndex]);
				if (!std::isfinite(residual)) {
					return failure(ErrorCode::AlgorithmFailure,
						"Calibration residual calculation failed.",
						"A point residual is NaN or infinity.");
				}
				solution.points.push_back(CalibrationPoint{
					.pointIndex = pointIndex,
					.imagePoint = orderedImagePoints[pointIndex],
					.physicalPoint = physicalPoints[pointIndex],
					.residual = residual,
					.used = flatMask.at<unsigned char>(
						0, static_cast<int>(pointIndex)) != 0
				});
			}
			return solution;
		}
		catch (const cv::Exception& exception) {
			return failure(ErrorCode::AlgorithmFailure,
				"Calibration homography solving failed.", exception.what());
		}
		catch (const std::exception& exception) {
			return failure(ErrorCode::InternalFailure,
				"Calibration solving failed unexpectedly.", exception.what());
		}
		catch (...) {
			return failure(ErrorCode::InternalFailure,
				"Calibration solving failed unexpectedly.",
				"A non-standard exception escaped from calibration solving.");
		}
	}

	CalibrationEvaluationOutcome CalibrationAlgorithm::evaluate(
		const CalibrationSolution& solution) const
	{
		using Role = vision::core::CameraRole;
		std::string_view stage = "resources";
		int side = 0;
		cv::Size expectedSize;
		std::size_t usedPointCount = 0;
		const auto failure = [&](ErrorCode code, std::string_view message,
			std::string_view detail) {
			return Error{ code, std::string(message),
				"CalibrationAlgorithm::evaluate; camera_role=" +
				std::to_string(static_cast<int>(solution.cameraRole)) +
				"; stage=" + std::string(stage) +
				"; grid_side_points=" + std::to_string(side) +
				"; solution_points=" + std::to_string(solution.points.size()) +
				"; used_points=" + std::to_string(usedPointCount) +
				"; expected_size=" + std::to_string(expectedSize.width) + "x" +
				std::to_string(expectedSize.height) + "; " + std::string(detail) };
		};

		try {
			if (!resources_) {
				return failure(ErrorCode::InternalFailure,
					"Calibration evaluation resources are unavailable.",
					"The initialized resources are missing.");
			}
			side = resources_->configuration.gridSidePoints;
			const auto expectedPointCount = static_cast<std::size_t>(side * side);

			stage = "validate_solution_role";
			const detail::CameraGeometry* geometry = nullptr;
			switch (solution.cameraRole) {
			case Role::Center: geometry = &resources_->center; break;
			case Role::TopLeft: geometry = &resources_->topLeft; break;
			case Role::TopRight: geometry = &resources_->topRight; break;
			case Role::BottomRight: geometry = &resources_->bottomRight; break;
			case Role::BottomLeft: geometry = &resources_->bottomLeft; break;
			case Role::Unknown:
			default:
				return failure(ErrorCode::InvalidResult,
					"Calibration solution is invalid.",
					"The solution does not contain a known camera role.");
			}
			expectedSize = geometry->imageSize();

			stage = "validate_homography";
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
					if (!std::isfinite(solution.imageToPhysical(row, column))) {
						return failure(ErrorCode::InvalidResult,
							"Calibration solution is invalid.",
							"The image-to-physical matrix contains NaN or infinity.");
					}
				}
			}
			const auto& matrix = solution.imageToPhysical;
			const double determinant =
				matrix(0, 0) * (matrix(1, 1) * matrix(2, 2) -
					matrix(1, 2) * matrix(2, 1)) -
				matrix(0, 1) * (matrix(1, 0) * matrix(2, 2) -
					matrix(1, 2) * matrix(2, 0)) +
				matrix(0, 2) * (matrix(1, 0) * matrix(2, 1) -
					matrix(1, 1) * matrix(2, 0));
			constexpr double minimumDeterminant = 1.0e-12;
			if (!std::isfinite(determinant) ||
				std::abs(determinant) <= minimumDeterminant) {
				return failure(ErrorCode::InvalidResult,
					"Calibration solution is invalid.",
					"The image-to-physical matrix is singular.");
			}

			stage = "validate_points";
			if (solution.points.size() != expectedPointCount) {
				return failure(ErrorCode::InvalidResult,
					"Calibration solution points are invalid.",
					"The solution point count does not match the configured grid.");
			}

			std::vector<cv::Point2d> imagePoints;
			std::vector<cv::Point2d> physicalPoints;
			imagePoints.reserve(expectedPointCount);
			physicalPoints.reserve(expectedPointCount);
			double maximumError = 0.0;
			double residualScale = 0.0;
			double scaledSquareSum = 0.0;
			for (std::size_t pointIndex = 0;
				pointIndex < expectedPointCount; ++pointIndex) {
				const auto& point = solution.points[pointIndex];
				if (point.pointIndex != pointIndex) {
					return failure(ErrorCode::InvalidResult,
						"Calibration solution points are invalid.",
						"Point indices must match the physical P1-to-PN order.");
				}
				if (!std::isfinite(point.imagePoint.x) ||
					!std::isfinite(point.imagePoint.y) ||
					point.imagePoint.x < 0.0 || point.imagePoint.y < 0.0 ||
					point.imagePoint.x >= expectedSize.width ||
					point.imagePoint.y >= expectedSize.height) {
					return failure(ErrorCode::InvalidResult,
						"Calibration solution points are invalid.",
						"An image point is non-finite or outside the camera image.");
				}
				if (!std::isfinite(point.physicalPoint.x) ||
					!std::isfinite(point.physicalPoint.y)) {
					return failure(ErrorCode::InvalidResult,
						"Calibration solution points are invalid.",
						"A physical point contains NaN or infinity.");
				}
				if (!std::isfinite(point.residual) || point.residual < 0.0) {
					return failure(ErrorCode::InvalidResult,
						"Calibration solution residuals are invalid.",
						"A residual is negative, NaN, or infinity.");
				}

				imagePoints.push_back(point.imagePoint);
				physicalPoints.push_back(point.physicalPoint);
				usedPointCount += point.used ? 1u : 0u;
				maximumError = std::max(maximumError, point.residual);

				if (point.residual > 0.0) {
					if (residualScale < point.residual) {
						const double ratio = residualScale / point.residual;
						scaledSquareSum = 1.0 + scaledSquareSum * ratio * ratio;
						residualScale = point.residual;
					}
					else {
						const double ratio = point.residual / residualScale;
						scaledSquareSum += ratio * ratio;
					}
				}
			}
			if (containsDuplicate(imagePoints) ||
				!containsNonCollinearTriple(imagePoints)) {
				return failure(ErrorCode::InvalidResult,
					"Calibration solution points are invalid.",
					"Image points must be unique and contain a non-collinear triple.");
			}
			if (containsDuplicate(physicalPoints) ||
				!containsNonCollinearTriple(physicalPoints)) {
				return failure(ErrorCode::InvalidResult,
					"Calibration solution points are invalid.",
					"Physical points must be unique and contain a non-collinear triple.");
			}

			stage = "compute_quality";
			const double rmsError = residualScale == 0.0 ? 0.0 :
				residualScale * std::sqrt(
					scaledSquareSum / static_cast<double>(expectedPointCount));
			if (!std::isfinite(rmsError) || !std::isfinite(maximumError)) {
				return failure(ErrorCode::AlgorithmFailure,
					"Calibration quality calculation failed.",
					"The calculated quality metrics are NaN or infinity.");
			}

			CalibrationEvaluation evaluation;
			evaluation.detectedPointCount = expectedPointCount;
			evaluation.usedPointCount = usedPointCount;
			evaluation.rmsError = rmsError;
			evaluation.maximumError = maximumError;
			evaluation.passed =
				usedPointCount >= resources_->configuration.minimumUsedPoints &&
				rmsError <= resources_->configuration.maximumRmsErrorMm &&
				maximumError <= resources_->configuration.maximumResidualMm;
			return evaluation;
		}
		catch (const cv::Exception& exception) {
			return failure(ErrorCode::AlgorithmFailure,
				"Calibration quality calculation failed.", exception.what());
		}
		catch (const std::exception& exception) {
			return failure(ErrorCode::InternalFailure,
				"Calibration evaluation failed unexpectedly.", exception.what());
		}
		catch (...) {
			return failure(ErrorCode::InternalFailure,
				"Calibration evaluation failed unexpectedly.",
				"A non-standard exception escaped from calibration evaluation.");
		}
	}

} // namespace vision::algorithm
