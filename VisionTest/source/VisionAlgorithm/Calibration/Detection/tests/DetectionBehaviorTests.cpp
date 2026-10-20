#include "VisionAlgorithm/Calibration/support/CalibrationScenario.h"

namespace vision::test::algorithm::creation { namespace {

TEST_F(Algorithm_CalibrationDetectionTests, Detect_DefaultDarkGrid_Returns25PointsAndLeavesInputUnchanged)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto centers = gridCenters();
			const auto raw = drawCircles(centers);
			const auto snapshot = raw.clone();

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectDetectionImage(*detection, raw, referenceRectify(raw));
			expectCompleteGrid(*detection, centers);
			EXPECT_EQ(cv::norm(raw, snapshot, cv::NORM_INF), 0.0);
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_DefaultLightGrid_Returns25Points)
		{
			writeTextFile(directory_.path() / "calibration_parameters.yaml", replaceFirst(
				makeCalibrationAlgorithmConfiguration(), "blob_color: \"dark\"", "blob_color: \"light\""));
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto centers = gridCenters();
			const auto raw = drawCircles(centers, true);

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectDetectionImage(*detection, raw, referenceRectify(raw));
			expectCompleteGrid(*detection, centers);
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_TranslatedGrid_ReturnsCentersWithinOnePixel)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto centers = gridCenters(5, 0.0, { 17.25, -9.5 });
			const auto raw = drawCircles(centers);

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectCompleteGrid(*detection, centers);
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_RotatedGrid_KeepsNativeOpenCvOrder)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto centers = gridCenters(5, 23.0, { 5.25, -3.5 });
			const auto raw = drawCircles(centers);
			const auto expectedImage = referenceRectify(raw);
			const auto expectedOrder = referenceGrid(expectedImage);
			ASSERT_EQ(expectedOrder.size(), 25u);

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			ASSERT_NO_FATAL_FAILURE(expectCompleteGrid(*detection, centers));
			expectDetectionImage(*detection, raw, expectedImage);
			for (std::size_t index = 0; index < expectedOrder.size(); ++index) {
				EXPECT_DOUBLE_EQ(detection->imagePoints[index].x, expectedOrder[index].x);
				EXPECT_DOUBLE_EQ(detection->imagePoints[index].y, expectedOrder[index].y);
			}
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_AllSideCounts_DarkLightAndRotatedTranslatedGridsMatchReference)
		{
			for (int side = 3; side <= 15; ++side) {
				SCOPED_TRACE(side);
				// Keep circle radius/spacing fixed; expand only the synthetic canvas
				// so every rotated grid, including 15x15, fits with a clear margin.
				const int extent = (side - 1) * 120 + 240;
				const cv::Size size{ extent, extent };
				writeDetectionGeometry(directory_.path() / "center.yaml", size);
				for (bool light : { false, true }) {
					SCOPED_TRACE(light);
					auto configuration = makeCalibrationAlgorithmConfiguration(side);
					if (light) {
						configuration = replaceFirst(configuration, "blob_color: \"dark\"", "blob_color: \"light\"");
					}
					writeTextFile(directory_.path() / "calibration_parameters.yaml", configuration);
					auto algorithm = createAlgorithm();
					ASSERT_NE(algorithm, nullptr);
					for (double degrees : { 0.0, 23.0 }) {
						SCOPED_TRACE(degrees);
						const auto centers = gridCenters(side, degrees, { 5.25, -3.5 }, size);
						ASSERT_EQ(centers.size(), static_cast<std::size_t>(side * side));
						const auto raw = drawCircles(centers, light, 18, size);
						const auto snapshot = raw.clone();
						const auto expectedImage = referenceRectify(raw);
						const auto expectedOrder = referenceGrid(expectedImage, side, light);
						ASSERT_EQ(expectedOrder.size(), centers.size());

						const auto outcome = algorithm->detect(CameraRole::Center, raw);

						const auto* detection = std::get_if<CalibrationDetection>(&outcome);
						ASSERT_NE(detection, nullptr);
						ASSERT_NO_FATAL_FAILURE(expectCompleteGrid(*detection, centers, side));
						expectDetectionImage(*detection, raw, expectedImage);
						EXPECT_EQ(cv::norm(raw, snapshot, cv::NORM_INF), 0.0);
						for (std::size_t index = 0; index < expectedOrder.size(); ++index) {
							EXPECT_DOUBLE_EQ(detection->imagePoints[index].x, expectedOrder[index].x);
							EXPECT_DOUBLE_EQ(detection->imagePoints[index].y, expectedOrder[index].y);
						}
					}
				}
			}
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_SmallerCompleteGrid_DoesNotInferOrRetryAnotherSize)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto raw = drawCircles(gridCenters(3));
			// Verify the input really is a detectable 3x3, while the configuration is 5x5.
			ASSERT_EQ(referenceGrid(referenceRectify(raw), 3).size(), 9u);

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectDetectionImage(*detection, raw, referenceRectify(raw));
			EXPECT_FALSE(detection->usableForSolving);
			EXPECT_TRUE(detection->imagePoints.empty());
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_BlankImage_ReturnsIndependentImageAndNoPoints)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Mat raw(480, 640, CV_8UC1, cv::Scalar{ 255 });

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectDetectionImage(*detection, raw, referenceRectify(raw));
			EXPECT_FALSE(detection->usableForSolving);
			EXPECT_TRUE(detection->imagePoints.empty());
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_OneCircleMissing_DiscardsPartialCenters)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			auto centers = gridCenters();
			centers.pop_back();
			const auto raw = drawCircles(centers);

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectDetectionImage(*detection, raw, referenceRectify(raw));
			EXPECT_FALSE(detection->usableForSolving);
			EXPECT_TRUE(detection->imagePoints.empty());
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_25CollinearCandidates_DoesNotAcceptBlobCountAsGrid)
		{
			const cv::Size size{ 1400, 480 };
			writeDetectionGeometry(directory_.path() / "center.yaml", size);
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			std::vector<cv::Point2d> centers;
			for (int index = 0; index < 25; ++index) {
				centers.emplace_back(100.0 + 50.0 * index, 240.0);
			}
			const auto raw = drawCircles(centers, false, 12, size);

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectDetectionImage(*detection, raw, referenceRectify(raw));
			EXPECT_FALSE(detection->usableForSolving);
			EXPECT_TRUE(detection->imagePoints.empty());
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_FiveRoles_UsesEachGeometryAndBlobConfiguration)
		{
			auto configuration = makeCalibrationAlgorithmConfiguration();
			for (std::size_t index = 0; index < kDetectionRoles.size(); ++index) {
				const int roleIndex = static_cast<int>(index);
				const int radius = 12 + 6 * roleIndex;
				auto node = makeCircleDetectionNode(kCalibrationRoles[index]);
				node = replaceFirst(node, "minimum_area_px2: 100.0",
					"minimum_area_px2: " + std::to_string(0.8 * CV_PI * radius * radius));
				node = replaceFirst(node, "maximum_area_px2: 10000.0",
					"maximum_area_px2: " + std::to_string(1.2 * CV_PI * radius * radius));
				if (index % 2 != 0) {
					node = replaceFirst(node, "\"dark\"", "\"light\"");
				}
				configuration = replaceFirst(configuration, makeCircleDetectionNode(kCalibrationRoles[index]), node);
				writeDetectionGeometry(directory_.path() / kCalibrationFiles[index],
					{ 640 + 20 * roleIndex, 480 + 10 * roleIndex }, { 3.0 * index, -2.0 * index });
			}
			writeTextFile(directory_.path() / "calibration_parameters.yaml", configuration);
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);

			for (std::size_t index = 0; index < kDetectionRoles.size(); ++index) {
				SCOPED_TRACE(kCalibrationRoles[index]);
				const int roleIndex = static_cast<int>(index);
				const cv::Size size{ 640 + 20 * roleIndex, 480 + 10 * roleIndex };
				const cv::Point2d shift{ 3.0 * index, -2.0 * index };
				auto expectedCenters = gridCenters();
				for (auto& center : expectedCenters) {
					center += shift;
				}
				for (int radiusIndex = 0; radiusIndex < 5; ++radiusIndex) {
					SCOPED_TRACE(radiusIndex);
					const auto raw = drawCircles(gridCenters(), index % 2 != 0, 12 + 6 * radiusIndex, size);
					const auto snapshot = raw.clone();
					const auto outcome = algorithm->detect(kDetectionRoles[index], raw);
					const auto* detection = std::get_if<CalibrationDetection>(&outcome);
					ASSERT_NE(detection, nullptr);
					expectDetectionImage(*detection, raw, referenceRectify(raw, shift), kDetectionRoles[index]);
					EXPECT_EQ(cv::norm(raw, snapshot, cv::NORM_INF), 0.0);
					if (radiusIndex == roleIndex) {
						expectCompleteGrid(*detection, expectedCenters);
					}
					else {
						EXPECT_FALSE(detection->usableForSolving);
						EXPECT_TRUE(detection->imagePoints.empty());
					}
				}
			}
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_NonzeroDistortion_MatchesIndependentOpenCvRectification)
		{
			const cv::Mat distortion = (cv::Mat_<double>(1, 5) << 0.08, -0.015, 0.001, -0.002, 0.0);
			const cv::Point2d shift{ 7.5, -4.25 };
			writeDetectionGeometry(directory_.path() / "center.yaml", { 640, 480 }, shift, distortion);
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const auto raw = drawCircles(gridCenters());
			const auto snapshot = raw.clone();
			const auto expectedImage = referenceRectify(raw, shift, distortion);
			ASSERT_GT(cv::norm(raw, expectedImage, cv::NORM_INF), 0.0);
			const auto expectedOrder = referenceGrid(expectedImage);
			ASSERT_EQ(expectedOrder.size(), 25u);

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectDetectionImage(*detection, raw, expectedImage);
			ASSERT_TRUE(detection->usableForSolving);
			ASSERT_EQ(detection->imagePoints.size(), expectedOrder.size());
			for (std::size_t index = 0; index < expectedOrder.size(); ++index) {
				EXPECT_DOUBLE_EQ(detection->imagePoints[index].x, expectedOrder[index].x);
				EXPECT_DOUBLE_EQ(detection->imagePoints[index].y, expectedOrder[index].y);
			}
			EXPECT_EQ(cv::norm(raw, snapshot, cv::NORM_INF), 0.0);
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_InvalidImageContracts_ReturnsInvalidImage)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const int sizes[]{ 2, 480, 640 };
			const cv::Mat padded(480, 641, CV_8UC1, cv::Scalar{ 0 });
			const std::vector<cv::Mat> invalidImages{
				cv::Mat{}, cv::Mat(480, 640, CV_8UC3, cv::Scalar::all(0)),
				cv::Mat(480, 640, CV_16UC1, cv::Scalar{ 0 }),
				cv::Mat(3, sizes, CV_8UC1, cv::Scalar{ 0 }),
				padded(cv::Rect{ 0, 0, 640, 480 }),
				cv::Mat(479, 640, CV_8UC1, cv::Scalar{ 0 }),
				cv::Mat(480, 639, CV_8UC1, cv::Scalar{ 0 })
			};
			ASSERT_FALSE(invalidImages[4].isContinuous());
			for (std::size_t index = 0; index < invalidImages.size(); ++index) {
				SCOPED_TRACE(index);
				const auto outcome = algorithm->detect(CameraRole::Center, invalidImages[index]);
				const auto* error = std::get_if<vision::core::Error>(&outcome);
				ASSERT_NE(error, nullptr);
				EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidImage);
				EXPECT_FALSE(error->message.empty());
				EXPECT_FALSE(error->diagnostic.empty());
			}
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_UnknownOrInvalidRole_ReturnsInvalidConfigurationBeforeImageValidation)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			for (const auto role : { CameraRole::Unknown, static_cast<CameraRole>(999) }) {
				const auto outcome = algorithm->detect(role, cv::Mat{});
				const auto* error = std::get_if<vision::core::Error>(&outcome);
				ASSERT_NE(error, nullptr);
				EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
				EXPECT_FALSE(error->message.empty());
				EXPECT_FALSE(error->diagnostic.empty());
			}
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_LaterCallsAndInputRelease_DoNotChangePreviousResults)
		{
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			auto raw = drawCircles(gridCenters());
			auto firstOutcome = algorithm->detect(CameraRole::Center, raw);
			auto* first = std::get_if<CalibrationDetection>(&firstOutcome);
			ASSERT_NE(first, nullptr);
			ASSERT_TRUE(first->usableForSolving);
			const auto firstImage = first->rectifiedImage.clone();
			const auto firstPoints = first->imagePoints;
			raw.setTo(255);
			raw.release();
			EXPECT_EQ(cv::norm(first->rectifiedImage, firstImage, cv::NORM_INF), 0.0);

			auto secondRaw = drawCircles(gridCenters(5, 12.0, { 10.0, 5.0 }));
			auto secondOutcome = algorithm->detect(CameraRole::Center, secondRaw);
			auto* second = std::get_if<CalibrationDetection>(&secondOutcome);
			ASSERT_NE(second, nullptr);
			ASSERT_TRUE(second->usableForSolving);
			EXPECT_NE(first->rectifiedImage.data, second->rectifiedImage.data);
			second->rectifiedImage.setTo(123);

			cv::Mat blank(480, 640, CV_8UC1, cv::Scalar{ 255 });
			auto missingOutcome = algorithm->detect(CameraRole::Center, blank);
			const auto* missing = std::get_if<CalibrationDetection>(&missingOutcome);
			ASSERT_NE(missing, nullptr);
			EXPECT_FALSE(missing->usableForSolving);
			EXPECT_TRUE(missing->imagePoints.empty());
			const auto missingImage = missing->rectifiedImage.clone();
			blank.setTo(0);
			blank.release();
			algorithm.reset();
			EXPECT_EQ(cv::norm(first->rectifiedImage, firstImage, cv::NORM_INF), 0.0);
			EXPECT_EQ(first->imagePoints, firstPoints);
			EXPECT_EQ(cv::norm(missing->rectifiedImage, missingImage, cv::NORM_INF), 0.0);
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_OpenCvRectificationException_ReturnsAlgorithmFailure)
		{
			// OpenCV remap documents a dimension limit below 32767 pixels. This
			// small allocation exercises a real cv::Exception without a test hook.
			const cv::Size size{ 32767, 8 };
			writeDetectionGeometry(directory_.path() / "center.yaml", size);
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Mat raw(size, CV_8UC1, cv::Scalar{ 255 });
			const auto snapshot = raw.clone();

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* error = std::get_if<vision::core::Error>(&outcome);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(error->code, vision::core::ErrorCode::AlgorithmFailure);
			EXPECT_FALSE(error->message.empty());
			EXPECT_FALSE(error->diagnostic.empty());
			EXPECT_EQ(cv::norm(raw, snapshot, cv::NORM_INF), 0.0);
		}

TEST_F(Algorithm_CalibrationDetectionTests, Detect_MinimumCircularityEqualsOne_KeepsValidOpenCvDefaults)
		{
			writeTextFile(directory_.path() / "calibration_parameters.yaml", replaceFirst(
				makeCalibrationAlgorithmConfiguration(), "minimum_circularity: 0.75", "minimum_circularity: 1.0"));
			auto algorithm = createAlgorithm();
			ASSERT_NE(algorithm, nullptr);
			const cv::Mat raw(480, 640, CV_8UC1, cv::Scalar{ 255 });

			const auto outcome = algorithm->detect(CameraRole::Center, raw);

			const auto* detection = std::get_if<CalibrationDetection>(&outcome);
			ASSERT_NE(detection, nullptr);
			expectDetectionImage(*detection, raw, referenceRectify(raw));
			EXPECT_FALSE(detection->usableForSolving);
			EXPECT_TRUE(detection->imagePoints.empty());
		}

} }
