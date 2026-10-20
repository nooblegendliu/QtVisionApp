#pragma once

#include "AppConfig.h"
#include "AppTypes.h"

#include <QMainWindow>

#include <array>
#include <memory>

class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace Ui {
class MainWindow;
}

namespace vision::app {

class ApplicationController;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(
        ApplicationController* controller,
        QString backendName,
        QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] bool hasUnsavedChanges() const noexcept;

signals:
    void monitorModeRequested();
    void applicationExitRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void initializeTables();
    void connectUi();
    void connectController();
    void loadDraft(const ApplicationConfig& configuration);
    [[nodiscard]] ApplicationConfig collectDraft() const;
    void markDirty();
    void updateApplyActions();
    void applyDraft();
    void updateSnapshot(const ApplicationSnapshot& snapshot);
    void updatePresentation(const TaskPresentationSnapshot& presentation);
    void clearPresentation(const QString& message);
    void appendDiagnostic(const QString& message);
    void saveUiState();

    ApplicationController* controller_{nullptr};
    std::unique_ptr<Ui::MainWindow> ui_;
    QString backendName_;
    ApplicationConfig draft_;
    ApplicationSnapshot snapshot_;
    bool loading_{false};
    bool dirty_{false};
    int dirtyCount_{0};
    QString dirtySummary_;
    TaskState previousTaskState_{TaskState::Idle};

    std::array<QLineEdit*, 5> cameraSerials_{};
    std::array<QDoubleSpinBox*, 5> cameraExposures_{};
    std::array<QDoubleSpinBox*, 5> cameraGains_{};
    std::array<QLineEdit*, 4> lightSelectors_{};
    std::array<QDoubleSpinBox*, 4> lightIntensities_{};
};

} // namespace vision::app
