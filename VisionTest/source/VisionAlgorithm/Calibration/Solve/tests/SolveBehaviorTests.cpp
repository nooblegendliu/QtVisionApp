#include "VisionAlgorithm/Calibration/support/CalibrationScenario.h"

namespace vision::test::algorithm::creation { namespace {

TEST_F(Algorithm_CalibrationSolveTests, Solve_AllGridSizes_RecoverKnownPerspectiveTransform)
		{
			// OpenCV's RANSAC estimate is refined numerically rather than copied from
			// the generator. 1e-6 is above the observed sub-micro-unit roundoff and
			// remains 50,000 times smaller than the configured 0.05 mm threshold.
			constexpr double numericalTolerance = 1.0e-6;
			const cv::Matx33d expected{
				0.021, 0.0007, -5.0,
				-0.0004, 0.019, 2.5,
				0.000001, -0.000002, 1.0
			};
			for (int side = 3; side <= 15; ++side) {
				SCOPED_TRACE(side);
				const int extent = (side - 1) * 80 + 240;
				const cv::Size size{ extent, extent };
				writeCalibrationAlgorithmConfiguration(directory_.path(), side);
				writeDetectionGeometry(directory_.path() / "center.yaml", size);
				auto algorithm = createAlgorithm();
				ASSERT_NE(algorithm, nullptr);
				const auto orientation = kGridOrientations[
					static_cast<std::size_t>((side - 3) % kGridOrientations.size())];
				const auto detection = makeSolveDetection(side, size);
				const auto physicalPoints = makePhysicalPoints(
					detection, side, orientation, expected);

				const auto outcome = algorithm->solve(
					detection, physicalPoints, orientation);

				const auto* solution = std::get_if<vision::algorithm::CalibrationSolution>(&outcome);
				ASSERT_NE(solution, nullptr);
				EXPECT_EQ(solution->cameraRole, CameraRole::Center);
				ASSERT_EQ(solution->points.size(), physicalPoints.size());
				for (std::size_t index = 0; index < solution->points.size(); ++index) {
					const auto& point = solution->points[index];
					EXPECT_EQ(point.pointIndex, index);
					EXPECT_EQ(point.imagePoint, detection.imagePoints[
						referenceOrientedIndex(side, index, orientation)]);
					EXPECT_EQ(point.physicalPoint, physicalPoints[index]);
					EXPECT_TRUE(point.used);
					EXPECT_LE(point.residual, numericalTolerance);
				}
				for (int row = 0; row < 3; ++row) {
					for (int column = 0; column < 3; ++column) {
						EXPECT_NEAR(solution->imageToPhysical(row, column),
							expected(row, column), numericalTolerance);
					}
				}
			}
		}

TEST_F(Algorithm_CalibrationSolveTests, Solve_EightOrientations_UseSpecifiedPhysicalOrder)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Matx33d transform{
				0.03, 0.002, -8.0,
				-0.001, 0.025, 3.0,
				0.0, 0.0, 1.0
			};
			const auto detection = makeSolveDetection(5);
			for (const auto orientation : kGridOrientations) {
				SCOPED_TRACE(static_cast<int>(orientation));
				const auto physicalPoints = makePhysicalPoints(
					detection, 5, orientation, transform);
				const auto outcome = algorithm->solve(
					detection, physicalPoints, orientation);
				const auto* solution = std::get_if<vision::algorithm::CalibrationSolution>(&outcome);
				ASSERT_NE(solution, nullptr);
				ASSERT_EQ(solution->points.size(), 25u);
				for (std::size_t index = 0; index < solution->points.size(); ++index) {
					EXPECT_EQ(solution->points[index].imagePoint,
						detection.imagePoints[referenceOrientedIndex(
							5, index, orientation)]);
				}
			}
		}

TEST_F(Algorithm_CalibrationSolveTests, Solve_FiveRoles_ValidateOwnImageSizeAndPreserveRole)
		{
			for (std::size_t index = 0; index < kDetectionRoles.size(); ++index) {
				writeDetectionGeometry(directory_.path() / kCalibrationFiles[index],
					{ 640 + static_cast<int>(index) * 20,
					  480 + static_cast<int>(index) * 10 });
			}
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Matx33d transform{
				0.02, 0.0, -4.0, 0.0, 0.02, -3.0, 0.0, 0.0, 1.0 };
			for (std::size_t index = 0; index < kDetectionRoles.size(); ++index) {
				SCOPED_TRACE(index);
				const cv::Size size{ 640 + static_cast<int>(index) * 20,
					480 + static_cast<int>(index) * 10 };
				const auto detection = makeSolveDetection(5, size, kDetectionRoles[index]);
				const auto physicalPoints = makePhysicalPoints(detection, 5,
					CalibrationGridOrientation::P1TopLeftXRight, transform);
				const auto outcome = algorithm->solve(detection, physicalPoints,
					CalibrationGridOrientation::P1TopLeftXRight);
				const auto* solution = std::get_if<vision::algorithm::CalibrationSolution>(&outcome);
				ASSERT_NE(solution, nullptr);
				EXPECT_EQ(solution->cameraRole, kDetectionRoles[index]);
			}
		}

TEST_F(Algorithm_CalibrationSolveTests, Solve_OneOrTwoOutliersReturnCandidateAndResidualsForAllPoints)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Matx33d transform{
				0.025, 0.001, -7.0, -0.001, 0.022, 4.0, 0.0, 0.0, 1.0 };
			const auto detection = makeSolveDetection(5);
			for (int outlierCount : { 1, 2 }) {
				SCOPED_TRACE(outlierCount);
				auto physicalPoints = makePhysicalPoints(detection, 5,
					CalibrationGridOrientation::P1BottomLeftXRight, transform);
				physicalPoints[0] += cv::Point2d{ 5.0, -4.0 };
				if (outlierCount == 2) {
					physicalPoints[12] += cv::Point2d{ -6.0, 3.5 };
				}

				const auto outcome = algorithm->solve(detection, physicalPoints,
					CalibrationGridOrientation::P1BottomLeftXRight);
				const auto* solution = std::get_if<vision::algorithm::CalibrationSolution>(&outcome);
				ASSERT_NE(solution, nullptr);
				ASSERT_EQ(solution->points.size(), 25u);
				std::size_t usedCount = 0;
				for (const auto& point : solution->points) {
					usedCount += point.used ? 1u : 0u;
					EXPECT_TRUE(std::isfinite(point.residual));
				}
				EXPECT_EQ(usedCount, static_cast<std::size_t>(25 - outlierCount));
				EXPECT_FALSE(solution->points[0].used);
				EXPECT_GT(solution->points[0].residual, 1.0);
				if (outlierCount == 2) {
					EXPECT_LT(usedCount, 24u);
					EXPECT_FALSE(solution->points[12].used);
					EXPECT_GT(solution->points[12].residual, 1.0);
				}
			}
		}

