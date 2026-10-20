#include "CalibrationServices.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace vision::app {
namespace {

using vision::algorithm::CalibrationGridOrientation;
using vision::core::CameraRole;
using vision::core::Error;
using vision::core::ErrorCode;

Error invalidFile(std::string diagnostic)
{
    return Error{
        ErrorCode::InvalidConfiguration,
        "The calibration point file is invalid.",
        std::move(diagnostic)};
}

Error fileFailure(std::string message, std::string diagnostic)
{
    return Error{ErrorCode::InternalFailure, std::move(message),
                 std::move(diagnostic)};
}

std::string_view trim(std::string_view value)
{
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

bool parseDouble(std::string_view text, double& value)
{
    text = trim(text);
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(
        first, last, value, std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == last &&
        std::isfinite(value);
}

bool duplicatePoints(const vision::algorithm::PhysicalPointSet& points)
{
    for (std::size_t index = 0; index < points.size(); ++index) {
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (points[index] == points[previous]) return true;
        }
    }
    return false;
}

bool allCollinear(const vision::algorithm::PhysicalPointSet& points)
{
    if (points.size() < 3U) return true;
    std::size_t first = 0U;
    std::size_t second = 1U;
    double maximumDistanceSquared = 0.0;
    for (std::size_t a = 0; a < points.size(); ++a) {
        for (std::size_t b = a + 1U; b < points.size(); ++b) {
            const cv::Point2d delta = points[b] - points[a];
            const double distanceSquared = delta.dot(delta);
            if (distanceSquared > maximumDistanceSquared) {
                maximumDistanceSquared = distanceSquared;
                first = a;
                second = b;
            }
        }
    }
    if (!(maximumDistanceSquared > 0.0)) return true;
    const cv::Point2d baseline = points[second] - points[first];
    const double tolerance = 64.0 * std::numeric_limits<double>::epsilon() *
        std::max(1.0, maximumDistanceSquared);
    for (std::size_t index = 0; index < points.size(); ++index) {
        const cv::Point2d offset = points[index] - points[first];
        const double cross = baseline.x * offset.y - baseline.y * offset.x;
        if (std::abs(cross) > tolerance) return false;
    }
    return true;
}

const char* roleNode(CameraRole role) noexcept
{
    switch (role) {
    case CameraRole::Center: return "center";
    case CameraRole::TopLeft: return "top_left";
    case CameraRole::TopRight: return "top_right";
    case CameraRole::BottomRight: return "bottom_right";
    case CameraRole::BottomLeft: return "bottom_left";
    default: return nullptr;
    }
}

bool finiteMatrix(const cv::Matx33d& matrix)
{
    for (double value : matrix.val) {
        if (!std::isfinite(value)) return false;
    }
    return std::abs(cv::determinant(cv::Mat(matrix))) > 1e-12;
}

bool finiteMat(const cv::Mat& matrix)
{
    return !matrix.empty() && matrix.dims == 2 &&
        cv::checkRange(matrix, true, nullptr);
}

std::filesystem::path siblingPath(
    const std::filesystem::path& target, std::string_view suffix)
{
    return target.parent_path() /
        (target.filename().string() + std::string(suffix));
}

vision::core::Status writeCandidate(
    const std::filesystem::path& source,
    const std::filesystem::path& candidate,
    const cv::Matx33d& matrix)
{
    try {
        cv::FileStorage input(source.string(), cv::FileStorage::READ);
        if (!input.isOpened()) {
            return invalidFile("Could not open calibration resource: " +
                               source.string());
        }
        int width = 0;
        int height = 0;
        cv::Mat cameraMatrix;
        cv::Mat distortion;
        cv::Mat newCameraMatrix;
        input["image_width"] >> width;
        input["image_height"] >> height;
        input["camera_matrix"] >> cameraMatrix;
        input["distortion_coefficients"] >> distortion;
        input["new_camera_matrix"] >> newCameraMatrix;
        input.release();
        if (width <= 0 || height <= 0 || cameraMatrix.rows != 3 ||
            cameraMatrix.cols != 3 || !finiteMat(cameraMatrix) ||
            !finiteMat(distortion) || newCameraMatrix.rows != 3 ||
            newCameraMatrix.cols != 3 || !finiteMat(newCameraMatrix)) {
            return invalidFile(
                "Existing calibration resource is incomplete: " +
                source.string());
        }

        cv::FileStorage output(
            candidate.string(),
            cv::FileStorage::WRITE | cv::FileStorage::FORMAT_YAML);
        if (!output.isOpened()) {
            return fileFailure(
                "The calibration candidate file could not be created.",
                candidate.string());
        }
        output << "image_width" << width;
        output << "image_height" << height;
        output << "camera_matrix" << cameraMatrix;
        output << "distortion_coefficients" << distortion;
        output << "new_camera_matrix" << newCameraMatrix;
        output << "image_to_physical" << cv::Mat(matrix);
        output.release();

        cv::FileStorage verify(candidate.string(), cv::FileStorage::READ);
        if (!verify.isOpened()) {
            return fileFailure(
                "The calibration candidate file could not be verified.",
                candidate.string());
        }
        int savedWidth = 0;
        int savedHeight = 0;
        cv::Mat savedCamera;
        cv::Mat savedDistortion;
        cv::Mat savedNewCamera;
        cv::Mat saved;
        verify["image_width"] >> savedWidth;
        verify["image_height"] >> savedHeight;
        verify["camera_matrix"] >> savedCamera;
        verify["distortion_coefficients"] >> savedDistortion;
        verify["new_camera_matrix"] >> savedNewCamera;
        verify["image_to_physical"] >> saved;
        if (savedWidth != width || savedHeight != height ||
            savedCamera.rows != 3 || savedCamera.cols != 3 ||
            !finiteMat(savedCamera) || !finiteMat(savedDistortion) ||
            savedNewCamera.rows != 3 || savedNewCamera.cols != 3 ||
            !finiteMat(savedNewCamera) || saved.rows != 3 ||
            saved.cols != 3 || !finiteMat(saved)) {
            return fileFailure(
                "The calibration candidate file could not be verified.",
                candidate.string());
        }
        cv::Mat saved64;
        saved.convertTo(saved64, CV_64F);
        if (std::abs(cv::determinant(saved64)) <= 1e-12) {
            return fileFailure(
                "The calibration candidate file could not be verified.",
                candidate.string() + ": singular image_to_physical");
        }
        return std::monostate{};
    } catch (const cv::Exception& exception) {
        return fileFailure(
            "The calibration candidate file could not be written.",
            source.string() + ": " + exception.what());
    } catch (const std::exception& exception) {
        return fileFailure(
            "The calibration candidate file could not be written.",
            source.string() + ": " + exception.what());
    } catch (...) {
        return fileFailure(
            "The calibration candidate file could not be written.",
            source.string() + ": unknown exception");
    }
}

} // namespace

CalibrationPointFileOutcome loadCalibrationPointFile(
    const std::filesystem::path& path)
{
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return invalidFile("Could not open point file: " + path.string());
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        std::string content = buffer.str();
        if (content.size() >= 3U &&
            static_cast<unsigned char>(content[0]) == 0xEFU &&
            static_cast<unsigned char>(content[1]) == 0xBBU &&
            static_cast<unsigned char>(content[2]) == 0xBFU) {
            content.erase(0, 3);
        }

        vision::algorithm::PhysicalPointSet points;
        std::istringstream lines(content);
        std::string line;
        std::size_t lineNumber = 0U;
        while (std::getline(lines, line)) {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string_view value = trim(line);
            if (value.empty()) continue;
            const std::size_t comma = value.find(',');
            if (comma == std::string_view::npos ||
                value.find(',', comma + 1U) != std::string_view::npos) {
                return invalidFile(
                    "Line " + std::to_string(lineNumber) +
                    " must contain exactly two comma-separated values.");
            }
            double x = 0.0;
            double y = 0.0;
            if (!parseDouble(value.substr(0, comma), x) ||
                !parseDouble(value.substr(comma + 1U), y)) {
                return invalidFile(
                    "Line " + std::to_string(lineNumber) +
                    " contains an invalid or non-finite coordinate.");
            }
            points.emplace_back(x, y);
        }

        const double root = std::sqrt(static_cast<double>(points.size()));
        const int side = static_cast<int>(std::llround(root));
        if (side < 3 || side > 15 ||
            static_cast<std::size_t>(side * side) != points.size()) {
            return invalidFile(
                "Point count must be n*n for n in [3, 15]; actualCount=" +
                std::to_string(points.size()));
        }
        if (duplicatePoints(points)) {
            return invalidFile("The point file contains duplicate coordinates.");
        }
        if (allCollinear(points)) {
            return invalidFile("The point file coordinates are collinear.");
        }
        return CalibrationPointFile{path, side, std::move(points)};
    } catch (const std::exception& exception) {
        return fileFailure(
            "The calibration point file could not be read.",
            path.string() + ": " + exception.what());
    } catch (...) {
        return fileFailure(
            "The calibration point file could not be read.",
            path.string() + ": unknown exception");
    }
}

