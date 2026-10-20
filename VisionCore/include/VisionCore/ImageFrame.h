#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core/mat.hpp>

namespace vision::core {

enum class CameraRole {
    Unknown,
    Center,
    TopLeft,
    TopRight,
    BottomRight,
    BottomLeft
};

struct ImageFrame {
    cv::Mat image;
    std::chrono::steady_clock::time_point capturedAt;
    std::uint64_t frameNumber{ 0 };
    std::string cameraId;
    CameraRole cameraRole{CameraRole::Unknown};
};

struct ImageFrames {
    std::vector<ImageFrame> frames;
};

} // namespace vision::core
