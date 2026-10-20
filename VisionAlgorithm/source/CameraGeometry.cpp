#include "CameraGeometry.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace vision::algorithm::detail {

	namespace 
	{
        [[nodiscard]] std::invalid_argument calibrationError(
            const std::filesystem::path& path,
            const std::string& message)
        {
            return std::invalid_argument(
                "Invalid camera calibration '" + path.string() + "': " + message);
        }

        bool isOpenCvConfigurationFormatError(
            const cv::Exception& exception) noexcept
        {
            switch (exception.code) {
            case cv::Error::StsBadArg:
            case cv::Error::StsObjectNotFound:
            case cv::Error::StsParseError:
            case cv::Error::StsUnsupportedFormat:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] int readRequiredInteger(
            const cv::FileStorage& storage,
            const char* key,
            const std::filesystem::path& path)
        {
            const cv::FileNode node = storage[key];
            if (node.empty() || !node.isInt()) {
                throw calibrationError(
                    path,
                    std::string("required integer '") + key + "' is missing.");
            }
            return static_cast<int>(node);
        }

        [[nodiscard]] cv::Mat readRequiredMatrix(
            const cv::FileStorage& storage,
            const char* key,
            const std::filesystem::path& path)
        {
            const cv::FileNode node = storage[key];
            if (node.empty()) {
                throw calibrationError(
                    path,
                    std::string("required matrix '") + key + "' is missing.");
            }

            cv::Mat matrix;
            node >> matrix;
            if (matrix.empty()) {
                throw calibrationError(
                    path,
                    std::string("matrix '") + key + "' is empty.");
            }

            cv::Mat converted;
            matrix.convertTo(converted, CV_64F);
            return converted;
        }

        [[nodiscard]] cv::Mat readOptionalMatrix(
            const cv::FileStorage& storage,
            const char* key)
        {
            const cv::FileNode node = storage[key];
            if (node.empty()) {
                return {};
            }

            cv::Mat matrix;
            node >> matrix;
            if (matrix.empty()) {
                return {};
            }

            cv::Mat converted;
            matrix.convertTo(converted, CV_64F);
            return converted;
        }

        void requireMatrixShape(
            const cv::Mat& matrix,
            int rows,
            int columns,
            const char* name)
        {
            if (matrix.rows != rows || matrix.cols != columns) {
                throw std::invalid_argument(
                    std::string(name) + " must be " + std::to_string(rows) + "x" +
                    std::to_string(columns) + ".");
            }
        }

        void requireFiniteMatrix(const cv::Mat& matrix, const char* name)
        {
            if (!cv::checkRange(matrix, true, nullptr)) {
                throw std::invalid_argument(
                    std::string(name) + " contains NaN or infinity.");
            }
        }

        bool isValidCoefficientCount(std::size_t count) noexcept {
            switch (count) {
            case 4:
            case 5:
            case 8:
            case 12:
            case 14:
                return true;
            default:
                return false;
            }
        }

        void requireValidDistortionCoefficients(
            const cv::Mat& coefficients)
        {
            if (coefficients.empty()) {
                throw std::invalid_argument(
                    "distortion_coefficients must not be empty.");
            }

            if (coefficients.channels() != 1) {
                throw std::invalid_argument(
                    "distortion_coefficients must be a single-channel matrix.");
            }

            if (coefficients.rows != 1 &&
                coefficients.cols != 1) {
                throw std::invalid_argument(
                    "distortion_coefficients must be a row or column vector.");
            }

            const std::size_t coefficientCount =
                coefficients.total();

            if (!isValidCoefficientCount(
                coefficientCount)) {
                throw std::invalid_argument(
                    "distortion_coefficients must contain exactly "
                    "4, 5, 8, 12, or 14 values.");
            }
        }

	}	// namespace

    CameraGeometry::CameraGeometry(
        cv::Size imageSize,
        cv::Mat cameraMatrix,
        cv::Mat distortionCoefficients,
        cv::Mat newCameraMatrix,
        cv::Mat imageToPhysical)
        : imageSize_(imageSize),
        cameraMatrix_(std::move(cameraMatrix)),
        distortionCoefficients_(std::move(distortionCoefficients)),
        newCameraMatrix_(std::move(newCameraMatrix)),
        imageToPhysical_(std::move(imageToPhysical))
    {
        validateOrThrow();
        initializeMaps();
    }

    CameraGeometry CameraGeometry::loadFromFile(
        const std::filesystem::path& path)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) {
            throw calibrationError(path, "file does not exist or is not readable.");
        }

        int imageWidth = 0;
        int imageHeight = 0;
        cv::Mat cameraMatrix;
        cv::Mat distortionCoefficients;
        cv::Mat newCameraMatrix;
        cv::Mat imageToPhysical;

        try {
            cv::FileStorage storage(path.string(), cv::FileStorage::READ);
            if (!storage.isOpened()) {
                throw calibrationError(
                    path,
                    "OpenCV FileStorage could not open the file.");
            }

            imageWidth = readRequiredInteger(storage, "image_width", path);
            imageHeight = readRequiredInteger(storage, "image_height", path);

            cameraMatrix = readRequiredMatrix(
                storage,
                "camera_matrix",
                path);
            distortionCoefficients = readRequiredMatrix(
                storage,
                "distortion_coefficients",
                path);
            newCameraMatrix = readOptionalMatrix(
                storage,
                "new_camera_matrix");
            imageToPhysical = readRequiredMatrix(
                storage,
                "image_to_physical",
                path);
        }
        catch (const std::invalid_argument&) {
            throw;
        }
        catch (const cv::Exception& exception) {
            if (!isOpenCvConfigurationFormatError(exception)) {
                throw;
            }

            throw calibrationError(
                path,
                "OpenCV could not parse the calibration file: " +
                    std::string(exception.what()));
        }

        if (newCameraMatrix.empty()) {
            newCameraMatrix = cameraMatrix.clone();
        }

        try {
            return CameraGeometry(
                cv::Size(imageWidth, imageHeight),
                std::move(cameraMatrix),
                std::move(distortionCoefficients),
                std::move(newCameraMatrix),
                std::move(imageToPhysical));
        }
        catch (const std::invalid_argument& exception) {
            throw calibrationError(path, exception.what());
        }
    }

    void CameraGeometry::validateOrThrow() const
    {
        if (imageSize_.width <= 0 || imageSize_.height <= 0) {
            throw std::invalid_argument(
                "image_width and image_height must be greater than zero.");
        }

        requireMatrixShape(cameraMatrix_, 3, 3, "camera_matrix");
        requireMatrixShape(newCameraMatrix_, 3, 3, "new_camera_matrix");
        requireMatrixShape(imageToPhysical_, 3, 3, "image_to_physical");

        requireValidDistortionCoefficients(distortionCoefficients_);

        requireFiniteMatrix(cameraMatrix_, "camera_matrix");
        requireFiniteMatrix(distortionCoefficients_, "distortion_coefficients");
        requireFiniteMatrix(newCameraMatrix_, "new_camera_matrix");
        requireFiniteMatrix(imageToPhysical_, "image_to_physical");

        constexpr double minimumDeterminant = 1.0e-12;
        if (std::abs(cv::determinant(cameraMatrix_)) <= minimumDeterminant) {
            throw std::invalid_argument("camera_matrix is singular.");
        }
        if (std::abs(cv::determinant(newCameraMatrix_)) <= minimumDeterminant) {
            throw std::invalid_argument("new_camera_matrix is singular.");
        }
        if (std::abs(cv::determinant(imageToPhysical_)) <= minimumDeterminant) {
            throw std::invalid_argument("image_to_physical is singular.");
        }
    }

    void CameraGeometry::initializeMaps()
    {
        cv::initUndistortRectifyMap(
            cameraMatrix_,
            distortionCoefficients_,
            cv::Mat(),
            newCameraMatrix_,
            imageSize_,
            CV_32FC1,
            mapX_,
            mapY_);

        if (mapX_.empty() || mapY_.empty()) {
            throw std::runtime_error(
                "OpenCV failed to initialize undistortion maps.");
        }
    }

    cv::Mat CameraGeometry::rectify(const cv::Mat& source) const
    {
        if (source.empty()) {
            throw std::invalid_argument("Cannot rectify an empty image.");
        }
        if (source.size() != imageSize_) {
            throw std::invalid_argument(
                "Runtime image size does not match the calibration image size.");
        }
        if (mapX_.empty() || mapY_.empty()) {
            throw std::logic_error("Undistortion maps are not initialized.");
        }

        cv::Mat corrected;
        cv::remap(
            source,
            corrected,
            mapX_,
            mapY_,
            cv::INTER_LINEAR,
            cv::BORDER_CONSTANT);
        return corrected;
    }

    cv::Mat CameraGeometry::rectifyRoi(
        const cv::Mat& source,
        const cv::Rect& rectifiedRoi) const
    {
        if (source.empty()) {
            throw std::invalid_argument("Cannot rectify an empty image.");
        }
        if (source.size() != imageSize_) {
            throw std::invalid_argument(
                "Runtime image size does not match the calibration image size.");
        }
        if (rectifiedRoi.width <= 0 || rectifiedRoi.height <= 0 ||
            rectifiedRoi.x < 0 || rectifiedRoi.y < 0 ||
            rectifiedRoi.x > imageSize_.width - rectifiedRoi.width ||
            rectifiedRoi.y > imageSize_.height - rectifiedRoi.height) {
            throw std::invalid_argument(
                "Rectified ROI is outside the calibration image.");
        }
        if (mapX_.empty() || mapY_.empty()) {
            throw std::logic_error("Undistortion maps are not initialized.");
        }

        cv::Mat corrected;
        cv::remap(
            source,
            corrected,
            mapX_(rectifiedRoi),
            mapY_(rectifiedRoi),
            cv::INTER_LINEAR,
            cv::BORDER_CONSTANT);
        return corrected;
    }

    cv::Point2d CameraGeometry::pixelToPhysical(
        const cv::Point2d& pixel) const
    {
        if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y)) {
            throw std::invalid_argument(
                "Pixel coordinates contain NaN or infinity.");
        }

        const std::vector<cv::Point2d> source{ pixel };
        std::vector<cv::Point2d> destination;
        cv::perspectiveTransform(source, destination, imageToPhysical_);

        if (destination.size() != 1 ||
            !std::isfinite(destination.front().x) ||
            !std::isfinite(destination.front().y)) {
            throw std::runtime_error(
                "Pixel-to-physical transformation produced an invalid coordinate.");
        }

        return destination.front();
    }

}	// namespace vision::algorithm::detail
