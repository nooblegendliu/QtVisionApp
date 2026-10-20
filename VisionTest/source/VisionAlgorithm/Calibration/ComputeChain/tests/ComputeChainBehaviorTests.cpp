#include "VisionAlgorithm/Calibration/support/CalibrationScenario.h"

namespace vision::test::algorithm::creation { namespace {

TEST_F(Algorithm_CalibrationEndToEndTests, DefaultFiveByFive_PublicStagesFormPassingChain)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto raw = drawCircles(gridCenters());
			const auto rawSnapshot = raw.clone();
			const auto orientation = CalibrationGridOrientation::P1BottomLeftXRight;

			const auto detectionOutcome = algorithm->detect(CameraRole::Center, raw);
			const auto* detection = std::get_if<CalibrationDetection>(&detectionOutcome);
			ASSERT_NE(detection, nullptr);
			ASSERT_TRUE(detection->usableForSolving);
			ASSERT_EQ(detection->imagePoints.size(), 25u);
			const auto physicalPoints = makePhysicalPoints(
				*detection, 5, orientation, kEndToEndTransform);
			const auto physicalSnapshot = physicalPoints;

			const auto solutionOutcome = algorithm->solve(
				*detection, physicalPoints, orientation);
			const auto* solution = std::get_if<CalibrationSolution>(&solutionOutcome);
			ASSERT_NE(solution, nullptr);
			ASSERT_EQ(solution->cameraRole, CameraRole::Center);
			ASSERT_EQ(solution->points.size(), 25u);
			for (const auto& point : solution->points) {
				EXPECT_TRUE(point.used);
				EXPECT_TRUE(std::isfinite(point.residual));
				EXPECT_LE(point.residual, 1.0e-6);
			}

			const auto evaluationOutcome = algorithm->evaluate(*solution);
			const auto* evaluation = std::get_if<CalibrationEvaluation>(
				&evaluationOutcome);
			ASSERT_NE(evaluation, nullptr);
			EXPECT_EQ(evaluation->detectedPointCount, 25u);
			EXPECT_EQ(evaluation->usedPointCount, 25u);
			EXPECT_LE(evaluation->rmsError, 1.0e-6);
			EXPECT_LE(evaluation->maximumError, 1.0e-6);
			EXPECT_TRUE(evaluation->passed);
			expectTransformMappingNear(solution->imageToPhysical,
				kEndToEndTransform, raw.size());
			EXPECT_EQ(cv::norm(raw, rawSnapshot, cv::NORM_INF), 0.0);
			EXPECT_EQ(physicalPoints, physicalSnapshot);
		}

TEST_F(Algorithm_CalibrationEndToEndTests, RepresentativeGridSizes_CompletePublicChain)
		{
			for (const int side : { 3, 5, 8, 15 }) {
				SCOPED_TRACE(side);
				const int extent = (side - 1) * 80 + 240;
				const cv::Size size{ extent, extent };
				writeCalibrationAlgorithmConfiguration(directory_.path(), side);
				writeDetectionGeometry(directory_.path() / "center.yaml", size);
				auto algorithm = createAlgorithm();
				ASSERT_NE(algorithm, nullptr);
				const auto raw = drawCircles(gridCenters(side, 0.0, {}, size),
					false, 18, size);
				const auto orientation = kGridOrientations[
					static_cast<std::size_t>(side) % kGridOrientations.size()];

				const auto detectionOutcome = algorithm->detect(CameraRole::Center, raw);
				const auto* detection = std::get_if<CalibrationDetection>(&detectionOutcome);
				ASSERT_NE(detection, nullptr);
				ASSERT_TRUE(detection->usableForSolving);
				const auto expectedCount = static_cast<std::size_t>(side * side);
				ASSERT_EQ(detection->imagePoints.size(), expectedCount);
				const auto physicalPoints = makePhysicalPoints(
					*detection, side, orientation, kEndToEndTransform);
				const auto solutionOutcome = algorithm->solve(
					*detection, physicalPoints, orientation);
				const auto* solution = std::get_if<CalibrationSolution>(&solutionOutcome);
				ASSERT_NE(solution, nullptr);
				ASSERT_EQ(solution->points.size(), expectedCount);
				const auto evaluationOutcome = algorithm->evaluate(*solution);
				const auto* evaluation = std::get_if<CalibrationEvaluation>(
					&evaluationOutcome);
				ASSERT_NE(evaluation, nullptr);
				EXPECT_EQ(evaluation->detectedPointCount, expectedCount);
				EXPECT_EQ(evaluation->usedPointCount, expectedCount);
				EXPECT_TRUE(evaluation->passed);
				expectTransformMappingNear(solution->imageToPhysical,
					kEndToEndTransform, size);
			}
		}

TEST_F(Algorithm_CalibrationEndToEndTests, FiveCameraRoles_KeepRoleAndGeometryThroughChain)
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
				const auto raw = drawCircles(
					gridCenters(5, 0.0, {}, sizes[index]), false, 18, sizes[index]);
				const auto orientation = kGridOrientations[index];
				const auto detectionOutcome = algorithm->detect(
					kDetectionRoles[index], raw);
				const auto* detection = std::get_if<CalibrationDetection>(
					&detectionOutcome);
				ASSERT_NE(detection, nullptr);
				ASSERT_TRUE(detection->usableForSolving);
				EXPECT_EQ(detection->cameraRole, kDetectionRoles[index]);
				const auto physicalPoints = makePhysicalPoints(
					*detection, 5, orientation, kEndToEndTransform);
				const auto solutionOutcome = algorithm->solve(
					*detection, physicalPoints, orientation);
				const auto* solution = std::get_if<CalibrationSolution>(&solutionOutcome);
				ASSERT_NE(solution, nullptr);
				EXPECT_EQ(solution->cameraRole, kDetectionRoles[index]);
				const auto evaluationOutcome = algorithm->evaluate(*solution);
				const auto* evaluation = std::get_if<CalibrationEvaluation>(
					&evaluationOutcome);
				ASSERT_NE(evaluation, nullptr);
				EXPECT_TRUE(evaluation->passed);
				expectTransformMappingNear(solution->imageToPhysical,
					kEndToEndTransform, sizes[index]);
			}
		}

