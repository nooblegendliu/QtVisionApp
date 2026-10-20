#include "mark/MarkPipeline.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace vision::test::algorithm::mark_pipeline {
namespace {

vision::algorithm::detail::MarkAlgorithmConfig makeValidConfig()
{
    vision::algorithm::detail::MarkAlgorithmConfig config;
    config.inputImageSizePx = {1000, 800};
    config.topLeftCameraRoiPx = {0, 0, 200, 200};
    config.topRightCameraRoiPx = {800, 0, 200, 200};
    config.bottomRightCameraRoiPx = {800, 600, 200, 200};
    config.bottomLeftCameraRoiPx = {0, 600, 200, 200};
    config.shapeMatcher.templateImageSizePx = {100, 100};
    return config;
}

void validate(const vision::algorithm::detail::MarkAlgorithmConfig& config)
{
    const cv::Size calibrationSize = config.inputImageSizePx;
    vision::algorithm::detail::validateMarkPipelineResources(
        config,
        calibrationSize,
        calibrationSize,
        calibrationSize,
        calibrationSize);
}

} // namespace

TEST(Algorithm_MarkResourceValidationTests, AcceptsValidRequiredResources)
{
    EXPECT_NO_THROW(validate(makeValidConfig()));
}

TEST(Algorithm_MarkResourceValidationTests, RejectsInvalidInputAndRoiGeometry)
{
    auto config = makeValidConfig();
    config.inputImageSizePx.width = 0;
    EXPECT_THROW(validate(config), std::invalid_argument);

    config = makeValidConfig();
    config.topLeftCameraRoiPx.x = -1;
    EXPECT_THROW(validate(config), std::invalid_argument);

    config = makeValidConfig();
    config.topRightCameraRoiPx.width = 0;
    EXPECT_THROW(validate(config), std::invalid_argument);

    config = makeValidConfig();
    config.bottomRightCameraRoiPx.x = 900;
    EXPECT_THROW(validate(config), std::invalid_argument);
}

TEST(Algorithm_MarkResourceValidationTests, RejectsNonPositiveTemplateImageSize)
{
    auto config = makeValidConfig();
    config.shapeMatcher.templateImageSizePx.height = 0;
    EXPECT_THROW(validate(config), std::invalid_argument);
}

TEST(Algorithm_MarkResourceValidationTests, EnforcesTemplateImageFitsInsideRoi)
{
    auto config = makeValidConfig();
    config.shapeMatcher.templateImageSizePx = {200, 200};
    EXPECT_NO_THROW(validate(config));

    config = makeValidConfig();
    config.shapeMatcher.templateImageSizePx = {201, 200};
    EXPECT_THROW(validate(config), std::invalid_argument);

    config = makeValidConfig();
    config.shapeMatcher.templateImageSizePx = {200, 201};
    EXPECT_THROW(validate(config), std::invalid_argument);
}

TEST(Algorithm_MarkResourceValidationTests, RejectsRoleSpecificCalibrationSizeMismatch)
{
    const auto config = makeValidConfig();

    EXPECT_THROW(
        vision::algorithm::detail::validateMarkPipelineResources(
            config,
            config.inputImageSizePx,
            cv::Size(999, 800),
            config.inputImageSizePx,
            config.inputImageSizePx),
        std::invalid_argument);
}

} // namespace vision::test::algorithm::mark_pipeline
