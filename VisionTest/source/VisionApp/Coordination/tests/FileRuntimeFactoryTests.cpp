#include "FileRuntimeFactory.h"
#include "TestSupport/TestDirectory.h"
#include "VisionAlgorithm/Edge/support/EdgePipelineTestSupport.h"

#include <VisionCore/VisionCore.h>
#include <gtest/gtest.h>
#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

namespace vision::test::app {
namespace {

using namespace vision::app;
using namespace vision::core;
namespace edge = vision::test::algorithm::edge_pipeline;

class App_FileRuntime : public ::testing::Test {
protected:
    void SetUp() override {
        const auto examples = edge::findWorkspaceRoot() / "VisionAlgorithm" / "config";
        ASSERT_TRUE(std::filesystem::exists(examples));
        std::filesystem::copy_file(examples / "calibration" / "center.example.yaml",
                                   directory_.path() / "center.yaml");
        std::filesystem::copy_file(examples / "parameters" / "edge.example.yaml",
                                   directory_.path() / "edge.yaml");
        config_.algorithmConfigPath = directory_.path() / "algorithm.yaml";
        // This tests result correctness, not execution speed on a CI machine.
        std::ofstream mainConfig(config_.algorithmConfigPath, std::ios::binary);
        ASSERT_TRUE(mainConfig.is_open());
        mainConfig << R"yaml(%YAML:1.0
---
timeout_ms: 30000
enabled_algorithms: { edge: 1, mark: 0 }
calibration:
  parameters: "unused-calibration-parameters.yaml"
  center: "center.yaml"
  top_left: "unused-top-left.yaml"
  top_right: "unused-top-right.yaml"
  bottom_right: "unused-bottom-right.yaml"
  bottom_left: "unused-bottom-left.yaml"
edge:
  parameters: "edge.yaml"
)yaml";
        ASSERT_TRUE(mainConfig.good());
    }

    std::filesystem::path saveImage(const cv::Mat& image) {
        const auto path = directory_.path() / L"测试图像.png";
        std::vector<unsigned char> encoded;
        if (!cv::imencode(".png", image, encoded)) {
            throw std::runtime_error("Unable to encode file runtime fixture");
        }
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        if (!output.good()) {
            throw std::runtime_error("Unable to write file runtime fixture");
        }
        return path;
    }

    std::unique_ptr<VisionCore> createCore(const cv::Mat& image) {
        auto outcome = makeFileCoreFactory(saveImage(image))(config_, nullptr, nullptr, nullptr);
        if (const auto* error = std::get_if<Error>(&outcome)) {
            ADD_FAILURE() << error->message << ": " << error->diagnostic;
            return {};
        }
        return std::move(std::get<std::unique_ptr<VisionCore>>(outcome));
    }

    support::ScopedTestDirectory directory_{"app-file-runtime"};
    VisionRuntimeConfig config_;
};

TEST_F(App_FileRuntime, MissingAndMalformedFilesReturnConfigurationError) {
    const auto malformed = directory_.path() / "malformed.png";
    {
        std::ofstream output(malformed, std::ios::binary);
        output << "This is not an image";
    }
    for (const auto& path : {directory_.path() / "missing.png", malformed}) {
        const auto outcome = makeFileCoreFactory(path)(config_, nullptr, nullptr, nullptr);
        const auto* error = std::get_if<Error>(&outcome);
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(error->code, ErrorCode::InvalidConfiguration);
        EXPECT_FALSE(error->message.empty());
    }
}

TEST_F(App_FileRuntime, RealEdgeAlgorithmReturnsSyntheticGeometryAndPreservesSourcePixels) {
    const cv::Mat image = edge::makeFactoryScene();
    auto core = createCore(image);
    ASSERT_NE(core, nullptr);
    const auto outcome = core->locate(LocateOptions{LocateMode::Edge});
    const auto* result = std::get_if<LocateResult>(&outcome);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->decision, LocateDecision::Ok);
    EXPECT_NEAR(result->x, edge::kImageCenterX, 2.0);
    EXPECT_NEAR(result->y, edge::kImageCenterY, 2.0);
    EXPECT_NEAR(result->r, 0.0, 0.3);

