#include "VisionCore/support/CoreTestData.h"

#include <array>
#include <utility>

#include <opencv2/core.hpp>

namespace vision::test::core {
namespace {

unsigned char RolePixel(vision::core::CameraRole role) {
    switch (role) {
    case vision::core::CameraRole::Center:
        return 10U;
    case vision::core::CameraRole::TopLeft:
        return 20U;
    case vision::core::CameraRole::TopRight:
        return 30U;
    case vision::core::CameraRole::BottomRight:
        return 40U;
    case vision::core::CameraRole::BottomLeft:
        return 50U;
    case vision::core::CameraRole::Unknown:
    default:
        return 0U;
    }
}

std::string RoleCameraId(vision::core::CameraRole role) {
    switch (role) {
    case vision::core::CameraRole::Center:
        return "center-camera";
    case vision::core::CameraRole::TopLeft:
        return "top-left-camera";
    case vision::core::CameraRole::TopRight:
        return "top-right-camera";
    case vision::core::CameraRole::BottomRight:
        return "bottom-right-camera";
    case vision::core::CameraRole::BottomLeft:
        return "bottom-left-camera";
    case vision::core::CameraRole::Unknown:
    default:
        return "unknown-camera";
    }
}

} // namespace

std::chrono::steady_clock::time_point FixedCaptureTime() {
    return std::chrono::steady_clock::time_point{std::chrono::milliseconds{1234}};
}

cv::Mat MakeGrayImage(int rows, int columns, unsigned char fill) {
    cv::Mat image(rows, columns, CV_8UC1);
    image.setTo(cv::Scalar(fill));
    return image;
}

cv::Mat MakeNonContinuousGrayImage(int rows, int columns, unsigned char fill) {
    cv::Mat backing = MakeGrayImage(rows, columns + 1, fill);
    return backing(cv::Rect(0, 0, columns, rows));
}

vision::core::ImageFrame MakeFrame(vision::core::CameraRole role,
                                   std::string cameraId,
                                   std::uint64_t frameNumber,
                                   std::chrono::steady_clock::time_point capturedAt) {
    const int rows = role == vision::core::CameraRole::Center ? 3 : 3 + static_cast<int>(RolePixel(role) / 10U);
    const int columns = role == vision::core::CameraRole::Center ? 4 : 4 + static_cast<int>(RolePixel(role) / 10U);
    return vision::core::ImageFrame{
        MakeGrayImage(rows, columns, RolePixel(role)), capturedAt, frameNumber, std::move(cameraId), role};
}

vision::core::ImageFrames MakeEdgeFrames() {
    return vision::core::ImageFrames{
        std::vector<vision::core::ImageFrame>{
            MakeFrame(vision::core::CameraRole::Center, "center-camera", 101U)}};
}

vision::core::ImageFrames MakeMarkPointFrames(const std::vector<vision::core::CameraRole>& order) {
    const std::array<vision::core::CameraRole, 4> defaultOrder{
        vision::core::CameraRole::TopLeft,
        vision::core::CameraRole::TopRight,
        vision::core::CameraRole::BottomRight,
        vision::core::CameraRole::BottomLeft};
    const std::vector<vision::core::CameraRole> selectedOrder =
        order.empty() ? std::vector<vision::core::CameraRole>(defaultOrder.begin(), defaultOrder.end()) : order;

    vision::core::ImageFrames frames;
    frames.frames.reserve(selectedOrder.size());
    std::uint64_t frameNumber = 201U;
    for (vision::core::CameraRole role : selectedOrder) {
        frames.frames.push_back(MakeFrame(role, RoleCameraId(role), frameNumber++));
    }
    return frames;
}

vision::core::Error MakeError(vision::core::ErrorCode code, const std::string& label) {
    return vision::core::Error{code, "message:" + label, "diagnostic:" + label};
}

vision::core::AlgorithmOutput MakeOkOutput(double x, double y, double r) {
    return vision::core::AlgorithmOutput{x, y, r, true};
}

vision::core::AlgorithmOutput MakeNgOutput(double x, double y, double r) {
    return vision::core::AlgorithmOutput{x, y, r, false};
}

} // namespace vision::test::core