TEST_F(Algorithm_CalibrationEndToEndTests, EightOrientations_MapOneDetectionAndPassQuality)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto raw = drawCircles(gridCenters(5, 17.0, { 7.0, -5.0 }));
			const auto detectionOutcome = algorithm->detect(CameraRole::Center, raw);
			const auto* detection = std::get_if<CalibrationDetection>(&detectionOutcome);
			ASSERT_NE(detection, nullptr);
			ASSERT_TRUE(detection->usableForSolving);

			for (const auto orientation : kGridOrientations) {
				SCOPED_TRACE(static_cast<int>(orientation));
				const auto physicalPoints = makePhysicalPoints(
					*detection, 5, orientation, kEndToEndTransform);
				const auto solutionOutcome = algorithm->solve(
					*detection, physicalPoints, orientation);
				const auto* solution = std::get_if<CalibrationSolution>(&solutionOutcome);
				ASSERT_NE(solution, nullptr);
				ASSERT_EQ(solution->points.size(), 25u);
				for (std::size_t pointIndex = 0;
					pointIndex < solution->points.size(); ++pointIndex) {
					EXPECT_EQ(solution->points[pointIndex].imagePoint,
						detection->imagePoints[referenceOrientedIndex(
							5, pointIndex, orientation)]);
				}
				const auto evaluationOutcome = algorithm->evaluate(*solution);
				const auto* evaluation = std::get_if<CalibrationEvaluation>(
					&evaluationOutcome);
				ASSERT_NE(evaluation, nullptr);
				EXPECT_TRUE(evaluation->passed);
				expectTransformMappingNear(solution->imageToPhysical,
					kEndToEndTransform, raw.size());
			}
		}