CalibrationOrientationOutcome inferCalibrationOrientation(
    int side, std::size_t p1, std::size_t p2)
{
    if (side < 3 || side > 15) {
        return invalidFile("Grid side must be in [3, 15].");
    }
    const std::size_t n = static_cast<std::size_t>(side);
    const std::size_t last = n - 1U;
    const std::size_t bottomLeft = last * n;
    const std::size_t bottomRight = n * n - 1U;
    if (p1 == 0U && p2 == 1U)
        return CalibrationGridOrientation::P1TopLeftXRight;
    if (p1 == 0U && p2 == n)
        return CalibrationGridOrientation::P1TopLeftXDown;
    if (p1 == last && p2 == last - 1U)
        return CalibrationGridOrientation::P1TopRightXLeft;
    if (p1 == last && p2 == last + n)
        return CalibrationGridOrientation::P1TopRightXDown;
    if (p1 == bottomRight && p2 == bottomRight - 1U)
        return CalibrationGridOrientation::P1BottomRightXLeft;
    if (p1 == bottomRight && p2 == bottomRight - n)
        return CalibrationGridOrientation::P1BottomRightXUp;
    if (p1 == bottomLeft && p2 == bottomLeft + 1U)
        return CalibrationGridOrientation::P1BottomLeftXRight;
    if (p1 == bottomLeft && p2 == bottomLeft - n)
        return CalibrationGridOrientation::P1BottomLeftXUp;
    return invalidFile(
        "P1 must be a grid corner and P2 must be one of its two direct neighbours.");
}

