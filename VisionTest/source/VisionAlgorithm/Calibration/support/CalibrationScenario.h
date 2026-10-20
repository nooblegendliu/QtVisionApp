#include "TestSupport/TestDirectory.h"
#pragma once
#include <VisionAlgorithm/Calibration.h>

#include <gtest/gtest.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "AlgorithmConfig.h"

namespace vision::test::algorithm::creation {
	namespace {

		using CalibrationTestDirectory = vision::test::support::ScopedTestDirectory;

		constexpr std::array<const char*, 5> kCalibrationFiles{
			"center.yaml",
			"top_left.yaml",
			"top_right.yaml",
			"bottom_right.yaml",
			"bottom_left.yaml"
		};

		constexpr std::array<const char*, 5> kCalibrationRoles{
			"center",
			"top_left",
			"top_right",
			"bottom_right",
			"bottom_left"
		};

		void writeTextFile(
			const std::filesystem::path& path,
			const std::string& contents)
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			ASSERT_TRUE(output.is_open()) << path.string();
			output << contents;
			ASSERT_TRUE(output.good()) << path.string();
		}

		[[nodiscard]] std::string makeMainConfiguration()
		{
			return R"yaml(%YAML:1.0
---
timeout_ms: 500
enabled_algorithms:
  edge: 1
  mark: 0
calibration:
  parameters: "calibration_parameters.yaml"
  center: "center.yaml"
  top_left: "top_left.yaml"
  top_right: "top_right.yaml"
  bottom_right: "bottom_right.yaml"
  bottom_left: "bottom_left.yaml"
edge:
  parameters: "unused_edge_parameters.yaml"
)yaml";
		}

		[[nodiscard]] std::string makeCircleDetectionNode(const char* role)
		{
			return
				"  " + std::string(role) + ":\n"
				"    blob_color: \"dark\"\n"
				"    minimum_threshold: 20.0\n"
				"    maximum_threshold: 240.0\n"
				"    threshold_step: 10.0\n"
				"    minimum_repeatability: 2\n"
				"    minimum_distance_px: 20.0\n"
				"    minimum_area_px2: 100.0\n"
				"    maximum_area_px2: 10000.0\n"
				"    minimum_circularity: 0.75\n";
		}

		[[nodiscard]] std::string makeCalibrationAlgorithmConfiguration(int gridSidePoints = 5)
		{
			std::string contents =
				"%YAML:1.0\n"
				"---\n"
				"grid_side_points: " + std::to_string(gridSidePoints) + "\n"
				"detection:\n";
			for (const char* role : kCalibrationRoles) {
				contents += makeCircleDetectionNode(role);
			}

			contents +=
				"solver:\n"
				"  ransac_threshold_mm: 0.05\n"
				"quality:\n"
				"  minimum_used_points: " + std::to_string(gridSidePoints * gridSidePoints - 1) + "\n"
				"  maximum_rms_error_mm: 0.03\n"
				"  maximum_residual_mm: 0.08\n";
			return contents;
		}

		void writeCalibrationAlgorithmConfiguration(
			const std::filesystem::path& directory, int gridSidePoints = 5)
		{
			writeTextFile(
				directory / "calibration_parameters.yaml",
				makeCalibrationAlgorithmConfiguration(gridSidePoints));
		}

		[[nodiscard]] std::string replaceFirst(
			std::string contents,
			const std::string& original,
			const std::string& replacement)
		{
			const auto position = contents.find(original);
			if (position == std::string::npos) {
				throw std::logic_error(
					"Calibration test mutation source was not found.");
			}
			contents.replace(position, original.size(), replacement);
			return contents;
		}

		[[nodiscard]] std::string makeCalibrationConfiguration(
			bool singularImageToPhysical = false)
		{
			const std::string imageToPhysicalData = singularImageToPhysical
				? "[ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 ]"
				: "[ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 ]";

			return R"yaml(%YAML:1.0
---
image_width: 16
image_height: 12
camera_matrix: !!opencv-matrix
  rows: 3
  cols: 3
  dt: d
  data: [ 100.0, 0.0, 8.0, 0.0, 100.0, 6.0, 0.0, 0.0, 1.0 ]
distortion_coefficients: !!opencv-matrix
  rows: 1
  cols: 5
  dt: d
  data: [ 0.0, 0.0, 0.0, 0.0, 0.0 ]
new_camera_matrix: !!opencv-matrix
  rows: 3
  cols: 3
  dt: d
  data: [ 100.0, 0.0, 8.0, 0.0, 100.0, 6.0, 0.0, 0.0, 1.0 ]
image_to_physical: !!opencv-matrix
  rows: 3
  cols: 3
  dt: d
  data: )yaml" + imageToPhysicalData + '\n';
		}

		void writeCalibrationFiles(
			const std::filesystem::path& directory,
			const char* omittedFile = nullptr)
		{
			for (const char* fileName : kCalibrationFiles) {
				if (omittedFile != nullptr && std::string(fileName) == omittedFile) {
					continue;
				}
				writeTextFile(
					directory / fileName,
					makeCalibrationConfiguration());
			}
		}

		using CalibrationAlgorithm = vision::algorithm::CalibrationAlgorithm;
		using CalibrationDetection = vision::algorithm::CalibrationDetection;
		using CameraRole = vision::core::CameraRole;

		constexpr std::array<CameraRole, 5> kDetectionRoles{
			CameraRole::Center, CameraRole::TopLeft, CameraRole::TopRight,
			CameraRole::BottomRight, CameraRole::BottomLeft
		};

		cv::Mat detectionCameraMatrix(cv::Size size)
		{
			return cv::Mat(cv::Matx33d{
				500.0, 0.0, size.width / 2.0,
				0.0, 500.0, size.height / 2.0,
				0.0, 0.0, 1.0 });
		}

		void writeDetectionGeometry(const std::filesystem::path& path,
			cv::Size size = { 640, 480 }, cv::Point2d shift = {},
			const cv::Mat& distortion = cv::Mat::zeros(1, 5, CV_64F))
		{
			const auto cameraMatrix = detectionCameraMatrix(size);
			auto newCameraMatrix = cameraMatrix.clone();
			newCameraMatrix.at<double>(0, 2) += shift.x;
			newCameraMatrix.at<double>(1, 2) += shift.y;
			cv::FileStorage output(path.string(), cv::FileStorage::WRITE);
			ASSERT_TRUE(output.isOpened());
			output << "image_width" << size.width << "image_height" << size.height;
			output << "camera_matrix" << cameraMatrix;
			output << "distortion_coefficients" << distortion;
			output << "new_camera_matrix" << newCameraMatrix;
			// Deliberately not identity: detect must never apply this old mapping.
			output << "image_to_physical" << cv::Mat(cv::Matx33d{
				0.2, 0.0, 100.0, 0.0, -0.2, 50.0, 0.0, 0.0, 1.0 });
		}

		cv::Mat referenceRectify(const cv::Mat& raw, cv::Point2d shift = {},
			const cv::Mat& distortion = cv::Mat::zeros(1, 5, CV_64F))
		{
			const auto cameraMatrix = detectionCameraMatrix(raw.size());
			auto newCameraMatrix = cameraMatrix.clone();
			newCameraMatrix.at<double>(0, 2) += shift.x;
			newCameraMatrix.at<double>(1, 2) += shift.y;
			cv::Mat mapX, mapY, expected;
			cv::initUndistortRectifyMap(cameraMatrix, distortion, cv::Mat(),
				newCameraMatrix, raw.size(), CV_32FC1, mapX, mapY);
			cv::remap(raw, expected, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_CONSTANT);
			return expected;
		}

		std::vector<cv::Point2d> gridCenters(int side = 5, double degrees = 0.0,
			cv::Point2d translation = {}, cv::Size size = { 640, 480 })
		{
			const double angle = degrees * CV_PI / 180.0;
			std::vector<cv::Point2d> centers;
			for (int row = 0; row < side; ++row) {
				for (int column = 0; column < side; ++column) {
					const double x = (column - (side - 1) / 2.0) * 80.0;
					const double y = (row - (side - 1) / 2.0) * 80.0;
					centers.emplace_back(
						size.width / 2.0 + x * std::cos(angle) - y * std::sin(angle) + translation.x,
						size.height / 2.0 + x * std::sin(angle) + y * std::cos(angle) + translation.y);
				}
			}
			return centers;
		}

		cv::Mat drawCircles(const std::vector<cv::Point2d>& centers,
			bool light = false, int radius = 18, cv::Size size = { 640, 480 })
		{
			cv::Mat image(size, CV_8UC1, cv::Scalar{ light ? 0.0 : 255.0 });
			for (const auto& center : centers) {
				cv::circle(image, cv::Point{ cvRound(center.x * 256), cvRound(center.y * 256) },
					radius * 256, cv::Scalar{ light ? 255.0 : 0.0 }, cv::FILLED, cv::LINE_AA, 8);
			}
			return image;
		}

		std::vector<cv::Point2f> referenceGrid(const cv::Mat& image, int side = 5, bool light = false)
		{
			// Independent public OpenCV reference; do not call production conversion helpers.
			cv::SimpleBlobDetector::Params parameters;
			parameters.minThreshold = 20.0f;
			parameters.maxThreshold = 240.0f;
			parameters.thresholdStep = 10.0f;
			parameters.minRepeatability = 2;
			parameters.minDistBetweenBlobs = 20.0f;
			parameters.filterByColor = true;
			parameters.blobColor = light ? 255 : 0;
			parameters.filterByArea = true;
			parameters.minArea = 100.0f;
			parameters.maxArea = 10000.0f;
			parameters.filterByCircularity = true;
			parameters.minCircularity = 0.75f;
			parameters.filterByConvexity = false;
			parameters.filterByInertia = false;
			parameters.collectContours = false;
			cv::CirclesGridFinderParameters gridParameters;
			if (side == 3) {
				gridParameters.minDensity = 6;
			}
			std::vector<cv::Point2f> centers;
			const bool found = cv::findCirclesGrid(image, cv::Size{ side, side }, centers,
				cv::CALIB_CB_SYMMETRIC_GRID, cv::SimpleBlobDetector::create(parameters), gridParameters);
			EXPECT_TRUE(found);
			EXPECT_EQ(centers.size(), static_cast<std::size_t>(side * side));
			return centers;
		}

		void expectDetectionImage(const CalibrationDetection& detection,
			const cv::Mat& raw, const cv::Mat& expected, CameraRole role = CameraRole::Center)
		{
			EXPECT_EQ(detection.cameraRole, role);
			ASSERT_FALSE(detection.rectifiedImage.empty());
			EXPECT_EQ(detection.rectifiedImage.dims, 2);
			EXPECT_EQ(detection.rectifiedImage.size(), raw.size());
			EXPECT_EQ(detection.rectifiedImage.type(), CV_8UC1);
			EXPECT_TRUE(detection.rectifiedImage.isContinuous());
			EXPECT_NE(detection.rectifiedImage.data, raw.data);
			EXPECT_EQ(cv::norm(detection.rectifiedImage, expected, cv::NORM_INF), 0.0);
		}

		void expectCompleteGrid(const CalibrationDetection& detection,
			const std::vector<cv::Point2d>& expected, int side = 5)
		{
			ASSERT_TRUE(detection.usableForSolving);
			const auto expectedCount = static_cast<std::size_t>(side * side);
			ASSERT_EQ(detection.imagePoints.size(), expectedCount);
			ASSERT_EQ(expected.size(), expectedCount);
			std::vector<bool> matched(expectedCount, false);
			for (const auto& point : detection.imagePoints) {
				EXPECT_TRUE(std::isfinite(point.x) && std::isfinite(point.y));
				EXPECT_GE(point.x, 0.0);
				EXPECT_GE(point.y, 0.0);
				EXPECT_LT(point.x, detection.rectifiedImage.cols);
				EXPECT_LT(point.y, detection.rectifiedImage.rows);
				bool found = false;
				for (std::size_t index = 0; index < expected.size(); ++index) {
					// One-pixel Euclidean tolerance is a synthetic functional baseline only.
					if (!matched[index] && cv::norm(point - expected[index]) <= 1.0) {
						matched[index] = true;
						found = true;
						break;
					}
				}
				EXPECT_TRUE(found) << "Unmatched center: " << point;
			}
		}

		class Algorithm_CalibrationDetectionTests : public ::testing::Test {
		protected:
			CalibrationTestDirectory directory_{ "detection" };

			void SetUp() override
			{
				writeTextFile(directory_.path() / "vision_algorithm.yaml", makeMainConfiguration());
				writeCalibrationAlgorithmConfiguration(directory_.path());
				for (const char* file : kCalibrationFiles) {
					writeDetectionGeometry(directory_.path() / file);
				}
			}

			std::unique_ptr<CalibrationAlgorithm> createAlgorithm()
			{
				auto outcome = CalibrationAlgorithm::create(directory_.path() / "vision_algorithm.yaml");
				if (const auto* error = std::get_if<vision::core::Error>(&outcome)) {
					ADD_FAILURE() << error->message << ": " << error->diagnostic;
					return nullptr;
				}
				return std::move(std::get<std::unique_ptr<CalibrationAlgorithm>>(outcome));
			}
		};

		using CalibrationGridOrientation =
			vision::algorithm::CalibrationGridOrientation;

		constexpr std::array<CalibrationGridOrientation, 8> kGridOrientations{
			CalibrationGridOrientation::P1TopLeftXRight,
			CalibrationGridOrientation::P1TopLeftXDown,
			CalibrationGridOrientation::P1TopRightXLeft,
			CalibrationGridOrientation::P1TopRightXDown,
			CalibrationGridOrientation::P1BottomRightXLeft,
			CalibrationGridOrientation::P1BottomRightXUp,
			CalibrationGridOrientation::P1BottomLeftXRight,
			CalibrationGridOrientation::P1BottomLeftXUp
		};

		std::size_t referenceOrientedIndex(int side, std::size_t pointIndex,
			CalibrationGridOrientation orientation)
		{
			const int x = static_cast<int>(pointIndex % side);
			const int y = static_cast<int>(pointIndex / side);
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
				throw std::logic_error("Unexpected test orientation.");
			}
			return static_cast<std::size_t>(row * side + column);
		}

		cv::Point2d transformPoint(const cv::Point2d& point,
			const cv::Matx33d& transform)
		{
			const cv::Vec3d homogeneous = transform * cv::Vec3d{ point.x, point.y, 1.0 };
			return { homogeneous[0] / homogeneous[2], homogeneous[1] / homogeneous[2] };
		}

		CalibrationDetection makeSolveDetection(int side,
			cv::Size size = { 640, 480 }, CameraRole role = CameraRole::Center)
		{
			CalibrationDetection detection;
			detection.cameraRole = role;
			detection.rectifiedImage = cv::Mat(size, CV_8UC1, cv::Scalar{ 255 });
			detection.imagePoints = gridCenters(side, 0.0, {}, size);
			detection.usableForSolving = true;
			return detection;
		}

		vision::algorithm::PhysicalPointSet makePhysicalPoints(
			const CalibrationDetection& detection, int side,
			CalibrationGridOrientation orientation, const cv::Matx33d& transform)
		{
			vision::algorithm::PhysicalPointSet physicalPoints;
			physicalPoints.reserve(static_cast<std::size_t>(side * side));
			for (std::size_t pointIndex = 0;
				pointIndex < static_cast<std::size_t>(side * side); ++pointIndex) {
				physicalPoints.push_back(transformPoint(
					detection.imagePoints[referenceOrientedIndex(
						side, pointIndex, orientation)], transform));
			}
			return physicalPoints;
		}

		void expectSolveError(const vision::algorithm::CalibrationSolutionOutcome& outcome,
			vision::core::ErrorCode code)
		{
			const auto* error = std::get_if<vision::core::Error>(&outcome);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(error->code, code);
			EXPECT_FALSE(error->message.empty());
			EXPECT_FALSE(error->diagnostic.empty());
		}

		class Algorithm_CalibrationSolveTests : public Algorithm_CalibrationDetectionTests {
		};

		using CalibrationEvaluation = vision::algorithm::CalibrationEvaluation;
		using CalibrationSolution = vision::algorithm::CalibrationSolution;

		CalibrationSolution makeEvaluationSolution(int side,
			cv::Size size = { 640, 480 }, CameraRole role = CameraRole::Center)
		{
			CalibrationSolution solution;
			solution.cameraRole = role;
			solution.imageToPhysical = cv::Matx33d::eye();
			const auto imagePoints = gridCenters(side, 0.0, {}, size);
			solution.points.reserve(imagePoints.size());
			for (std::size_t index = 0; index < imagePoints.size(); ++index) {
				solution.points.push_back(vision::algorithm::CalibrationPoint{
					.pointIndex = index,
					.imagePoint = imagePoints[index],
					.physicalPoint = {
						static_cast<double>(index % static_cast<std::size_t>(side)) * 10.0,
						static_cast<double>(index / static_cast<std::size_t>(side)) * 10.0 },
					.residual = 0.0,
					.used = true
				});
			}
			return solution;
		}

		void expectEvaluationError(
			const vision::algorithm::CalibrationEvaluationOutcome& outcome,
			vision::core::ErrorCode code = vision::core::ErrorCode::InvalidResult)
		{
			const auto* error = std::get_if<vision::core::Error>(&outcome);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(error->code, code);
			EXPECT_FALSE(error->message.empty());
			EXPECT_FALSE(error->diagnostic.empty());
		}

		class Algorithm_CalibrationEvaluationTests : public Algorithm_CalibrationDetectionTests {
		};

		const cv::Matx33d kEndToEndTransform{
			0.020, 0.001, -5.0,
			-0.0005, 0.019, 3.0,
			0.000001, -0.000002, 1.0
		};

		void expectTransformMappingNear(const cv::Matx33d& actual,
			const cv::Matx33d& expected, cv::Size imageSize,
			double toleranceMm = 1.0e-6)
		{
			const std::array<cv::Point2d, 5> validationPoints{
				cv::Point2d{ imageSize.width * 0.5, imageSize.height * 0.5 },
				cv::Point2d{ 32.0, 32.0 },
				cv::Point2d{ imageSize.width - 33.0, 32.0 },
				cv::Point2d{ imageSize.width - 33.0, imageSize.height - 33.0 },
				cv::Point2d{ 32.0, imageSize.height - 33.0 }
			};
			for (const auto& imagePoint : validationPoints) {
				EXPECT_LE(cv::norm(
					transformPoint(imagePoint, actual) -
					transformPoint(imagePoint, expected)), toleranceMm);
			}
		}

		class Algorithm_CalibrationEndToEndTests : public Algorithm_CalibrationDetectionTests {
		};

	} // namespace
} // namespace vision::test::algorithm::creation