TEST_F(Algorithm_CalibrationEndToEndTests, RansacExclusionsDriveIntegratedQualitySemantics)
		{
			writeTextFile(directory_.path() / "calibration_parameters.yaml",
				replaceFirst(makeCalibrationAlgorithmConfiguration(),
					"ransac_threshold_mm: 0.05", "ransac_threshold_mm: 0.01"));
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto raw = drawCircles(gridCenters());
			const auto orientation = CalibrationGridOrientation::P1TopLeftXRight;
			const auto detectionOutcome = algorithm->detect(CameraRole::Center, raw);
			const auto* detection = std::get_if<CalibrationDetection>(&detectionOutcome);
			ASSERT_NE(detection, nullptr);
			ASSERT_TRUE(detection->usableForSolving);
			const auto basePhysicalPoints = makePhysicalPoints(
				*detection, 5, orientation, kEndToEndTransform);

			for (const int outlierCount : { 1, 2 }) {
				SCOPED_TRACE(outlierCount);
				auto physicalPoints = basePhysicalPoints;
				physicalPoints[0] += cv::Point2d{ 0.06, 0.0 };
				if (outlierCount == 2) {
					physicalPoints[18] += cv::Point2d{ -0.06, 0.0 };
				}
				const auto solutionOutcome = algorithm->solve(
					*detection, physicalPoints, orientation);
				const auto* solution = std::get_if<CalibrationSolution>(&solutionOutcome);
				ASSERT_NE(solution, nullptr);
				const auto evaluationOutcome = algorithm->evaluate(*solution);
				const auto* evaluation = std::get_if<CalibrationEvaluation>(
					&evaluationOutcome);
				ASSERT_NE(evaluation, nullptr);
				EXPECT_EQ(evaluation->usedPointCount,
					static_cast<std::size_t>(25 - outlierCount));
				EXPECT_LT(evaluation->rmsError, 0.03);
				EXPECT_LT(evaluation->maximumError, 0.08);
				EXPECT_EQ(evaluation->passed, outlierCount == 1);
			}

			auto severePhysicalPoints = basePhysicalPoints;
			severePhysicalPoints[0] += cv::Point2d{ 0.10, 0.0 };
			const auto severeSolutionOutcome = algorithm->solve(
				*detection, severePhysicalPoints, orientation);
			const auto* severeSolution = std::get_if<CalibrationSolution>(
				&severeSolutionOutcome);
			ASSERT_NE(severeSolution, nullptr);
			const auto severeEvaluationOutcome = algorithm->evaluate(*severeSolution);
			const auto* severeEvaluation = std::get_if<CalibrationEvaluation>(
				&severeEvaluationOutcome);
			ASSERT_NE(severeEvaluation, nullptr);
			EXPECT_EQ(severeEvaluation->usedPointCount, 24u);
			EXPECT_GT(severeEvaluation->maximumError, 0.08);
			EXPECT_FALSE(severeEvaluation->passed);
		}

TEST_F(Algorithm_CalibrationEndToEndTests, StageFailuresKeepStablePublicBoundaries)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Mat blank(480, 640, CV_8UC1, cv::Scalar{ 255 });
			const auto detectionOutcome = algorithm->detect(CameraRole::Center, blank);
			const auto* detection = std::get_if<CalibrationDetection>(&detectionOutcome);
			ASSERT_NE(detection, nullptr);
			EXPECT_FALSE(detection->usableForSolving);
			EXPECT_TRUE(detection->imagePoints.empty());
			const auto validDetection = makeSolveDetection(5);
			const auto validPhysicalPoints = makePhysicalPoints(validDetection, 5,
				CalibrationGridOrientation::P1TopLeftXRight, kEndToEndTransform);
			expectSolveError(algorithm->solve(*detection, validPhysicalPoints,
				CalibrationGridOrientation::P1TopLeftXRight),
				vision::core::ErrorCode::InvalidResult);

			const auto validRaw = drawCircles(gridCenters());
			const auto validDetectionOutcome = algorithm->detect(
				CameraRole::Center, validRaw);
			const auto* detected = std::get_if<CalibrationDetection>(
				&validDetectionOutcome);
			ASSERT_NE(detected, nullptr);
			ASSERT_TRUE(detected->usableForSolving);
			auto invalidPhysicalPoints = makePhysicalPoints(*detected, 5,
				CalibrationGridOrientation::P1TopLeftXRight, kEndToEndTransform);
			invalidPhysicalPoints.pop_back();
			expectSolveError(algorithm->solve(*detected, invalidPhysicalPoints,
				CalibrationGridOrientation::P1TopLeftXRight),
				vision::core::ErrorCode::InvalidConfiguration);
			expectSolveError(algorithm->solve(*detected, validPhysicalPoints,
				static_cast<CalibrationGridOrientation>(999)),
				vision::core::ErrorCode::InvalidConfiguration);
		}

