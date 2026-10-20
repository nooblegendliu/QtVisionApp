#pragma once

#include <VisionAlgorithm/Calibration.h>
#include <VisionCore/Error.h>

#include <filesystem>
#include <map>
#include <vector>

namespace vision::app {

struct CalibrationPointFile {
    std::filesystem::path path;
    int gridSidePoints{0};
    vision::algorithm::PhysicalPointSet points;
};

using CalibrationPointFileOutcome =
    vision::core::Outcome<CalibrationPointFile>;
using CalibrationOrientationOutcome =
    vision::core::Outcome<vision::algorithm::CalibrationGridOrientation>;

[[nodiscard]] CalibrationPointFileOutcome loadCalibrationPointFile(
    const std::filesystem::path& path);

[[nodiscard]] CalibrationOrientationOutcome inferCalibrationOrientation(
    int gridSidePoints,
    std::size_t p1DetectionIndex,
    std::size_t p2DetectionIndex);

class CalibrationFileTransaction final {
public:
    using MatrixMap = std::map<
        vision::core::CameraRole, cv::Matx33d>;
    using CreateOutcome =
        vision::core::Outcome<CalibrationFileTransaction>;

    [[nodiscard]] static CreateOutcome stageAndReplace(
        const std::filesystem::path& algorithmMainConfig,
        const MatrixMap& matrices);

    ~CalibrationFileTransaction() noexcept;
    CalibrationFileTransaction(CalibrationFileTransaction&& other) noexcept;
    CalibrationFileTransaction& operator=(
        CalibrationFileTransaction&& other) noexcept;
    CalibrationFileTransaction(const CalibrationFileTransaction&) = delete;
    CalibrationFileTransaction& operator=(
        const CalibrationFileTransaction&) = delete;

    [[nodiscard]] vision::core::Status rollback() noexcept;
    [[nodiscard]] vision::core::Status commit() noexcept;

private:
    struct Entry {
        std::filesystem::path target;
        std::filesystem::path candidate;
        std::filesystem::path backup;
    };

    explicit CalibrationFileTransaction(std::vector<Entry> entries);
    void cleanupCandidates() noexcept;
    void cleanupBackups() noexcept;

    std::vector<Entry> entries_;
    bool finished_{false};
};

} // namespace vision::app
