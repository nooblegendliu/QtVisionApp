#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace vision::app {

class ApplicationController;
class MainWindow;
class MonitorWindow;

class WindowCoordinator final : public QObject {
public:
    WindowCoordinator(
        ApplicationController* controller,
        QString backendName,
        QObject* parent = nullptr);
    ~WindowCoordinator() override;

    void showInitialWindow();

private:
    void showMonitorWindow();
    void showDetailedWindow();
    void requestApplicationExit();

    ApplicationController* controller_{nullptr};
    std::unique_ptr<MainWindow> mainWindow_;
    std::unique_ptr<MonitorWindow> monitorWindow_;
    bool shutdownRequested_{false};
};

} // namespace vision::app
