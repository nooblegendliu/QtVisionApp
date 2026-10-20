#include "VisionAlgorithm/Calibration/support/CalibrationScenario.h"

namespace vision::test::algorithm::creation { namespace {

TEST_F(Algorithm_CalibrationEvaluationTests, Evaluate_AllGridSizes_CountAllPointsAndPass)
		{
			for (int side = 3; side <= 15; ++side) {
				SCOPED_TRACE(side);
				const int extent = (side - 1) * 80 + 240;
				const cv::Size size{ extent, extent };
				writeCalibrationAlgorithmConfiguration(directory_.path(), side);
				writeDetectionGeometry(directory_.path() / "center.yaml", size);
				auto algorithm = createAlgorithm();
				ASSERT_NE(algorithm, nullptr);
				const auto solution = makeEvaluationSolution(side, size);

				const auto outcome = algorithm->evaluate(solution);

				const auto* evaluation = std::get_if<CalibrationEvaluation>(&outcome);
				ASSERT_NE(evaluation, nullptr);
				const auto expectedCount = static_cast<std::size_t>(side * side);
				EXPECT_EQ(evaluation->detectedPointCount, expectedCount);
				EXPECT_EQ(evaluation->usedPointCount, expectedCount);
				EXPECT_DOUBLE_EQ(evaluation->rmsError, 0.0);
				EXPECT_DOUBLE_EQ(evaluation->maximumError, 0.0);
				EXPECT_TRUE(evaluation->passed);
			}
		}

TEST_F(Algorithm_CalibrationEvaluationTests, Evaluate_FiveRolesUseTheirOwnImageBounds)
		{
			std::array<cv::Size, 5> sizes;
			for (std::size_t index = 0; index < kDetectionRoles.size(); ++index) {
				sizes[index] = { 640 + static_cast<int>(index) * 20,
					480 + static_cast<int>(index) * 10 };
				writeDetectionGeometry(directory_.path() / kCalibrationFiles[index],
					sizes[index]);
			}
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);

			for (std::size_t index = 0; index < kDetectionRoles.size(); ++index) {
				SCOPED_TRACE(index);
				const auto solution = makeEvaluationSolution(
					5, sizes[index], kDetectionRoles[index]);
				const auto outcome = algorithm->evaluate(solution);
				const auto* evaluation = std::get_if<CalibrationEvaluation>(&outcome);
				ASSERT_NE(evaluation, nullptr);
				EXPECT_TRUE(evaluation->passed);

				auto outsideOwnGeometry = solution;
				outsideOwnGeometry.points[0].imagePoint.x = sizes[index].width;
				expectEvaluationError(algorithm->evaluate(outsideOwnGeometry));
			}
		}

TEST_F(Algorithm_CalibrationEvaluationTests, Evaluate_UsesStoredResidualsForAllPoints)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			auto solution = makeEvaluationSolution(5);
			solution.points[0].residual = 0.01;
			solution.points[1].residual = 0.02;
			solution.points[2].residual = 0.04;
			solution.points[2].used = false;
			const double expectedRms = std::sqrt(
				(0.01 * 0.01 + 0.02 * 0.02 + 0.04 * 0.04) / 25.0);

			const auto outcome = algorithm->evaluate(solution);

			const auto* evaluation = std::get_if<CalibrationEvaluation>(&outcome);
			ASSERT_NE(evaluation, nullptr);
			EXPECT_EQ(evaluation->detectedPointCount, 25u);
			EXPECT_EQ(evaluation->usedPointCount, 24u);
			EXPECT_NEAR(evaluation->rmsError, expectedRms, 1.0e-15);
			EXPECT_DOUBLE_EQ(evaluation->maximumError, 0.04);
			EXPECT_TRUE(evaluation->passed);

			solution.points[2].residual = 0.09;
			const auto failedOutcome = algorithm->evaluate(solution);
			const auto* failed = std::get_if<CalibrationEvaluation>(&failedOutcome);
			ASSERT_NE(failed, nullptr);
			EXPECT_EQ(failed->usedPointCount, 24u);
			EXPECT_DOUBLE_EQ(failed->maximumError, 0.09);
			EXPECT_FALSE(failed->passed);
		}

