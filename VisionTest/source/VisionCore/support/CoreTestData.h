#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "VisionCore/LocateTypes.h"

namespace vision::test::core {

std::chrono::steady_clock::time_point FixedCaptureTime();

cv::Mat MakeGrayImage(int rows = 3, int columns = 4, unsigned char fill = 0U);
cv::Mat MakeNonContinuousGrayImage(int rows = 3, int columns = 4, unsigned char fill = 0U);

vision::core::ImageFrame MakeFrame(vision::core::CameraRole role,
                                   std::string cameraId,
                                   std::uint64_t frameNumber = 1U,
                                   std::chrono::steady_clock::time_point capturedAt = FixedCaptureTime());
vision::core::ImageFrames MakeEdgeFrames();
vision::core::ImageFrames MakeMarkPointFrames(
    const std::vector<vision::core::CameraRole>& order = {});

vision::core::Error MakeError(vision::core::ErrorCode code, const std::string& label);
vision::core::AlgorithmOutput MakeOkOutput(double x = 1.25, double y = -2.5, double r = 3.75);
vision::core::AlgorithmOutput MakeNgOutput(double x = 1.0, double y = 2.0, double r = 3.0);

} // namespace vision::test::core
