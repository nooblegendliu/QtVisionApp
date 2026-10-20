#include "TestSupport/TestDirectory.h"
#include <VisionAlgorithm/VisionAlgorithm.h>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <variant>

#include "AlgorithmConfig.h"

namespace vision::test::algorithm::creation {
namespace {

using ScopedTestDirectory = vision::test::support::ScopedTestDirectory;

[[nodiscard]] std::filesystem::path findWorkspaceRoot()
{
    const std::array<std::filesystem::path, 2> startingPoints{
        std::filesystem::current_path(),
        std::filesystem::absolute(
            std::filesystem::path(__FILE__)).parent_path(),
    };

    for (auto current : startingPoints) {
        while (!current.empty()) {
            if (std::filesystem::exists(current / "QtVisionApp.slnx")) {
                return current;
            }

            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
    }

    return {};
}

void writeTextFile(
    const std::filesystem::path& path,
    const std::string& contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open()) << path.string();
    output << contents;
    ASSERT_TRUE(output.good()) << path.string();
}

[[nodiscard]] std::string yamlPath(const std::filesystem::path& path)
{
    return '"' + path.generic_string() + '"';
}

[[nodiscard]] std::string makeEdgeMainConfig(
    const std::filesystem::path& calibrationPath,
    const std::filesystem::path& parameterPath)
{
    return
        "%YAML:1.0\n"
        "---\n"
        "timeout_ms: 500\n"
        "enabled_algorithms:\n"
        "  edge: 1\n"
        "  mark: 0\n"
        "calibration:\n"
        "  parameters: \"missing_calibration_parameters.yaml\"\n"
        "  center: " + yamlPath(calibrationPath) + "\n"
        "  top_left: \"missing_top_left.yaml\"\n"
        "  top_right: \"missing_top_right.yaml\"\n"
        "  bottom_right: \"missing_bottom_right.yaml\"\n"
        "  bottom_left: \"missing_bottom_left.yaml\"\n"
        "edge:\n"
        "  parameters: " + yamlPath(parameterPath) + "\n";
}

[[nodiscard]] std::string makeMarkMainConfig()
{
    return R"yaml(%YAML:1.0
---
timeout_ms: 500
enabled_algorithms:
  edge: 0
  mark: 1
calibration:
  parameters: "missing_calibration_parameters.yaml"
  center: "missing_center.yaml"
  top_left: "calibration.yaml"
  top_right: "calibration.yaml"
  bottom_right: "calibration.yaml"
  bottom_left: "calibration.yaml"
mark:
  parameters: "mark.yaml"
)yaml";
}

[[nodiscard]] std::string makeSmallCalibrationConfig()
{
    return R"yaml(%YAML:1.0
---
image_width: 32
image_height: 32
camera_matrix: !!opencv-matrix
  rows: 3
  cols: 3
  dt: d
  data: [ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 ]
distortion_coefficients: !!opencv-matrix
  rows: 1
  cols: 5
  dt: d
  data: [ 0.0, 0.0, 0.0, 0.0, 0.0 ]
new_camera_matrix: !!opencv-matrix
  rows: 3
  cols: 3
  dt: d
  data: [ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 ]
image_to_physical: !!opencv-matrix
  rows: 3
  cols: 3
  dt: d
  data: [ 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 ]
)yaml";
}

[[nodiscard]] std::string makeMarkConfigWithMissingModels()
{
    return R"yaml(%YAML:1.0
---
input_image:
  width_px: 32
  height_px: 32
camera_rois:
  top_left: { x_px: 0, y_px: 0, width_px: 32, height_px: 32 }
  top_right: { x_px: 0, y_px: 0, width_px: 32, height_px: 32 }
  bottom_right: { x_px: 0, y_px: 0, width_px: 32, height_px: 32 }
  bottom_left: { x_px: 0, y_px: 0, width_px: 32, height_px: 32 }
shape_matcher:
  class_id: "missing_mark"
  template_file_pattern: "missing_models/%s_templ.yaml"
  template_info_file: "missing_models/mark_info.yaml"
  template_image_width_px: 32
  template_image_height_px: 32
  detector:
    num_features: 32
    pyramid_t: [4, 8]
    weak_threshold: 30.0
    strong_threshold: 60.0
quality:
  mark_geometry:
    top_length: { lower: 0.0, upper: 1000.0 }
    right_length: { lower: 0.0, upper: 1000.0 }
    bottom_length: { lower: 0.0, upper: 1000.0 }
    left_length: { lower: 0.0, upper: 1000.0 }
    diagonal_top_left_to_bottom_right: { lower: 0.0, upper: 1000.0 }
    diagonal_top_right_to_bottom_left: { lower: 0.0, upper: 1000.0 }
  pose_result:
    center_x: { lower: -1000.0, upper: 1000.0 }
    center_y: { lower: -1000.0, upper: 1000.0 }
    absolute_angle_deg: { lower: 0.0, upper: 90.0 }
)yaml";
}