TEST_F(Algorithm_CalibrationSolveTests, Solve_InvalidDetectionContracts_ReturnStableErrors)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Matx33d transform{
				0.02, 0.0, -4.0, 0.0, 0.02, -3.0, 0.0, 0.0, 1.0 };
			const auto valid = makeSolveDetection(5);
			const auto physicalPoints = makePhysicalPoints(valid, 5,
				CalibrationGridOrientation::P1TopLeftXRight, transform);
			const auto orientation = CalibrationGridOrientation::P1TopLeftXRight;

			auto detection = valid;
			detection.cameraRole = CameraRole::Unknown;
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidResult);
			detection = valid;
			detection.usableForSolving = false;
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidResult);
			detection = valid;
			detection.rectifiedImage = cv::Mat{};
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidImage);
			detection = valid;
			detection.rectifiedImage = cv::Mat(480, 640, CV_8UC3, cv::Scalar::all(0));
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidImage);
			detection = valid;
			detection.rectifiedImage = cv::Mat(479, 640, CV_8UC1, cv::Scalar{ 0 });
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidImage);
			const cv::Mat padded(480, 641, CV_8UC1, cv::Scalar{ 0 });
			detection = valid;
			detection.rectifiedImage = padded(cv::Rect{ 0, 0, 640, 480 });
			ASSERT_FALSE(detection.rectifiedImage.isContinuous());
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidImage);
			detection = valid;
			detection.imagePoints.pop_back();
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidResult);
			detection = valid;
			detection.imagePoints[0] = detection.imagePoints[1];
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidResult);
			detection = valid;
			for (std::size_t index = 0; index < detection.imagePoints.size(); ++index) {
				detection.imagePoints[index] = { 20.0 + index * 10.0, 100.0 };
			}
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidResult);
			detection = valid;
			detection.imagePoints[0].x = std::numeric_limits<double>::quiet_NaN();
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidResult);
			detection = valid;
			detection.imagePoints[0].x = detection.rectifiedImage.cols;
			expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
				vision::core::ErrorCode::InvalidResult);
		}

