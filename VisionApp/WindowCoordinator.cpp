#include "WindowCoordinator.h"

#include "ApplicationController.h"
#include "MainWindow.h"
#include "MonitorWindow.h"

#include <QMessageBox>
#include <QWidget>

#include <utility>

namespace vision::app {

WindowCoordinator::WindowCoordinator(
    ApplicationController* controller,
    QString backendName,
    QObject* parent)
    : QObject(parent),
      controller_(controller),
      mainWindow_(std::make_unique<MainWindow>(
          controller, std::move(backendName))),
      monitorWindow_(std::make_unique<MonitorWindow>(controller))
{
    connect(mainWindow_.get(), &MainWindow::monitorModeRequested,
            this, &WindowCoordinator::showMonitorWindow);
    connect(mainWindow_.get(), &MainWindow::applicationExitRequested,
            this, &WindowCoordinator::requestApplicationExit);
    connect(monitorWindow_.get(), &MonitorWindow::detailedModeRequested,
            this, &WindowCoordinator::showDetailedWindow);
    connect(monitorWindow_.get(), &MonitorWindow::applicationExitRequested,
            this, &WindowCoordinator::requestApplicationExit);
}

WindowCoordinator::~WindowCoordinator() = default;

void WindowCoordinator::showInitialWindow()
{
    showDetailedWindow();
}

void WindowCoordinator::showMonitorWindow()
{
    if (shutdownRequested_) return;

    monitorWindow_->show();
    monitorWindow_->raise();
    monitorWindow_->activateWindow();
    mainWindow_->hide();
}

void WindowCoordinator::showDetailedWindow()
{
    if (shutdownRequested_) return;

    mainWindow_->show();
    mainWindow_->raise();
    mainWindow_->activateWindow();
    monitorWindow_->hide();
}

void WindowCoordinator::requestApplicationExit()
{
    if (shutdownRequested_) return;

    QWidget* dialogParent = monitorWindow_->isVisible()
        ? static_cast<QWidget*>(monitorWindow_.get())
        : static_cast<QWidget*>(mainWindow_.get());
    if (mainWindow_->hasUnsavedChanges()) {
        const auto answer = QMessageBox::question(
            dialogParent,
            QStringLiteral("未应用配置"),
            QStringLiteral("存在未应用的配置修改。放弃修改并关闭软件？"));
        if (answer != QMessageBox::Yes) return;
    }

    shutdownRequested_ = true;
    mainWindow_->setEnabled(false);
    monitorWindow_->setEnabled(false);
    controller_->shutdown();
}

} // namespace vision::app