CalibrationFileTransaction::CalibrationFileTransaction(
    std::vector<Entry> entries)
    : entries_(std::move(entries))
{
}

CalibrationFileTransaction::CalibrationFileTransaction(
    CalibrationFileTransaction&& other) noexcept
    : entries_(std::move(other.entries_)), finished_(other.finished_)
{
    other.finished_ = true;
}

CalibrationFileTransaction& CalibrationFileTransaction::operator=(
    CalibrationFileTransaction&& other) noexcept
{
    if (this == &other) return *this;
    if (!finished_) static_cast<void>(rollback());
    entries_ = std::move(other.entries_);
    finished_ = other.finished_;
    other.finished_ = true;
    return *this;
}

CalibrationFileTransaction::~CalibrationFileTransaction() noexcept
{
    if (!finished_) static_cast<void>(rollback());
}

CalibrationFileTransaction::CreateOutcome
CalibrationFileTransaction::stageAndReplace(
    const std::filesystem::path& algorithmMainConfig,
    const MatrixMap& matrices)
{
    if (matrices.empty()) {
        return invalidFile("No calibration matrices were supplied.");
    }
    try {
        cv::FileStorage main(
            algorithmMainConfig.string(), cv::FileStorage::READ);
        if (!main.isOpened()) {
            return invalidFile(
                "Could not open algorithm main configuration: " +
                algorithmMainConfig.string());
        }
        const cv::FileNode calibration = main["calibration"];
        if (calibration.empty() || !calibration.isMap()) {
            return invalidFile(
                "Algorithm main configuration has no calibration map.");
        }

        std::vector<Entry> entries;
        entries.reserve(matrices.size());
        std::set<std::filesystem::path> targets;
        const std::filesystem::path base =
            std::filesystem::absolute(algorithmMainConfig).parent_path();
        for (const auto& [role, matrix] : matrices) {
            const char* nodeName = roleNode(role);
            if (nodeName == nullptr || !finiteMatrix(matrix)) {
                return invalidFile(
                    "A role or image_to_physical matrix is invalid.");
            }
            std::string configuredPath;
            calibration[nodeName] >> configuredPath;
            if (configuredPath.empty()) {
                return invalidFile(
                    std::string("Missing calibration path for role ") +
                    nodeName + '.');
            }
            std::filesystem::path target(configuredPath);
            if (target.is_relative()) target = base / target;
            target = std::filesystem::absolute(target).lexically_normal();
            if (!targets.insert(target).second) {
                return invalidFile(
                    "Calibration roles must reference distinct target files.");
            }
            Entry entry{
                target,
                siblingPath(target, ".candidate.tmp"),
                siblingPath(target, ".calibration.bak")};
            std::error_code error;
            std::filesystem::remove(entry.candidate, error);
            error.clear();
            std::filesystem::remove(entry.backup, error);
            auto write = writeCandidate(target, entry.candidate, matrix);
            if (const auto* failure = std::get_if<Error>(&write)) {
                std::filesystem::remove(entry.candidate, error);
                for (const Entry& created : entries) {
                    std::filesystem::remove(created.candidate, error);
                }
                return *failure;
            }
            entries.push_back(std::move(entry));
        }

        for (const Entry& entry : entries) {
            std::error_code error;
            std::filesystem::copy_file(
                entry.target, entry.backup,
                std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                CalibrationFileTransaction cleanup(std::move(entries));
                cleanup.finished_ = true;
                cleanup.cleanupCandidates();
                cleanup.cleanupBackups();
                return fileFailure(
                    "The existing calibration file could not be backed up.",
                    entry.target.string() + ": " + error.message());
            }
        }
        for (const Entry& entry : entries) {
            std::error_code error;
            std::filesystem::copy_file(
                entry.candidate, entry.target,
                std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                CalibrationFileTransaction transaction(std::move(entries));
                const auto rollbackStatus = transaction.rollback();
                std::string diagnostic = entry.target.string() + ": " +
                    error.message();
                if (const auto* rollbackError =
                        std::get_if<Error>(&rollbackStatus)) {
                    diagnostic += "; rollback failed: " +
                        rollbackError->diagnostic;
                }
                return fileFailure(
                    "The calibration files could not be replaced.",
                    std::move(diagnostic));
            }
        }
        return CalibrationFileTransaction(std::move(entries));
    } catch (const cv::Exception& exception) {
        return fileFailure(
            "The calibration file transaction could not be prepared.",
            exception.what());
    } catch (const std::exception& exception) {
        return fileFailure(
            "The calibration file transaction could not be prepared.",
            exception.what());
    } catch (...) {
        return fileFailure(
            "The calibration file transaction could not be prepared.",
            "unknown exception");
    }
}