TEST_F(Algorithm_CalibrationSolveTests, Solve_InvalidPhysicalPointsOrOrientation_ReturnInvalidConfiguration)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Matx33d transform{
				0.02, 0.0, -4.0, 0.0, 0.02, -3.0, 0.0, 0.0, 1.0 };
			const auto detection = makeSolveDetection(5);
			const auto valid = makePhysicalPoints(detection, 5,
				CalibrationGridOrientation::P1TopLeftXRight, transform);
			const auto orientation = CalibrationGridOrientation::P1TopLeftXRight;
			expectSolveError(algorithm->solve(detection, valid,
				static_cast<CalibrationGridOrientation>(999)),
				vision::core::ErrorCode::InvalidConfiguration);

			for (int mutation = 0; mutation < 6; ++mutation) {
				SCOPED_TRACE(mutation);
				auto physicalPoints = valid;
				switch (mutation) {
				case 0: physicalPoints.pop_back(); break;
				case 1: physicalPoints.push_back({ 1.0, 2.0 }); break;
				case 2: physicalPoints[0] = physicalPoints[1]; break;
				case 3: physicalPoints[0].y = std::numeric_limits<double>::infinity(); break;
				case 4: physicalPoints[0].x = std::numeric_limits<double>::quiet_NaN(); break;
				case 5:
					for (std::size_t index = 0; index < physicalPoints.size(); ++index) {
						physicalPoints[index] = { static_cast<double>(index), 2.0 };
					}
					break;
				default: break;
				}
				expectSolveError(algorithm->solve(detection, physicalPoints, orientation),
					vision::core::ErrorCode::InvalidConfiguration);
			}
		}

TEST_F(Algorithm_CalibrationSolveTests, Solve_DoesNotModifyInputsOrRetainPreviousResult)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Matx33d transform{
				0.02, 0.001, -5.0, 0.0, 0.02, -3.0, 0.0, 0.0, 1.0 };
			const auto detection = makeSolveDetection(5);
			const auto imageSnapshot = detection.rectifiedImage.clone();
			const auto pointSnapshot = detection.imagePoints;
			const auto physicalPoints = makePhysicalPoints(detection, 5,
				CalibrationGridOrientation::P1TopRightXDown, transform);
			const auto physicalSnapshot = physicalPoints;

			auto success = algorithm->solve(detection, physicalPoints,
				CalibrationGridOrientation::P1TopRightXDown);
			auto* solution = std::get_if<vision::algorithm::CalibrationSolution>(&success);
			ASSERT_NE(solution, nullptr);
			const auto savedMatrix = solution->imageToPhysical;
			const auto savedPoints = solution->points;
			auto invalid = detection;
			invalid.usableForSolving = false;
			expectSolveError(algorithm->solve(invalid, physicalPoints,
				CalibrationGridOrientation::P1TopRightXDown),
				vision::core::ErrorCode::InvalidResult);

			EXPECT_EQ(cv::norm(detection.rectifiedImage, imageSnapshot, cv::NORM_INF), 0.0);
			EXPECT_EQ(detection.imagePoints, pointSnapshot);
			EXPECT_EQ(physicalPoints, physicalSnapshot);
			EXPECT_EQ(solution->imageToPhysical, savedMatrix);
			ASSERT_EQ(solution->points.size(), savedPoints.size());
			for (std::size_t index = 0; index < savedPoints.size(); ++index) {
				EXPECT_EQ(solution->points[index].pointIndex, savedPoints[index].pointIndex);
				EXPECT_EQ(solution->points[index].imagePoint, savedPoints[index].imagePoint);
				EXPECT_EQ(solution->points[index].physicalPoint, savedPoints[index].physicalPoint);
				EXPECT_DOUBLE_EQ(solution->points[index].residual, savedPoints[index].residual);
				EXPECT_EQ(solution->points[index].used, savedPoints[index].used);
			}
		}

} }
