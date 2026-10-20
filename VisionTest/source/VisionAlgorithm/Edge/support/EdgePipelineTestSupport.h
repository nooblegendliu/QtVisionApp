#pragma once

#include "AlgorithmConfig.h"
#include "CameraGeometry.h"
#include "VisionAlgorithm/VisionAlgorithm.h"
#include "edge/EdgePipeline.h"

#include <opencv2/core.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace vision::test::algorithm::edge_pipeline {

inline constexpr int kImageWidth = 5120;
inline constexpr int kImageHeight = 5120;
inline constexpr double kImageCenterX = 2560.0;
inline constexpr double kImageCenterY = 2560.0;
inline constexpr double kWaferWidth = 4200.0;
inline constexpr double kWaferHeight = 3400.0;
inline constexpr double kChamferLength = 220.0;

struct FactorySceneSpec {
    cv::Point2d center{kImageCenterX, kImageCenterY};
    double angleDeg{0.0};
    bool drawArmShadows{true};
};

struct EdgePostprocessMeasurements {
    bool geometryAvailable{false};

    std::array<cv::Point2d, 4> corners{};
    std::array<cv::Point2d, 4> sideMidpoints{};
    cv::Point2d center{};
    double angleDeg{0.0};
    double centerOffset{0.0};

    double topLength{0.0};
    double rightLength{0.0};
    double bottomLength{0.0};
    double leftLength{0.0};
    double diagonalTopLeftToBottomRight{0.0};
    double diagonalTopRightToBottomLeft{0.0};

    bool topQualified{false};
    bool rightQualified{false};
    bool bottomQualified{false};
    bool leftQualified{false};
    bool diagonalTopLeftToBottomRightQualified{false};
    bool diagonalTopRightToBottomLeftQualified{false};
    bool angleQualified{false};
    bool centerOffsetQualified{false};
    bool edgeModeQualified{false};
    bool offsetResultQualified{false};
    bool finalQualified{false};
};

[[nodiscard]] std::filesystem::path findWorkspaceRoot();

[[nodiscard]] vision::algorithm::detail::EdgeAlgorithmConfig
loadDefaultEdgeConfig();

[[nodiscard]] vision::algorithm::detail::CameraGeometry
loadDefaultCenterGeometry();

[[nodiscard]] vision::algorithm::detail::CameraGeometry
loadGeneratedCenterGeometry(const cv::Mat& imageToPhysical);

[[nodiscard]] std::unique_ptr<vision::algorithm::detail::EdgePipeline>
makePipeline(
    vision::algorithm::detail::EdgeAlgorithmConfig config,
    const std::shared_ptr<
        vision::algorithm::DemoEdgeVisualizationStore>& visualizationStore);

[[nodiscard]] std::unique_ptr<vision::algorithm::detail::EdgePipeline>
makePipeline(
    vision::algorithm::detail::EdgeAlgorithmConfig config,
    vision::algorithm::detail::CameraGeometry centerGeometry,
    const std::shared_ptr<
        vision::algorithm::DemoEdgeVisualizationStore>& visualizationStore);

[[nodiscard]] cv::Mat makeFactoryScene(const FactorySceneSpec& spec = {});

[[nodiscard]] vision::algorithm::detail::ExecutionContext
makeExecutionContext();

[[nodiscard]] bool imagesAreEqual(
    const cv::Mat& first,
    const cv::Mat& second);

// 当前 EdgePipeline 测试固定使用单位 image_to_physical，因而以下测量值
// 与生产后处理中的物理坐标测量数值一致。
[[nodiscard]] EdgePostprocessMeasurements evaluatePostprocess(
    const vision::algorithm::DemoEdgeVisualization& visualization,
    const vision::algorithm::detail::EdgeAlgorithmConfig& config);

[[nodiscard]] cv::Mat renderFactoryResult(
    const cv::Mat& original,
    const vision::algorithm::DemoEdgeVisualization& visualization,
    const vision::algorithm::detail::EdgeAlgorithmConfig& config,
    const vision::core::AlgorithmOutput& output);

[[nodiscard]] bool saveFactoryResult(
    const cv::Mat& original,
    const vision::algorithm::DemoEdgeVisualization& visualization,
    const vision::algorithm::detail::EdgeAlgorithmConfig& config,
    const vision::core::AlgorithmOutput& output,
    const std::string& fileName,
    std::filesystem::path& outputPath);

} // namespace vision::test::algorithm::edge_pipeline