TEST_F(Algorithm_CalibrationEvaluationTests, Evaluate_QualityThresholdsUseInclusiveBoundaries)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			auto solution = makeEvaluationSolution(5);
			solution.points[0].used = false;
			for (auto& point : solution.points) {
				point.residual = 0.03;
			}

			auto outcome = algorithm->evaluate(solution);
			const auto* evaluation = std::get_if<CalibrationEvaluation>(&outcome);
			ASSERT_NE(evaluation, nullptr);
			EXPECT_EQ(evaluation->usedPointCount, 24u);
			EXPECT_LE(evaluation->rmsError, 0.03);
			EXPECT_TRUE(evaluation->passed);

			solution.points[1].used = false;
			outcome = algorithm->evaluate(solution);
			evaluation = std::get_if<CalibrationEvaluation>(&outcome);
			ASSERT_NE(evaluation, nullptr);
			EXPECT_EQ(evaluation->usedPointCount, 23u);
			EXPECT_FALSE(evaluation->passed);

			solution = makeEvaluationSolution(5);
			solution.points[0].used = false;
			solution.points[0].residual = 0.08;
			outcome = algorithm->evaluate(solution);
			evaluation = std::get_if<CalibrationEvaluation>(&outcome);
			ASSERT_NE(evaluation, nullptr);
			EXPECT_DOUBLE_EQ(evaluation->maximumError, 0.08);
			EXPECT_TRUE(evaluation->passed);

			solution.points[0].residual = std::nextafter(
				0.08, std::numeric_limits<double>::infinity());
			outcome = algorithm->evaluate(solution);
			evaluation = std::get_if<CalibrationEvaluation>(&outcome);
			ASSERT_NE(evaluation, nullptr);
			EXPECT_FALSE(evaluation->passed);

			solution = makeEvaluationSolution(5);
			solution.points[0].used = false;
			for (auto& point : solution.points) {
				point.residual = std::nextafter(
					0.03, std::numeric_limits<double>::infinity());
			}
			outcome = algorithm->evaluate(solution);
			evaluation = std::get_if<CalibrationEvaluation>(&outcome);
			ASSERT_NE(evaluation, nullptr);
			EXPECT_GT(evaluation->rmsError, 0.03);
			EXPECT_FALSE(evaluation->passed);
		}

TEST_F(Algorithm_CalibrationEvaluationTests, Evaluate_LargeFiniteResidualDoesNotOverflowRms)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			auto solution = makeEvaluationSolution(5);
			solution.points[0].residual = std::numeric_limits<double>::max();

			const auto outcome = algorithm->evaluate(solution);

			const auto* evaluation = std::get_if<CalibrationEvaluation>(&outcome);
			ASSERT_NE(evaluation, nullptr);
			EXPECT_TRUE(std::isfinite(evaluation->rmsError));
			EXPECT_DOUBLE_EQ(
				evaluation->maximumError, std::numeric_limits<double>::max());
			EXPECT_FALSE(evaluation->passed);
		}

TEST_F(Algorithm_CalibrationEvaluationTests, Evaluate_InvalidRoleMatrixCountOrIndexReturnsInvalidResult)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto valid = makeEvaluationSolution(5);

			auto solution = valid;
			solution.cameraRole = CameraRole::Unknown;
			expectEvaluationError(algorithm->evaluate(solution));
			solution = valid;
			solution.cameraRole = static_cast<CameraRole>(999);
			expectEvaluationError(algorithm->evaluate(solution));
			solution = valid;
			solution.points.pop_back();
			expectEvaluationError(algorithm->evaluate(solution));
			solution = valid;
			solution.points[3].pointIndex = 2;
			expectEvaluationError(algorithm->evaluate(solution));
			solution = valid;
			solution.imageToPhysical = cv::Matx33d::zeros();
			expectEvaluationError(algorithm->evaluate(solution));
			solution = valid;
			solution.imageToPhysical(0, 0) =
				std::numeric_limits<double>::quiet_NaN();
			expectEvaluationError(algorithm->evaluate(solution));
			solution = valid;
			solution.imageToPhysical(2, 2) =
				std::numeric_limits<double>::infinity();
			expectEvaluationError(algorithm->evaluate(solution));
		}

