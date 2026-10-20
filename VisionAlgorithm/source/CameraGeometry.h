#pragma once

#include <opencv2/core.hpp>

#include <filesystem>

namespace vision::algorithm::detail {

	class CameraGeometry final {
	public:
		[[nodiscard]] static CameraGeometry loadFromFile(
			const std::filesystem::path& path);

		CameraGeometry(CameraGeometry&&) noexcept = default;
		CameraGeometry& operator=(CameraGeometry&&) noexcept = default;
		CameraGeometry(const CameraGeometry&) = delete;
		CameraGeometry& operator=(const CameraGeometry&) = delete;
		~CameraGeometry() = default;

		[[nodiscard]] cv::Mat rectify(const cv::Mat& source) const;

		[[nodiscard]] cv::Mat rectifyRoi(
			const cv::Mat& source,
			const cv::Rect& rectifiedRoi) const;

		[[nodiscard]] cv::Point2d pixelToPhysical(
			const cv::Point2d& pixel) const;

		[[nodiscard]] cv::Size imageSize() const noexcept
		{
			return imageSize_;
		}


	private:
		CameraGeometry(
			cv::Size imageSize,
			cv::Mat cameraMatrix,
			cv::Mat distortionCoefficients,
			cv::Mat newCameraMatrix,
			cv::Mat imageToPhysical);

		void validateOrThrow() const;
		void initializeMaps();

		cv::Size imageSize_;
		cv::Mat cameraMatrix_;            // Original intrinsic matrix K.
		cv::Mat distortionCoefficients_;  // Distortion vector D.
		cv::Mat newCameraMatrix_;         // Intrinsic matrix used by rectified images.
		cv::Mat imageToPhysical_;         // Rectified pixel coordinates -> physical plane.

		cv::Mat mapX_;
		cv::Mat mapY_;
	};

}	// namespace vision::algorithm::detail
