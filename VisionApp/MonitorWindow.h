#pragma once

#include "AppTypes.h"

#include <QMainWindow>

#include <memory>

class QCloseEvent;
class QWidget;

namespace Ui {
class MonitorWindow;
}

namespace vision::app {

class ApplicationController;

class MonitorWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MonitorWindow(
        ApplicationController* controller,
        QWidget* parent = nullptr);
    ~MonitorWindow() override;

signals:
    void detailedModeRequested();
    void applicationExitRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void updateSnapshot(const ApplicationSnapshot& snapshot);
    void updatePresentation(const TaskPresentationSnapshot& presentation);
    void clearPresentation(const QString& message);

    ApplicationController* controller_{nullptr};
    std::unique_ptr<Ui::MonitorWindow> ui_;
    ApplicationSnapshot snapshot_;
    TaskState previousTaskState_{TaskState::Idle};
};

} // namespace vision::app