vision::core::Status CalibrationFileTransaction::rollback() noexcept
{
    if (finished_) return std::monostate{};
    std::string failures;
    for (const Entry& entry : entries_) {
        std::error_code error;
        std::filesystem::copy_file(
            entry.backup, entry.target,
            std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            if (!failures.empty()) failures += "; ";
            failures += entry.target.string() + ": " + error.message();
        }
    }
    cleanupCandidates();
    if (failures.empty()) cleanupBackups();
    finished_ = true;
    if (!failures.empty()) {
        return fileFailure(
            "The previous calibration files could not be restored.",
            std::move(failures));
    }
    return std::monostate{};
}

vision::core::Status CalibrationFileTransaction::commit() noexcept
{
    if (finished_) return std::monostate{};
    cleanupCandidates();
    cleanupBackups();
    finished_ = true;
    return std::monostate{};
}

void CalibrationFileTransaction::cleanupCandidates() noexcept
{
    for (const Entry& entry : entries_) {
        std::error_code error;
        std::filesystem::remove(entry.candidate, error);
    }
}

void CalibrationFileTransaction::cleanupBackups() noexcept
{
    for (const Entry& entry : entries_) {
        std::error_code error;
        std::filesystem::remove(entry.backup, error);
    }
}

} // namespace vision::app