    const auto captured = core->captureForCalibration(LocateMode::Edge);
    const auto* frames = std::get_if<ImageFrames>(&captured);
    ASSERT_NE(frames, nullptr);
    ASSERT_EQ(frames->frames.size(), 1U);
    EXPECT_TRUE(edge::imagesAreEqual(frames->frames[0].image, image));
    EXPECT_EQ(core->status().state, CoreState::Ready);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(core->shutdown()));
}

TEST_F(App_FileRuntime, BlankImageReturnsNgWithZeroCoordinates) {
    auto core = createCore(cv::Mat(edge::kImageHeight, edge::kImageWidth,
                                  CV_8UC1, cv::Scalar(0)));
    ASSERT_NE(core, nullptr);
    const auto outcome = core->locate(LocateOptions{LocateMode::Edge});
    const auto* result = std::get_if<LocateResult>(&outcome);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->decision, LocateDecision::Ng);
    EXPECT_DOUBLE_EQ(result->x, 0.0);
    EXPECT_DOUBLE_EQ(result->y, 0.0);
    EXPECT_DOUBLE_EQ(result->r, 0.0);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(core->shutdown()));
}

TEST_F(App_FileRuntime, CapturesOwnPixelsAndUnsupportedModeDoesNotPreventRecoveryOrShutdown) {
    const cv::Mat image(24, 32, CV_8UC1, cv::Scalar(73));
    auto core = createCore(image);
    ASSERT_NE(core, nullptr);
    auto first = core->captureForCalibration(LocateMode::Edge);
    auto second = core->captureForCalibration(LocateMode::Edge);
    auto* firstFrames = std::get_if<ImageFrames>(&first);
    auto* secondFrames = std::get_if<ImageFrames>(&second);
    ASSERT_NE(firstFrames, nullptr);
    ASSERT_NE(secondFrames, nullptr);
    ASSERT_EQ(firstFrames->frames.size(), 1U);
    ASSERT_EQ(secondFrames->frames.size(), 1U);
    auto& firstFrame = firstFrames->frames[0];
    const auto& secondFrame = secondFrames->frames[0];
    EXPECT_EQ(firstFrame.role, CameraRole::Center);
    EXPECT_FALSE(firstFrame.cameraId.empty());
    EXPECT_EQ(firstFrame.image.type(), CV_8UC1);
    EXPECT_TRUE(firstFrame.image.isContinuous());
    EXPECT_NE(firstFrame.image.data, secondFrame.image.data);
    firstFrame.image.setTo(cv::Scalar(0));
    EXPECT_TRUE(edge::imagesAreEqual(secondFrame.image, image));
    const auto third = core->captureForCalibration(LocateMode::Edge);
    const auto* thirdFrames = std::get_if<ImageFrames>(&third);
    ASSERT_NE(thirdFrames, nullptr);
    ASSERT_EQ(thirdFrames->frames.size(), 1U);
    EXPECT_TRUE(edge::imagesAreEqual(thirdFrames->frames[0].image, image));

    const auto unsupported = core->locate(LocateOptions{LocateMode::MarkPoint});
    const auto* error = std::get_if<Error>(&unsupported);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ErrorCode::InvalidConfiguration);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(core->recover()));
    EXPECT_EQ(core->status().state, CoreState::Ready);
    EXPECT_TRUE(std::holds_alternative<ImageFrames>(core->captureForCalibration(LocateMode::Edge)));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(core->shutdown()));
    EXPECT_EQ(core->status().state, CoreState::Stopped);
    const auto stopped = core->locate(LocateOptions{LocateMode::Edge});
    ASSERT_TRUE(std::holds_alternative<Error>(stopped));
    EXPECT_EQ(std::get<Error>(stopped).code, ErrorCode::NotReady);
}

} // namespace
} // namespace vision::test::app
