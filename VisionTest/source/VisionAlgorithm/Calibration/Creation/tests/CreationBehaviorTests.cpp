#include "VisionAlgorithm/Calibration/support/CalibrationScenario.h"

namespace vision::test::algorithm::creation { namespace {

TEST(Algorithm_CalibrationFrameworkTests, Create_AllFiveCalibrationFilesAreValid_ReturnsInitializedObject)
		{
			CalibrationTestDirectory directory("valid_resources");
			writeTextFile(
				directory.path() / "vision_algorithm.yaml",
				makeMainConfiguration());
			writeCalibrationAlgorithmConfiguration(directory.path());
			writeCalibrationFiles(directory.path());

			auto outcome = vision::algorithm::CalibrationAlgorithm::create(
				directory.path() / "vision_algorithm.yaml");

			auto* algorithm = std::get_if<
				std::unique_ptr<vision::algorithm::CalibrationAlgorithm>>(&outcome);
			ASSERT_NE(algorithm, nullptr);
			EXPECT_NE(algorithm->get(), nullptr);
		}

TEST(Algorithm_CalibrationFrameworkTests, LoadParameters_ValidConfiguration_ReturnsTypedValues)
		{
			CalibrationTestDirectory directory("valid_parameters");
			writeTextFile(
				directory.path() / "calibration_parameters.yaml",
				replaceFirst(
					makeCalibrationAlgorithmConfiguration(),
					"blob_color: \"dark\"",
					"blob_color: \"light\""));

			const auto configuration =
				vision::algorithm::detail::loadCalibrationAlgorithmConfig(
					directory.path() / "calibration_parameters.yaml");

			EXPECT_EQ(
				configuration.center.blobColor,
				vision::algorithm::detail::CalibrationBlobColor::Light);
			EXPECT_FLOAT_EQ(configuration.center.minimumThreshold, 20.0F);
			EXPECT_FLOAT_EQ(configuration.topLeft.maximumThreshold, 240.0F);
			EXPECT_FLOAT_EQ(configuration.topRight.thresholdStep, 10.0F);
			EXPECT_EQ(configuration.bottomRight.minimumRepeatability, 2U);
			EXPECT_FLOAT_EQ(configuration.bottomLeft.minimumDistancePx, 20.0F);
			EXPECT_FLOAT_EQ(configuration.center.minimumAreaPx2, 100.0F);
			EXPECT_FLOAT_EQ(configuration.center.maximumAreaPx2, 10000.0F);
			EXPECT_FLOAT_EQ(configuration.center.minimumCircularity, 0.75F);
			EXPECT_DOUBLE_EQ(configuration.ransacThresholdMm, 0.05);
			EXPECT_EQ(configuration.gridSidePoints, 5);
			EXPECT_EQ(configuration.minimumUsedPoints, 24U);
			EXPECT_DOUBLE_EQ(configuration.maximumRmsErrorMm, 0.03);
			EXPECT_DOUBLE_EQ(configuration.maximumResidualMm, 0.08);
		}

TEST(Algorithm_CalibrationFrameworkTests, LoadParameters_InclusiveBoundariesAreAccepted)
		{
			CalibrationTestDirectory directory("parameter_boundaries");
			const auto parameterPath =
				directory.path() / "calibration_parameters.yaml";
			std::string contents = makeCalibrationAlgorithmConfiguration();
			contents = replaceFirst(
				std::move(contents),
				"minimum_threshold: 20.0",
				"minimum_threshold: 0.0");
			contents = replaceFirst(
				std::move(contents),
				"maximum_threshold: 240.0",
				"maximum_threshold: 255.0");
			contents = replaceFirst(
				std::move(contents),
				"minimum_circularity: 0.75",
				"minimum_circularity: 1.0");
			contents = replaceFirst(
				std::move(contents),
				"minimum_used_points: 24",
				"minimum_used_points: 25");
			contents = replaceFirst(
				std::move(contents),
				"maximum_residual_mm: 0.08",
				"maximum_residual_mm: 0.03");
			writeTextFile(parameterPath, contents);

			const auto configuration =
				vision::algorithm::detail::loadCalibrationAlgorithmConfig(
					parameterPath);

			EXPECT_FLOAT_EQ(configuration.center.minimumThreshold, 0.0F);
			EXPECT_FLOAT_EQ(configuration.center.maximumThreshold, 255.0F);
			EXPECT_FLOAT_EQ(configuration.center.minimumCircularity, 1.0F);
			EXPECT_EQ(configuration.minimumUsedPoints, 25U);
			EXPECT_DOUBLE_EQ(configuration.maximumResidualMm, 0.03);
		}

TEST(Algorithm_CalibrationFrameworkTests, LoadParameters_InvalidStructureOrValue_ThrowsInvalidArgument)
		{
			CalibrationTestDirectory directory("invalid_parameters");
			const auto parameterPath =
				directory.path() / "calibration_parameters.yaml";
			const std::string valid = makeCalibrationAlgorithmConfiguration();

			const std::vector<std::pair<std::string, std::string>> mutations{
				{ "detection:\n", "missing_detection:\n" },
				{ "  bottom_left:\n", "  missing_bottom_left:\n" },
				{ "blob_color: \"dark\"", "blob_color: \"blue\"" },
				{ "minimum_threshold: 20.0", "minimum_threshold: \"20.0\"" },
				{ "minimum_threshold: 20.0", "minimum_threshold: .Nan" },
				{ "minimum_threshold: 20.0", "minimum_threshold: -1.0" },
				{ "maximum_threshold: 240.0", "maximum_threshold: 20.0" },
				{ "maximum_threshold: 240.0", "maximum_threshold: 256.0" },
				{ "threshold_step: 10.0", "threshold_step: 0.0" },
				{ "threshold_step: 10.0", "threshold_step: 300.0" },
				{ "minimum_repeatability: 2", "minimum_repeatability: 0" },
				{ "minimum_repeatability: 2", "minimum_repeatability: 30" },
				{ "minimum_distance_px: 20.0", "minimum_distance_px: 0.0" },
				{ "minimum_area_px2: 100.0", "minimum_area_px2: 0.0" },
				{ "maximum_area_px2: 10000.0", "maximum_area_px2: 100.0" },
				{ "minimum_circularity: 0.75", "minimum_circularity: 1.1" },
				{ "solver:\n", "missing_solver:\n" },
				{ "ransac_threshold_mm: 0.05", "ransac_threshold_mm: 0.0" },
				{ "ransac_threshold_mm: 0.05", "ransac_threshold_mm: .Inf" },
				{ "quality:\n", "missing_quality:\n" },
				{ "minimum_used_points: 24", "minimum_used_points: 23" },
				{ "maximum_rms_error_mm: 0.03", "maximum_rms_error_mm: 0.0" },
				{ "maximum_residual_mm: 0.08", "maximum_residual_mm: 0.02" }
			};

			for (const auto& [original, replacement] : mutations) {
				SCOPED_TRACE(original + " -> " + replacement);
				writeTextFile(
					parameterPath,
					replaceFirst(valid, original, replacement));
				EXPECT_THROW(
					(void)vision::algorithm::detail::loadCalibrationAlgorithmConfig(
						parameterPath),
					std::invalid_argument);
			}
		}

TEST(Algorithm_CalibrationFrameworkTests, InternalDefaults_UseFiveByFiveAnd24UsedPoints)
		{
			const vision::algorithm::detail::CalibrationAlgorithmConfig configuration;
			EXPECT_EQ(configuration.gridSidePoints, 5);
			EXPECT_EQ(configuration.minimumUsedPoints, 24u);
		}

TEST(Algorithm_CalibrationFrameworkTests, Create_GridSizeMissingOrInvalid_ReturnsInvalidConfiguration)
		{
			CalibrationTestDirectory directory("invalid_grid_size");
			const auto mainPath = directory.path() / "vision_algorithm.yaml";
			writeTextFile(mainPath, makeMainConfiguration());
			writeCalibrationFiles(directory.path());
			const auto valid = makeCalibrationAlgorithmConfiguration();
			const std::vector<std::string> replacements{
				"", "grid_side_points: 5.0\n", "grid_side_points: \"5\"\n",
				"grid_side_points: .Nan\n", "grid_side_points: .Inf\n",
				"grid_side_points: [5]\n", "grid_side_points: { side: 5 }\n",
				"grid_side_points: -1\n", "grid_side_points: 0\n",
				"grid_side_points: 2\n", "grid_side_points: 16\n",
				"grid_side_points: 2147483647\n"
			};
			for (const auto& replacement : replacements) {
				SCOPED_TRACE(replacement.empty() ? "missing grid_side_points" : replacement);
				writeTextFile(directory.path() / "calibration_parameters.yaml",
					replaceFirst(valid, "grid_side_points: 5\n", replacement));
				const auto outcome = vision::algorithm::CalibrationAlgorithm::create(mainPath);
				const auto* error = std::get_if<vision::core::Error>(&outcome);
				ASSERT_NE(error, nullptr);
				EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
				EXPECT_FALSE(error->message.empty());
				EXPECT_FALSE(error->diagnostic.empty());
			}
		}

TEST(Algorithm_CalibrationFrameworkTests, Create_EveryGridSize_ValidatesRequiredUsedPointBoundaries)
		{
			CalibrationTestDirectory directory("grid_point_boundaries");
			const auto mainPath = directory.path() / "vision_algorithm.yaml";
			const auto parameterPath = directory.path() / "calibration_parameters.yaml";
			writeTextFile(mainPath, makeMainConfiguration());
			writeCalibrationFiles(directory.path());
			for (int side = 3; side <= 15; ++side) {
				SCOPED_TRACE(side);
				const int count = side * side;
				const auto valid = makeCalibrationAlgorithmConfiguration(side);
				const auto original = "  minimum_used_points: " + std::to_string(count - 1) + "\n";
				for (int used : { count - 2, count - 1, count, count + 1 }) {
					SCOPED_TRACE(used);
					writeTextFile(parameterPath, replaceFirst(valid, original,
						"  minimum_used_points: " + std::to_string(used) + "\n"));
					auto outcome = vision::algorithm::CalibrationAlgorithm::create(mainPath);
					if (used == count - 1 || used == count) {
						const auto* algorithm = std::get_if<
							std::unique_ptr<vision::algorithm::CalibrationAlgorithm>>(&outcome);
						ASSERT_NE(algorithm, nullptr);
						EXPECT_NE(algorithm->get(), nullptr);
						const auto parsed = vision::algorithm::detail::loadCalibrationAlgorithmConfig(parameterPath);
						EXPECT_EQ(parsed.gridSidePoints, side);
						EXPECT_EQ(parsed.minimumUsedPoints, static_cast<std::size_t>(used));
					}
					else {
						const auto* error = std::get_if<vision::core::Error>(&outcome);
						ASSERT_NE(error, nullptr);
						EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
						EXPECT_FALSE(error->diagnostic.empty());
					}
				}
				for (const auto& replacement : { std::string{},
					"  minimum_used_points: " + std::to_string(count - 1) + ".0\n",
					"  minimum_used_points: \"" + std::to_string(count - 1) + "\"\n" }) {
					SCOPED_TRACE(replacement.empty() ? "missing minimum_used_points" : replacement);
					writeTextFile(parameterPath, replaceFirst(valid, original, replacement));
					const auto outcome = vision::algorithm::CalibrationAlgorithm::create(mainPath);
					const auto* error = std::get_if<vision::core::Error>(&outcome);
					ASSERT_NE(error, nullptr);
					EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
				}
			}
		}

TEST(Algorithm_CalibrationFrameworkTests, Create_GridSizeChangedWithoutQualityMigration_ReturnsInvalidConfiguration)
		{
			CalibrationTestDirectory directory("grid_quality_migration");
			const auto mainPath = directory.path() / "vision_algorithm.yaml";
			writeTextFile(mainPath, makeMainConfiguration());
			writeCalibrationFiles(directory.path());
			for (int side : { 3, 7, 15 }) {
				SCOPED_TRACE(side);
				writeTextFile(directory.path() / "calibration_parameters.yaml", replaceFirst(
					makeCalibrationAlgorithmConfiguration(), "grid_side_points: 5",
					"grid_side_points: " + std::to_string(side)));
				const auto outcome = vision::algorithm::CalibrationAlgorithm::create(mainPath);
				const auto* error = std::get_if<vision::core::Error>(&outcome);
				ASSERT_NE(error, nullptr);
				EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
			}
		}

TEST(Algorithm_CalibrationFrameworkTests, Create_ParameterFileIsMissing_ReturnsInvalidConfiguration)
		{
			CalibrationTestDirectory directory("missing_parameter_file");
			writeTextFile(
				directory.path() / "vision_algorithm.yaml",
				makeMainConfiguration());
			writeCalibrationFiles(directory.path());

			const auto outcome = vision::algorithm::CalibrationAlgorithm::create(
				directory.path() / "vision_algorithm.yaml");

			const auto* error = std::get_if<vision::core::Error>(&outcome);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(
				error->code,
				vision::core::ErrorCode::InvalidConfiguration);
			EXPECT_FALSE(error->diagnostic.empty());
		}

TEST(Algorithm_CalibrationFrameworkTests, Create_ParameterFileIsMalformed_ReturnsInvalidConfiguration)
		{
			CalibrationTestDirectory directory("malformed_parameter_file");
			writeTextFile(
				directory.path() / "vision_algorithm.yaml",
				makeMainConfiguration());
			writeTextFile(
				directory.path() / "calibration_parameters.yaml",
				"%YAML:1.0\n---\ndetection: [broken\n");
			writeCalibrationFiles(directory.path());

			const auto outcome = vision::algorithm::CalibrationAlgorithm::create(
				directory.path() / "vision_algorithm.yaml");

			const auto* error = std::get_if<vision::core::Error>(&outcome);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(
				error->code,
				vision::core::ErrorCode::InvalidConfiguration);
			EXPECT_FALSE(error->diagnostic.empty());
		}

TEST(Algorithm_CalibrationFrameworkTests, Create_ParameterConfigurationIsInvalid_ReturnsInvalidConfiguration)
		{
			CalibrationTestDirectory directory("invalid_parameter_configuration");
			writeTextFile(
				directory.path() / "vision_algorithm.yaml",
				makeMainConfiguration());
			writeTextFile(
				directory.path() / "calibration_parameters.yaml",
				replaceFirst(
					makeCalibrationAlgorithmConfiguration(),
					"minimum_circularity: 0.75",
					"minimum_circularity: 0.0"));
			writeCalibrationFiles(directory.path());

			const auto outcome = vision::algorithm::CalibrationAlgorithm::create(
				directory.path() / "vision_algorithm.yaml");

			const auto* error = std::get_if<vision::core::Error>(&outcome);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(
				error->code,
				vision::core::ErrorCode::InvalidConfiguration);
			EXPECT_FALSE(error->diagnostic.empty());
		}

TEST(Algorithm_CalibrationFrameworkTests, Create_DisabledModeCalibrationFileIsMissing_ReturnsInvalidConfiguration)
		{
			CalibrationTestDirectory directory("missing_disabled_role");
			writeTextFile(
				directory.path() / "vision_algorithm.yaml",
				makeMainConfiguration());
			writeCalibrationAlgorithmConfiguration(directory.path());
			writeCalibrationFiles(directory.path(), "bottom_left.yaml");

			const auto outcome = vision::algorithm::CalibrationAlgorithm::create(
				directory.path() / "vision_algorithm.yaml");

			const auto* error = std::get_if<vision::core::Error>(&outcome);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(
				error->code,
				vision::core::ErrorCode::InvalidConfiguration);
			EXPECT_FALSE(error->diagnostic.empty());
		}

TEST(Algorithm_CalibrationFrameworkTests, Create_OneCalibrationMatrixIsInvalid_ReturnsInvalidConfiguration)
		{
			CalibrationTestDirectory directory("invalid_matrix");
			writeTextFile(
				directory.path() / "vision_algorithm.yaml",
				makeMainConfiguration());
			writeCalibrationAlgorithmConfiguration(directory.path());
			writeCalibrationFiles(directory.path());
			writeTextFile(
				directory.path() / "top_right.yaml",
				makeCalibrationConfiguration(true));

			const auto outcome = vision::algorithm::CalibrationAlgorithm::create(
				directory.path() / "vision_algorithm.yaml");

			const auto* error = std::get_if<vision::core::Error>(&outcome);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(
				error->code,
				vision::core::ErrorCode::InvalidConfiguration);
			EXPECT_FALSE(error->diagnostic.empty());
		}

} }