void expectInvalidConfiguration(
    const vision::algorithm::VisionAlgorithm::CreateOutcome& outcome)
{
    const auto* error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
    EXPECT_FALSE(error->message.empty());
    EXPECT_FALSE(error->diagnostic.empty());
}

} // namespace

TEST(Algorithm_VisionAlgorithmCreationTests, ValidEdgeConfigurationReturnsReadyPort)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());

    auto outcome = vision::algorithm::VisionAlgorithm::create(
        workspaceRoot / "VisionAlgorithm" / "config" /
        "vision_algorithm.example.yaml");

    const auto* algorithm = std::get_if<
        std::unique_ptr<vision::core::ILocateAlgorithm>>(&outcome);
    ASSERT_NE(algorithm, nullptr);
    EXPECT_NE(algorithm->get(), nullptr);
}

TEST(Algorithm_VisionAlgorithmCreationTests, MissingMainConfigurationReturnsStableError)
{
    ScopedTestDirectory directory("missing_main_configuration");

    const auto outcome = vision::algorithm::VisionAlgorithm::create(
        directory.path() / "missing.yaml");

    expectInvalidConfiguration(outcome);
}

TEST(Algorithm_VisionAlgorithmCreationTests, MalformedMainConfigurationReturnsStableError)
{
    ScopedTestDirectory directory("malformed_main_configuration");
    const auto configPath = directory.path() / "vision_algorithm.yaml";
    writeTextFile(
        configPath,
        "%YAML:1.0\n---\ntimeout_ms: \"five hundred\"\n");

    const auto outcome =
        vision::algorithm::VisionAlgorithm::create(configPath);

    expectInvalidConfiguration(outcome);
}

TEST(Algorithm_VisionAlgorithmCreationTests, MissingEdgeParameterReturnsStableError)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    ScopedTestDirectory directory("missing_edge_parameter");
    const auto configPath = directory.path() / "vision_algorithm.yaml";
    writeTextFile(
        configPath,
        makeEdgeMainConfig(
            workspaceRoot / "VisionAlgorithm" / "config" /
                "calibration" / "center.example.yaml",
            directory.path() / "missing_edge.yaml"));

    const auto outcome =
        vision::algorithm::VisionAlgorithm::create(configPath);

    expectInvalidConfiguration(outcome);
}

TEST(Algorithm_VisionAlgorithmCreationTests, MissingEdgeCalibrationReturnsStableError)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    ScopedTestDirectory directory("missing_edge_calibration");
    const auto configPath = directory.path() / "vision_algorithm.yaml";
    writeTextFile(
        configPath,
        makeEdgeMainConfig(
            directory.path() / "missing_calibration.yaml",
            workspaceRoot / "VisionAlgorithm" / "config" /
                "parameters" / "edge.example.yaml"));

    const auto outcome =
        vision::algorithm::VisionAlgorithm::create(configPath);

    expectInvalidConfiguration(outcome);
}

TEST(Algorithm_VisionAlgorithmCreationTests, EdgeOnlyDoesNotLoadDisabledMarkCalibrationsOrCalibrationParameters)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    ScopedTestDirectory directory("edge_ignores_disabled_mark_calibrations");
    const auto configPath = directory.path() / "vision_algorithm.yaml";
    writeTextFile(
        configPath,
        makeEdgeMainConfig(
            workspaceRoot / "VisionAlgorithm" / "config" /
                "calibration" / "center.example.yaml",
            workspaceRoot / "VisionAlgorithm" / "config" /
                "parameters" / "edge.example.yaml"));

    const auto outcome = vision::algorithm::VisionAlgorithm::create(configPath);

    const auto* algorithm = std::get_if<
        std::unique_ptr<vision::core::ILocateAlgorithm>>(&outcome);
    ASSERT_NE(algorithm, nullptr);
    EXPECT_NE(algorithm->get(), nullptr);
}

