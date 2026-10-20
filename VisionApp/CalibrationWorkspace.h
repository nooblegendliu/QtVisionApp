#pragma once

#include "AppTypes.h"

#include <QMetaType>
#include <QList>
#include <QString>
#include <QWidget>

#include <array>
#include <memory>

namespace Ui {
class CalibrationWorkspace;
}

namespace vision::app {

enum class CalibrationUiStage {
    NoInput,
    InputSelected,
    Capturing,
    ImageCaptured,
    Detecting,
    SelectingAnchors,
    PointsDetected,
    Solving,
    CandidateReady,
    Validating,
    ReadyToApply,
    Applying,
    Completed,
    Failed
};

class CalibrationWorkspace final : public QWidget {
    Q_OBJECT

public:
    explicit CalibrationWorkspace(QWidget* parent = nullptr);
    ~CalibrationWorkspace() override;

    void setBackendAvailable(bool available);
    [[nodiscard]] bool backendAvailable() const noexcept;
    void setStage(
        CalibrationMode mode,
        CalibrationUiStage stage,
        QString message = {});
    void applySnapshot(const CalibrationSnapshot& snapshot);

signals:
    void theoreticalPointFileSelected(
        vision::app::CalibrationMode mode,
        vision::core::CameraRole role,
        const QString& path);
    void captureRequested(vision::app::CalibrationMode mode);
    void detectionRequested(vision::app::CalibrationMode mode);
    void solveRequested(vision::app::CalibrationMode mode);
    void validationRequested(vision::app::CalibrationMode mode);
    void applyRequested(vision::app::CalibrationMode mode);
    void resetRequested(vision::app::CalibrationMode mode);
    void calibrationPointSelected(
        vision::core::CameraRole role, int detectionIndex);

private:
    struct PageState {
        CalibrationUiStage stage{CalibrationUiStage::NoInput};
        QString theoreticalPointPath;
        std::array<QString, 4> cornerPointPaths;
        QString message;
    };

    void initializeTables();
    void initializeImageViews();
    void initializeLayout();
    void connectUi();
    void restoreUiState();
    void saveUiState() const;
    void toggleDataPanel(CalibrationMode mode);
    void refreshImageLayout(CalibrationMode mode);
    void chooseTheoreticalPointFile(
        CalibrationMode mode, vision::core::CameraRole role);
    void resetPage(CalibrationMode mode, bool notify);
    void clearResults(CalibrationMode mode);
    void updatePage(CalibrationMode mode);
    void updateStepPresentation(CalibrationMode mode);
    void updateActionAvailability(CalibrationMode mode);
    [[nodiscard]] PageState& pageState(CalibrationMode mode);
    [[nodiscard]] const PageState& pageState(CalibrationMode mode) const;

    std::unique_ptr<Ui::CalibrationWorkspace> ui_;
    std::array<PageState, 2> pageStates_{};
    std::array<QImage, 5> displayedImages_{};
    QList<int> centerExpandedSizes_{700, 300};
    QList<int> cornerExpandedSizes_{700, 300};
    bool centerDataCollapsed_{false};
    bool cornerDataCollapsed_{false};
    bool backendAvailable_{false};
    bool maintenanceLocked_{false};
};

} // namespace vision::app

Q_DECLARE_METATYPE(vision::app::CalibrationUiStage)