TEST_F(Algorithm_CalibrationEvaluationTests, Evaluate_InvalidCoordinatesOrResidualsReturnInvalidResult)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto valid = makeEvaluationSolution(5);
			for (int mutation = 0; mutation < 13; ++mutation) {
				SCOPED_TRACE(mutation);
				auto solution = valid;
				switch (mutation) {
				case 0: solution.points[0].imagePoint = solution.points[1].imagePoint; break;
				case 1:
					for (std::size_t index = 0; index < solution.points.size(); ++index) {
						solution.points[index].imagePoint =
							{ 20.0 + static_cast<double>(index), 100.0 };
					}
					break;
				case 2: solution.points[0].imagePoint.x = -1.0; break;
				case 3: solution.points[0].imagePoint.y = 480.0; break;
				case 4: solution.points[0].imagePoint.x =
					std::numeric_limits<double>::quiet_NaN(); break;
				case 5: solution.points[0].imagePoint.y =
					std::numeric_limits<double>::infinity(); break;
				case 6: solution.points[0].physicalPoint =
					solution.points[1].physicalPoint; break;
				case 7:
					for (std::size_t index = 0; index < solution.points.size(); ++index) {
						solution.points[index].physicalPoint =
							{ static_cast<double>(index), 2.0 };
					}
					break;
				case 8: solution.points[0].physicalPoint.x =
					std::numeric_limits<double>::quiet_NaN(); break;
				case 9: solution.points[0].physicalPoint.y =
					std::numeric_limits<double>::infinity(); break;
				case 10: solution.points[0].residual = -0.01; break;
				case 11: solution.points[0].residual =
					std::numeric_limits<double>::quiet_NaN(); break;
				case 12: solution.points[0].residual =
					std::numeric_limits<double>::infinity(); break;
				default: break;
				}
				expectEvaluationError(algorithm->evaluate(solution));
			}
		}

TEST_F(Algorithm_CalibrationEvaluationTests, Evaluate_DoesNotModifyInputOrRetainPreviousResult)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto solution = makeEvaluationSolution(5);
			const auto matrixSnapshot = solution.imageToPhysical;
			const auto pointsSnapshot = solution.points;

			auto success = algorithm->evaluate(solution);
			auto* evaluation = std::get_if<CalibrationEvaluation>(&success);
			ASSERT_NE(evaluation, nullptr);
			const auto savedEvaluation = *evaluation;
			auto invalid = solution;
			invalid.points.pop_back();
			expectEvaluationError(algorithm->evaluate(invalid));

			EXPECT_EQ(solution.imageToPhysical, matrixSnapshot);
			ASSERT_EQ(solution.points.size(), pointsSnapshot.size());
			for (std::size_t index = 0; index < pointsSnapshot.size(); ++index) {
				EXPECT_EQ(solution.points[index].pointIndex,
					pointsSnapshot[index].pointIndex);
				EXPECT_EQ(solution.points[index].imagePoint,
					pointsSnapshot[index].imagePoint);
				EXPECT_EQ(solution.points[index].physicalPoint,
					pointsSnapshot[index].physicalPoint);
				EXPECT_DOUBLE_EQ(solution.points[index].residual,
					pointsSnapshot[index].residual);
				EXPECT_EQ(solution.points[index].used, pointsSnapshot[index].used);
			}
			EXPECT_EQ(evaluation->detectedPointCount,
				savedEvaluation.detectedPointCount);
			EXPECT_EQ(evaluation->usedPointCount, savedEvaluation.usedPointCount);
			EXPECT_DOUBLE_EQ(evaluation->rmsError, savedEvaluation.rmsError);
			EXPECT_DOUBLE_EQ(evaluation->maximumError,
				savedEvaluation.maximumError);
			EXPECT_EQ(evaluation->passed, savedEvaluation.passed);
		}

} }
