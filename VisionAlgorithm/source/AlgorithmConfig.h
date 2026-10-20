#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>

#include "edge/EdgeTypes.h"
#include "mark/MarkTypes.h"

namespace vision::algorithm::detail {

    enum class CalibrationBlobColor {
        Dark,
        Light
    };

    struct CircleDetectionConfig {
        CalibrationBlobColor blobColor{ CalibrationBlobColor::Dark };

        float minimumThreshold{ 0.0F };
        float maximumThreshold{ 255.0F };
        float thresholdStep{ 10.0F };

        std::size_t minimumRepeatability{ 2 };
        float minimumDistancePx{ 0.0F };
        float minimumAreaPx2{ 0.0F };
        float maximumAreaPx2{ 0.0F };
        float minimumCircularity{ 0.0F };
    };

    struct CalibrationAlgorithmConfig {
        int gridSidePoints{ 5 };
        CircleDetectionConfig center;
        CircleDetectionConfig topLeft;
        CircleDetectionConfig topRight;
        CircleDetectionConfig bottomRight;
        CircleDetectionConfig bottomLeft;

        double ransacThresholdMm{ 0.0 };
        std::size_t minimumUsedPoints{ 24 };
        double maximumRmsErrorMm{ 0.0 };
        double maximumResidualMm{ 0.0 };
    };

    struct CalibrationResourceConfig {
        std::filesystem::path parameterPath;
        std::filesystem::path centerPath;
        std::filesystem::path topLeftPath;
        std::filesystem::path topRightPath;
        std::filesystem::path bottomRightPath;
        std::filesystem::path bottomLeftPath;
    };

    struct EdgeResourceConfig {
        std::filesystem::path parameterPath;
    };

    struct MarkResourceConfig {
        std::filesystem::path parameterPath;
    };


    /**
     * 主配置文件的解析结果。
     *
     * optional 是否有值直接表示对应算法是否启用。
     * 这里只保存资源路径，不加载算法参数和标定内容。
     */
    struct VisionAlgorithmConfig {
        std::chrono::milliseconds timeout{ 500 };
        CalibrationResourceConfig calibration;

        std::optional<EdgeResourceConfig> edge;
        std::optional<MarkResourceConfig> mark;

        [[nodiscard]] bool edgeEnabled() const noexcept
        {
            return edge.has_value();
        }

        [[nodiscard]] bool markEnabled() const noexcept
        {
            return mark.has_value();
        }
    };
    

    [[nodiscard]] VisionAlgorithmConfig
        loadVisionAlgorithmConfig(
            const std::filesystem::path& configPath);

    [[nodiscard]] CalibrationAlgorithmConfig
        loadCalibrationAlgorithmConfig(
            const std::filesystem::path& parameterPath);

    [[nodiscard]] EdgeAlgorithmConfig
        loadEdgeAlgorithmConfig(
            const std::filesystem::path& parameterPath);

    [[nodiscard]] MarkAlgorithmConfig
        loadMarkAlgorithmConfig(
            const std::filesystem::path& parameterPath);

}	// namespace vision::algorithm::detail