TEST(Algorithm_VisionAlgorithmCreationTests, LegacyCalibrationFieldsAreRejected)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    ScopedTestDirectory directory("legacy_calibration_fields");
    const auto configPath = directory.path() / "vision_algorithm.yaml";
    writeTextFile(
        configPath,
        "%YAML:1.0\n"
        "---\n"
        "timeout_ms: 500\n"
        "enabled_algorithms: { edge: 1, mark: 0 }\n"
        "edge:\n"
        "  calibration: " +
            yamlPath(
                workspaceRoot / "VisionAlgorithm" / "config" /
                "calibration" / "center.example.yaml") + "\n"
        "  parameters: " +
            yamlPath(
                workspaceRoot / "VisionAlgorithm" / "config" /
                "parameters" / "edge.example.yaml") + "\n");

    expectInvalidConfiguration(
        vision::algorithm::VisionAlgorithm::create(configPath));
}

TEST(Algorithm_VisionAlgorithmCreationTests, CalibrationNodeRequiresParameterPath)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    ScopedTestDirectory directory("missing_calibration_parameter_path");
    const auto configPath = directory.path() / "vision_algorithm.yaml";

    writeTextFile(
        configPath,
        "%YAML:1.0\n"
        "---\n"
        "timeout_ms: 500\n"
        "enabled_algorithms: { edge: 1, mark: 0 }\n"
        "calibration:\n"
        "  center: \"center.yaml\"\n"
        "  top_left: \"top_left.yaml\"\n"
        "  top_right: \"top_right.yaml\"\n"
        "  bottom_right: \"bottom_right.yaml\"\n"
        "  bottom_left: \"bottom_left.yaml\"\n"
        "edge:\n"
        "  parameters: " +
            yamlPath(
                workspaceRoot / "VisionAlgorithm" / "config" /
                "parameters" / "edge.example.yaml") + "\n");

    expectInvalidConfiguration(
        vision::algorithm::VisionAlgorithm::create(configPath));
}

TEST(Algorithm_VisionAlgorithmCreationTests, CalibrationNodeRequiresEveryCameraRole)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    ScopedTestDirectory directory("missing_calibration_role");
    const auto configPath = directory.path() / "vision_algorithm.yaml";

    const std::array<const char*, 5> roles{
        "center",
        "top_left",
        "top_right",
        "bottom_right",
        "bottom_left"
    };

    for (const char* omittedRole : roles) {
        SCOPED_TRACE(omittedRole);
        std::string calibrationEntries;
        for (const char* role : roles) {
            if (std::string(role) == omittedRole) {
                continue;
            }
            calibrationEntries +=
                "  " + std::string(role) + ": \"" + role + ".yaml\"\n";
        }

        writeTextFile(
            configPath,
            "%YAML:1.0\n"
            "---\n"
            "timeout_ms: 500\n"
            "enabled_algorithms: { edge: 1, mark: 0 }\n"
            "calibration:\n"
            "  parameters: \"missing_calibration_parameters.yaml\"\n" +
            calibrationEntries +
            "edge:\n"
            "  parameters: " +
                yamlPath(
                    workspaceRoot / "VisionAlgorithm" / "config" /
                    "parameters" / "edge.example.yaml") + "\n");

        expectInvalidConfiguration(
            vision::algorithm::VisionAlgorithm::create(configPath));
    }
}

TEST(Algorithm_VisionAlgorithmCreationTests, CalibrationRolePathMustBeNonEmptyString)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    ScopedTestDirectory directory("invalid_calibration_role_path");
    const auto configPath = directory.path() / "vision_algorithm.yaml";

    const std::array<const char*, 2> invalidCenterValues{
        "\"\"",
        "42"
    };

    for (const char* invalidValue : invalidCenterValues) {
        SCOPED_TRACE(invalidValue);
        writeTextFile(
            configPath,
            "%YAML:1.0\n"
            "---\n"
            "timeout_ms: 500\n"
            "enabled_algorithms: { edge: 1, mark: 0 }\n"
            "calibration:\n"
            "  parameters: \"missing_calibration_parameters.yaml\"\n"
            "  center: " + std::string(invalidValue) + "\n"
            "  top_left: \"top_left.yaml\"\n"
            "  top_right: \"top_right.yaml\"\n"
            "  bottom_right: \"bottom_right.yaml\"\n"
            "  bottom_left: \"bottom_left.yaml\"\n"
            "edge:\n"
            "  parameters: " +
                yamlPath(
                    workspaceRoot / "VisionAlgorithm" / "config" /
                    "parameters" / "edge.example.yaml") + "\n");

        expectInvalidConfiguration(
            vision::algorithm::VisionAlgorithm::create(configPath));
    }
}

