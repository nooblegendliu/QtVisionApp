#include "TestSupport/TestDirectory.h"
#include <VisionAlgorithm/VisionAlgorithm.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

#include "VisionAlgorithm/Edge/support/EdgePipelineTestSupport.h"

namespace vision::test::algorithm::creation {
namespace {

using PublicApiTestDirectory = vision::test::support::ScopedTestDirectory;

void writeText(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open()) << path.string();
    output << contents;
    ASSERT_TRUE(output.good()) << path.string();
}

void expectCreateAndValidationAgree(const std::filesystem::path& path,
                                    vision::core::ErrorCode expectedCode) {
    const auto createOutcome = vision::algorithm::VisionAlgorithm::create(path);
    const auto validationStatus = vision::algorithm::VisionAlgorithm::validateConfiguration(path);
    const auto* createError = std::get_if<vision::core::Error>(&createOutcome);
    const auto* validationError = std::get_if<vision::core::Error>(&validationStatus);
    ASSERT_NE(createError, nullptr);
    ASSERT_NE(validationError, nullptr);
    EXPECT_EQ(createError->code, expectedCode);
    EXPECT_EQ(validationError->code, expectedCode);
    EXPECT_EQ(createError->message, validationError->message);
    EXPECT_EQ(createError->diagnostic, validationError->diagnostic);
}

std::filesystem::path exampleConfiguration() {
    return edge_pipeline::findWorkspaceRoot() / "VisionAlgorithm" / "config" /
        "vision_algorithm.example.yaml";
}

TEST(Algorithm_VisionAlgorithmPublicApiTests, CreateAndValidateAgreeForValidEdgeConfiguration) {
    const auto configPath = exampleConfiguration();
    ASSERT_TRUE(std::filesystem::exists(configPath));

    const auto createOutcome = vision::algorithm::VisionAlgorithm::create(configPath);
    const auto validationStatus = vision::algorithm::VisionAlgorithm::validateConfiguration(configPath);

    const auto* port = std::get_if<std::unique_ptr<vision::core::ILocateAlgorithm>>(&createOutcome);
    ASSERT_NE(port, nullptr);
    EXPECT_NE(port->get(), nullptr);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(validationStatus));
}

TEST(Algorithm_VisionAlgorithmPublicApiTests, CreateAndValidateAgreeForMissingAndMalformedConfiguration) {
    PublicApiTestDirectory directory("missing_and_malformed");
    expectCreateAndValidationAgree(
        directory.path() / "missing.yaml", vision::core::ErrorCode::InvalidConfiguration);

    const auto malformed = directory.path() / "malformed.yaml";
    writeText(malformed, "%YAML:1.0\n---\ntimeout_ms: [broken\n");
    expectCreateAndValidationAgree(malformed, vision::core::ErrorCode::InvalidConfiguration);
}

TEST(Algorithm_VisionAlgorithmPublicApiTests, CreateAndValidateAgreeForIncompatibleResources) {
    PublicApiTestDirectory directory("incompatible_resources");
    const auto config = directory.path() / "vision_algorithm.yaml";
    writeText(config,
              "%YAML:1.0\n---\n"
              "timeout_ms: 500\n"
              "enabled_algorithms: { edge: 1, mark: 0 }\n"
              "calibration:\n"
              "  parameters: \"missing_calibration_parameters.yaml\"\n"
              "  center: \"missing_calibration.yaml\"\n"
              "  top_left: \"missing_top_left.yaml\"\n"
              "  top_right: \"missing_top_right.yaml\"\n"
              "  bottom_right: \"missing_bottom_right.yaml\"\n"
              "  bottom_left: \"missing_bottom_left.yaml\"\n"
              "edge:\n"
              "  parameters: \"missing_parameters.yaml\"\n");

    expectCreateAndValidationAgree(config, vision::core::ErrorCode::InvalidConfiguration);
}

TEST(Algorithm_VisionAlgorithmPublicApiTests, LocateEdgeBlankImageReturnsNgWithoutChangingPixels) {
    auto createOutcome = vision::algorithm::VisionAlgorithm::create(exampleConfiguration());
    auto* port = std::get_if<std::unique_ptr<vision::core::ILocateAlgorithm>>(&createOutcome);
    ASSERT_NE(port, nullptr);
    ASSERT_NE(port->get(), nullptr);
    cv::Mat image(edge_pipeline::kImageHeight, edge_pipeline::kImageWidth, CV_8UC1, cv::Scalar{0});
    const cv::Mat original = image.clone();

    const auto outcome = (*port)->locate(vision::core::EdgeAlgorithmInput{std::cref(image)});

    const auto* output = std::get_if<vision::core::AlgorithmOutput>(&outcome);
    ASSERT_NE(output, nullptr);
    EXPECT_FALSE(output->valid);
    EXPECT_DOUBLE_EQ(output->x, 0.0);
    EXPECT_DOUBLE_EQ(output->y, 0.0);
    EXPECT_DOUBLE_EQ(output->r, 0.0);
    EXPECT_TRUE(edge_pipeline::imagesAreEqual(image, original));
}

TEST(Algorithm_VisionAlgorithmPublicApiTests, LocateEdgeInvalidImageReturnsStableError) {
    auto createOutcome = vision::algorithm::VisionAlgorithm::create(exampleConfiguration());
    auto* port = std::get_if<std::unique_ptr<vision::core::ILocateAlgorithm>>(&createOutcome);
    ASSERT_NE(port, nullptr);
    const cv::Mat empty;

    const auto outcome = (*port)->locate(vision::core::EdgeAlgorithmInput{std::cref(empty)});

    const auto* error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidImage);
    EXPECT_FALSE(error->message.empty());
}

TEST(Algorithm_VisionAlgorithmPublicApiTests, EdgeOnlyConfigurationRejectsMarkInputWithoutMatching) {
    auto createOutcome = vision::algorithm::VisionAlgorithm::create(exampleConfiguration());
    auto* port = std::get_if<std::unique_ptr<vision::core::ILocateAlgorithm>>(&createOutcome);
    ASSERT_NE(port, nullptr);
    const cv::Mat image(1, 1, CV_8UC1, cv::Scalar{0});
    const vision::core::MarkPointAlgorithmInput input{
        std::cref(image), std::cref(image), std::cref(image), std::cref(image)};

    const auto outcome = (*port)->locate(input);

    const auto* error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::AlgorithmFailure);
    EXPECT_NE(error->diagnostic.find("disabled"), std::string::npos);
}

} // namespace
} // namespace vision::test::algorithm::creation
