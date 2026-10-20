#include "MonitorWindow.h"

#include "ApplicationController.h"
#include "ImageView.h"
#include "ui_MonitorWindow.h"

#include <QCloseEvent>
#include <QColor>
#include <QPushButton>

#include <array>

namespace vision::app {
namespace {

QString cameraRoleText(vision::core::CameraRole role)
{
    switch (role) {
    case vision::core::CameraRole::Center:
        return QStringLiteral("Center");
    case vision::core::CameraRole::TopLeft:
        return QStringLiteral("TopLeft");
    case vision::core::CameraRole::TopRight:
        return QStringLiteral("TopRight");
    case vision::core::CameraRole::BottomLeft:
        return QStringLiteral("BottomLeft");
    case vision::core::CameraRole::BottomRight:
        return QStringLiteral("BottomRight");
    case vision::core::CameraRole::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QColor outcomeColor(const QString& outcome)
{
    if (outcome == QStringLiteral("OK")) {
        return QColor(0, 128, 0);
    }
    if (outcome == QStringLiteral("NG")) {
        return QColor(180, 0, 0);
    }
    return QColor(184, 92, 0);
}

} // namespace

MonitorWindow::MonitorWindow(
    ApplicationController* controller,
    QWidget* parent)
    : QMainWindow(parent),
      controller_(controller),
      ui_(std::make_unique<Ui::MonitorWindow>())
{
    ui_->setupUi(this);

    connect(ui_->returnDetailedButton, &QPushButton::clicked,
            this, &MonitorWindow::detailedModeRequested);
    connect(controller_, &ApplicationController::snapshotChanged,
            this, [this](const ApplicationSnapshot& value) {
                updateSnapshot(value);
            });
    connect(controller_, &ApplicationController::taskPresentationChanged,
            this, [this](const TaskPresentationSnapshot& value) {
                updatePresentation(value);
            });

    updateSnapshot(controller_->snapshot());
    clearPresentation(QStringLiteral("等待定位任务"));
}

MonitorWindow::~MonitorWindow() = default;

void MonitorWindow::updateSnapshot(const ApplicationSnapshot& snapshot)
{
    previousTaskState_ = snapshot_.taskState;
    snapshot_ = snapshot;
    if (snapshot.taskState == TaskState::Preparing &&
        previousTaskState_ != TaskState::Preparing) {
        clearPresentation(QStringLiteral("等待采集"));
    }
}

void MonitorWindow::updatePresentation(
    const TaskPresentationSnapshot& presentation)
{
    const auto& result = presentation.result;
    const QColor color = outcomeColor(result.outcome);

    if (result.mode == vision::core::LocateMode::Edge) {
        ui_->monitorImageStack->setCurrentWidget(ui_->edgeMonitorPage);
        ui_->edgeMonitorView->clearImage(QStringLiteral("未取得 Center 图像"));
        for (const auto& image : presentation.images) {
            if (image.role == vision::core::CameraRole::Center) {
                ui_->edgeMonitorView->setImage(image.image);
                break;
            }
        }
        ui_->edgeMonitorView->setOverlay(
            QStringLiteral("Center"), result.outcome, color);
        return;
    }

    ui_->monitorImageStack->setCurrentWidget(ui_->markMonitorPage);
    const std::array<vision::core::CameraRole, 4> roles{
        vision::core::CameraRole::TopLeft,
        vision::core::CameraRole::TopRight,
        vision::core::CameraRole::BottomLeft,
        vision::core::CameraRole::BottomRight};
    const std::array<ImageView*, 4> views{
        ui_->topLeftMonitorView,
        ui_->topRightMonitorView,
        ui_->bottomLeftMonitorView,
        ui_->bottomRightMonitorView};

    for (std::size_t index = 0; index < roles.size(); ++index) {
        views[index]->clearImage(QStringLiteral("未取得图像"));
        for (const auto& image : presentation.images) {
            if (image.role == roles[index]) {
                views[index]->setImage(image.image);
                break;
            }
        }
        views[index]->setOverlay(
            cameraRoleText(roles[index]), result.outcome, color);
    }
}

void MonitorWindow::clearPresentation(const QString& message)
{
    ui_->monitorImageStack->setCurrentWidget(ui_->edgeMonitorPage);
    ui_->edgeMonitorView->clearImage(message);
    for (ImageView* view : {
             ui_->topLeftMonitorView,
             ui_->topRightMonitorView,
             ui_->bottomLeftMonitorView,
             ui_->bottomRightMonitorView}) {
        view->clearImage(message);
    }
}

void MonitorWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    emit applicationExitRequested();
}

} // namespace vision::app
