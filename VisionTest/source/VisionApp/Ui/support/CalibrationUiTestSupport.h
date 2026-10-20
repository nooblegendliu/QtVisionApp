#pragma once

#include "VisionApp/Calibration/support/CalibrationRuntime.h"
#include "ApplicationController.h"
#include "CalibrationImageView.h"
#include "CalibrationWorkspace.h"
#include "MainWindow.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class QPushButton;

namespace vision::test::app {

// Test-owned composition around unchanged production widgets and controller.
class CalibrationUiSession final {
public:
    explicit CalibrationUiSession(const QString& name);
    ~CalibrationUiSession();
    void start();
    void prepareInput();
    void switchMode(vision::app::CalibrationMode mode);
    void selectRole(vision::core::CameraRole role);
    const char* modeButton(const char* centerName, const char* cornerName) const;
    vision::app::CalibrationMode mode{vision::app::CalibrationMode::Center};
    vision::core::CameraRole activeRole{vision::core::CameraRole::Center};
    void detect(bool complete = true);
    void clickButton(const char* name);
    void clickGrid(int row, int column);
    void clickViewport(QPoint position);
    void drag(QPoint start, QPoint delta);
    void zoom(int angleDelta);
    void waitFor(const std::function<bool()>& predicate, const char* operation);
    int detectionIndex(int row, int column) const;
    QPoint gridViewport(int row, int column) const;
    bool hasLabel(const QString& text) const;
    QPushButton* button(const char* name) const;
    const vision::app::CalibrationRoleSnapshot& center() const;
    void saveScreenshot(const QString& fileName);
    void record(const QString& event);
    void closeAndWait();
    void runInteractive();

    std::shared_ptr<CalibrationUiRuntimeState> runtime;
    vision::app::CalibrationSnapshot snapshot;
    int snapshotRevision{0};
    bool stopped{false};
    std::unique_ptr<vision::app::ApplicationController> controller;
    std::unique_ptr<vision::app::MainWindow> window;
    vision::app::CalibrationWorkspace* workspace{nullptr};
    vision::app::CalibrationImageView* view{nullptr};
    std::unique_ptr<QSignalSpy> pointClicks;
    QString outputDirectory;

private:
    QTemporaryDir resources_;
    QFile journal_;
    QString pointFile_;
    bool started_{false};
};

} // namespace vision::test::app