TEST(Algorithm_VisionAlgorithmCreationTests, CalibrationParameterPathMustBeNonEmptyString)
{
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    ScopedTestDirectory directory("invalid_calibration_parameter_path");
    const auto configPath = directory.path() / "vision_algorithm.yaml";

    const std::array<const char*, 2> invalidParameterValues{
        "\"\"",
        "42"
    };

    for (const char* invalidValue : invalidParameterValues) {
        SCOPED_TRACE(invalidValue);
        writeTextFile(
            configPath,
            "%YAML:1.0\n"
            "---\n"
            "timeout_ms: 500\n"
            "enabled_algorithms: { edge: 1, mark: 0 }\n"
            "calibration:\n"
            "  parameters: " + std::string(invalidValue) + "\n"
            "  center: \"center.yaml\"\n"
            "  top_left: \"top_left.yaml\"\n"
            "  top_right: \"top_right.yaml\"\n"
            "  bottom_right: \"bottom_right.yaml\"\n"
            "  bottom_left: \"bottom_left.yaml\"\n"
            "edge:\n"
            "  parameters: " +
                yamlPath(
                    workspaceRoot / "VisionAlgorithm" / "config" /
                    "parameters" / "edge.example.yaml") + "\n");

        expectInvalidConfiguration(
            vision::algorithm::VisionAlgorithm::create(configPath));
    }
}

TEST(Algorithm_VisionAlgorithmCreationTests, CalibrationPathsResolveFromMainConfigDirectory)
{
    ScopedTestDirectory directory("relative_calibration_paths");
    const auto configPath = directory.path() / "vision_algorithm.yaml";
    writeTextFile(
        configPath,
        R"yaml(%YAML:1.0
---
timeout_ms: 500
enabled_algorithms: { edge: 1, mark: 1 }
calibration:
  parameters: "parameters/calibration.yaml"
  center: "calibration/center.yaml"
  top_left: "calibration/top_left.yaml"
  top_right: "calibration/top_right.yaml"
  bottom_right: "calibration/bottom_right.yaml"
  bottom_left: "calibration/bottom_left.yaml"
edge:
  parameters: "parameters/edge.yaml"
mark:
  parameters: "parameters/mark.yaml"
)yaml");

    const auto config =
        vision::algorithm::detail::loadVisionAlgorithmConfig(configPath);

    const auto expectedBase =
        std::filesystem::absolute(directory.path()).lexically_normal();
    EXPECT_EQ(
        config.calibration.parameterPath,
        expectedBase / "parameters" / "calibration.yaml");
    EXPECT_EQ(
        config.calibration.centerPath,
        expectedBase / "calibration" / "center.yaml");
    EXPECT_EQ(
        config.calibration.topLeftPath,
        expectedBase / "calibration" / "top_left.yaml");
    EXPECT_EQ(
        config.calibration.topRightPath,
        expectedBase / "calibration" / "top_right.yaml");
    EXPECT_EQ(
        config.calibration.bottomRightPath,
        expectedBase / "calibration" / "bottom_right.yaml");
    EXPECT_EQ(
        config.calibration.bottomLeftPath,
        expectedBase / "calibration" / "bottom_left.yaml");
    ASSERT_TRUE(config.edge.has_value());
    ASSERT_TRUE(config.mark.has_value());
    EXPECT_EQ(
        config.edge->parameterPath,
        expectedBase / "parameters" / "edge.yaml");
    EXPECT_EQ(
        config.mark->parameterPath,
        expectedBase / "parameters" / "mark.yaml");
}

TEST(Algorithm_VisionAlgorithmCreationTests, MissingMarkModelsReturnStableError)
{
    ScopedTestDirectory directory("missing_mark_models");
    const auto configPath = directory.path() / "vision_algorithm.yaml";
    writeTextFile(configPath, makeMarkMainConfig());
    writeTextFile(
        directory.path() / "calibration.yaml",
        makeSmallCalibrationConfig());
    writeTextFile(
        directory.path() / "mark.yaml",
        makeMarkConfigWithMissingModels());

    const auto outcome =
        vision::algorithm::VisionAlgorithm::create(configPath);

    expectInvalidConfiguration(outcome);
    const auto& error = std::get<vision::core::Error>(outcome);
    EXPECT_NE(error.diagnostic.find("missing_models"), std::string::npos);
}

} // namespace vision::test::algorithm::creation