TEST_F(Algorithm_CalibrationEndToEndTests, SuccessFailureSuccess_DoesNotLeakStateOrMutateInputs)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			auto firstRaw = drawCircles(gridCenters());
			const auto firstRawSnapshot = firstRaw.clone();
			const auto orientation = CalibrationGridOrientation::P1TopRightXDown;
			auto firstDetectionOutcome = algorithm->detect(CameraRole::Center, firstRaw);
			auto* firstDetection = std::get_if<CalibrationDetection>(
				&firstDetectionOutcome);
			ASSERT_NE(firstDetection, nullptr);
			ASSERT_TRUE(firstDetection->usableForSolving);
			const auto firstPhysicalPoints = makePhysicalPoints(
				*firstDetection, 5, orientation, kEndToEndTransform);
			const auto firstPhysicalSnapshot = firstPhysicalPoints;
			auto firstSolutionOutcome = algorithm->solve(
				*firstDetection, firstPhysicalPoints, orientation);
			auto* firstSolution = std::get_if<CalibrationSolution>(&firstSolutionOutcome);
			ASSERT_NE(firstSolution, nullptr);
			auto firstEvaluationOutcome = algorithm->evaluate(*firstSolution);
			auto* firstEvaluation = std::get_if<CalibrationEvaluation>(
				&firstEvaluationOutcome);
			ASSERT_NE(firstEvaluation, nullptr);
			ASSERT_TRUE(firstEvaluation->passed);
			const auto savedRectifiedImage = firstDetection->rectifiedImage.clone();
			const auto savedDetectionPoints = firstDetection->imagePoints;
			const auto savedMatrix = firstSolution->imageToPhysical;
			const auto savedSolutionPoints = firstSolution->points;
			const auto savedEvaluation = *firstEvaluation;

			const cv::Mat blank(480, 640, CV_8UC1, cv::Scalar{ 255 });
			const auto failedDetectionOutcome = algorithm->detect(
				CameraRole::Center, blank);
			const auto* failedDetection = std::get_if<CalibrationDetection>(
				&failedDetectionOutcome);
			ASSERT_NE(failedDetection, nullptr);
			ASSERT_FALSE(failedDetection->usableForSolving);
			auto invalidPhysicalPoints = firstPhysicalPoints;
			invalidPhysicalPoints.pop_back();
			expectSolveError(algorithm->solve(*firstDetection,
				invalidPhysicalPoints, orientation),
				vision::core::ErrorCode::InvalidConfiguration);

			const auto secondRaw = drawCircles(
				gridCenters(5, 12.0, { 8.0, -4.0 }));
			const auto secondDetectionOutcome = algorithm->detect(
				CameraRole::Center, secondRaw);
			const auto* secondDetection = std::get_if<CalibrationDetection>(
				&secondDetectionOutcome);
			ASSERT_NE(secondDetection, nullptr);
			ASSERT_TRUE(secondDetection->usableForSolving);
			const auto secondPhysicalPoints = makePhysicalPoints(
				*secondDetection, 5, orientation, kEndToEndTransform);
			const auto secondSolutionOutcome = algorithm->solve(
				*secondDetection, secondPhysicalPoints, orientation);
			const auto* secondSolution = std::get_if<CalibrationSolution>(
				&secondSolutionOutcome);
			ASSERT_NE(secondSolution, nullptr);
			const auto secondEvaluationOutcome = algorithm->evaluate(*secondSolution);
			const auto* secondEvaluation = std::get_if<CalibrationEvaluation>(
				&secondEvaluationOutcome);
			ASSERT_NE(secondEvaluation, nullptr);
			EXPECT_TRUE(secondEvaluation->passed);

			EXPECT_EQ(cv::norm(firstRaw, firstRawSnapshot, cv::NORM_INF), 0.0);
			EXPECT_EQ(firstPhysicalPoints, firstPhysicalSnapshot);
			EXPECT_EQ(cv::norm(firstDetection->rectifiedImage,
				savedRectifiedImage, cv::NORM_INF), 0.0);
			EXPECT_EQ(firstDetection->imagePoints, savedDetectionPoints);
			EXPECT_EQ(firstSolution->imageToPhysical, savedMatrix);
			ASSERT_EQ(firstSolution->points.size(), savedSolutionPoints.size());
			for (std::size_t index = 0; index < savedSolutionPoints.size(); ++index) {
				EXPECT_EQ(firstSolution->points[index].pointIndex,
					savedSolutionPoints[index].pointIndex);
				EXPECT_EQ(firstSolution->points[index].imagePoint,
					savedSolutionPoints[index].imagePoint);
				EXPECT_EQ(firstSolution->points[index].physicalPoint,
					savedSolutionPoints[index].physicalPoint);
				EXPECT_DOUBLE_EQ(firstSolution->points[index].residual,
					savedSolutionPoints[index].residual);
				EXPECT_EQ(firstSolution->points[index].used,
					savedSolutionPoints[index].used);
			}
			EXPECT_EQ(firstEvaluation->detectedPointCount,
				savedEvaluation.detectedPointCount);
			EXPECT_EQ(firstEvaluation->usedPointCount,
				savedEvaluation.usedPointCount);
			EXPECT_DOUBLE_EQ(firstEvaluation->rmsError, savedEvaluation.rmsError);
			EXPECT_DOUBLE_EQ(firstEvaluation->maximumError,
				savedEvaluation.maximumError);
			EXPECT_EQ(firstEvaluation->passed, savedEvaluation.passed);
		}

} }
