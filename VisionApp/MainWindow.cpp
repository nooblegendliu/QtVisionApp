#include "MainWindow.h"

#include "ApplicationController.h"
#include "CalibrationWorkspace.h"
#include "ImageView.h"
#include "ui_MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <utility>
#include <variant>

namespace vision::app {
namespace {

constexpr std::array<vision::core::CameraRole, 5> cameraRoleOrder{
    vision::core::CameraRole::Center,
    vision::core::CameraRole::TopLeft,
    vision::core::CameraRole::TopRight,
    vision::core::CameraRole::BottomRight,
    vision::core::CameraRole::BottomLeft};

constexpr std::array<vision::hardware::HikLightChannelRole, 4> lightRoleOrder{
    vision::hardware::HikLightChannelRole::WhiteBacklight,
    vision::hardware::HikLightChannelRole::CoaxialRed,
    vision::hardware::HikLightChannelRole::CoaxialGreen,
    vision::hardware::HikLightChannelRole::CoaxialBlue};

QString cameraRoleText(vision::core::CameraRole role)
{
    switch (role) {
    case vision::core::CameraRole::Center: return QStringLiteral("Center");
    case vision::core::CameraRole::TopLeft: return QStringLiteral("TopLeft");
    case vision::core::CameraRole::TopRight: return QStringLiteral("TopRight");
    case vision::core::CameraRole::BottomRight: return QStringLiteral("BottomRight");
    case vision::core::CameraRole::BottomLeft: return QStringLiteral("BottomLeft");
    default: return QStringLiteral("Unknown");
    }
}

QString cameraModelText(vision::hardware::HikCameraModel model)
{
    switch (model) {
    case vision::hardware::HikCameraModel::MvCh25090Gm:
        return QStringLiteral("MV-CH250-90GM");
    case vision::hardware::HikCameraModel::MvCs12010Gm:
        return QStringLiteral("MV-CS120-10GM");
    }
    return QStringLiteral("Unknown");
}

QString lightRoleText(vision::hardware::HikLightChannelRole role)
{
    switch (role) {
    case vision::hardware::HikLightChannelRole::WhiteBacklight:
        return QStringLiteral("WhiteBacklight");
    case vision::hardware::HikLightChannelRole::CoaxialRed:
        return QStringLiteral("CoaxialRed");
    case vision::hardware::HikLightChannelRole::CoaxialGreen:
        return QStringLiteral("CoaxialGreen");
    case vision::hardware::HikLightChannelRole::CoaxialBlue:
        return QStringLiteral("CoaxialBlue");
    }
    return QStringLiteral("Unknown");
}

QString locateModeText(vision::core::LocateMode mode)
{
    switch (mode) {
    case vision::core::LocateMode::Edge: return QStringLiteral("Edge");
    case vision::core::LocateMode::MarkPoint: return QStringLiteral("MarkPoint");
    }
    return QStringLiteral("Unknown");
}

QString nativeText(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::filesystem::path nativePath(const QString& text)
{
#ifdef _WIN32
    return std::filesystem::path(text.toStdWString());
#else
    return std::filesystem::path(text.toStdString());
#endif
}

void normalizeRoleOrder(ApplicationConfig& configuration)
{
    const auto cameras = configuration.vision.cameras;
    for (std::size_t target = 0; target < cameraRoleOrder.size(); ++target) {
        const auto found = std::find_if(
            cameras.begin(), cameras.end(),
            [target](const auto& camera) {
                return camera.role == cameraRoleOrder[target];
            });
        if (found != cameras.end()) {
            configuration.vision.cameras[target] = *found;
        }
    }

    const auto channels = configuration.vision.lightController.channels;
    for (std::size_t target = 0; target < lightRoleOrder.size(); ++target) {
        const auto found = std::find_if(
            channels.begin(), channels.end(),
            [target](const auto& channel) {
                return channel.role == lightRoleOrder[target];
            });
        if (found != channels.end()) {
            configuration.vision.lightController.channels[target] = *found;
        }
    }
}

struct DraftChangeSummary {
    int count{0};
    QStringList categories;
};

DraftChangeSummary summarizeChanges(
    const ApplicationConfig& active,
    const ApplicationConfig& candidate)
{
    DraftChangeSummary summary;
    const auto record = [&summary](bool changed, const QString& category) {
        if (!changed) return;
        ++summary.count;
        if (!summary.categories.contains(category)) {
            summary.categories.push_back(category);
        }
    };

    for (std::size_t index = 0; index < active.vision.cameras.size(); ++index) {
        const auto& before = active.vision.cameras[index];
        const auto& after = candidate.vision.cameras[index];
        record(before.serialNumber != after.serialNumber, QStringLiteral("相机"));
        record(before.exposureTime != after.exposureTime, QStringLiteral("相机"));
        record(before.gain != after.gain, QStringLiteral("相机"));
    }
    record(active.vision.captureTimeout != candidate.vision.captureTimeout,
           QStringLiteral("相机"));
    record(active.vision.productionTrigger.inputLine !=
               candidate.vision.productionTrigger.inputLine,
           QStringLiteral("触发"));
    record(active.vision.productionTrigger.activation !=
               candidate.vision.productionTrigger.activation,
           QStringLiteral("触发"));
    record(active.vision.productionTrigger.delay !=
               candidate.vision.productionTrigger.delay,
           QStringLiteral("触发"));
    record(active.vision.lightController.serialNumber !=
               candidate.vision.lightController.serialNumber,
           QStringLiteral("光源"));
    for (std::size_t index = 0;
         index < active.vision.lightController.channels.size(); ++index) {
        const auto& before = active.vision.lightController.channels[index];
        const auto& after = candidate.vision.lightController.channels[index];
        record(before.outputSelector != after.outputSelector,
               QStringLiteral("光源"));
        record(before.normalizedIntensity != after.normalizedIntensity,
               QStringLiteral("光源"));
    }
    record(active.vision.algorithmConfigPath !=
               candidate.vision.algorithmConfigPath,
           QStringLiteral("算法"));
    record(active.tcp.listenAddress != candidate.tcp.listenAddress,
           QStringLiteral("TCP"));
    record(active.tcp.port != candidate.tcp.port, QStringLiteral("TCP"));
    record(active.tcp.heartbeatTimeout != candidate.tcp.heartbeatTimeout,
           QStringLiteral("TCP"));
    record(active.tcp.maximumFrameBytes != candidate.tcp.maximumFrameBytes,
           QStringLiteral("TCP"));
    record(active.resultDelivery.acknowledgementInterval !=
               candidate.resultDelivery.acknowledgementInterval,
           QStringLiteral("结果交付"));
    record(active.resultDelivery.maximumRetries !=
               candidate.resultDelivery.maximumRetries,
           QStringLiteral("结果交付"));
    record(active.logging.directory != candidate.logging.directory,
           QStringLiteral("日志"));
    record(active.logging.level != candidate.logging.level,
           QStringLiteral("日志"));
    record(active.logging.maximumFileBytes !=
               candidate.logging.maximumFileBytes,
           QStringLiteral("日志"));
    record(active.logging.maximumFiles != candidate.logging.maximumFiles,
           QStringLiteral("日志"));
    return summary;
}

QString applicationStateText(ApplicationState state)
{
    switch (state) {
    case ApplicationState::Starting: return QStringLiteral("启动中");
    case ApplicationState::Ready: return QStringLiteral("就绪");
    case ApplicationState::StartFailed: return QStringLiteral("启动失败");
    case ApplicationState::ShuttingDown: return QStringLiteral("关闭中");
    case ApplicationState::Stopped: return QStringLiteral("已停止");
    }
    return QStringLiteral("未知");
}

QString visionStateText(VisionServiceState state)
{
    switch (state) {
    case VisionServiceState::Starting: return QStringLiteral("初始化");
    case VisionServiceState::Ready: return QStringLiteral("就绪");
    case VisionServiceState::Faulted: return QStringLiteral("故障");
    case VisionServiceState::Recovering: return QStringLiteral("恢复中");
    case VisionServiceState::Reconfiguring: return QStringLiteral("配置中");
    case VisionServiceState::Stopped: return QStringLiteral("已停止");
    }
    return QStringLiteral("未知");
}

QString taskStateText(TaskState state)
{
    switch (state) {
    case TaskState::Idle: return QStringLiteral("空闲");
    case TaskState::Preparing: return QStringLiteral("准备采集");
    case TaskState::Running: return QStringLiteral("定位中");
    case TaskState::AwaitingResultAck: return QStringLiteral("等待结果确认");
    }
    return QStringLiteral("未知");
}

void setSemanticTextColor(QLabel* label, const QColor& color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

void resetTextColor(QLabel* label)
{
    label->setPalette(QApplication::palette());
}

} // namespace

MainWindow::MainWindow(
    ApplicationController* controller,
    QString backendName,
    QWidget* parent)
    : QMainWindow(parent),
      controller_(controller),
      ui_(std::make_unique<Ui::MainWindow>()),
      backendName_(std::move(backendName))
{
    ui_->setupUi(this);
    ui_->calibrationWorkspace->setBackendAvailable(false);
    initializeTables();
    connectUi();
    connectController();

    ui_->backendValue->setText(backendName_);
    ui_->configurationPathValue->setText(
        nativeText(controller_->configurationPath()));
    loadDraft(controller_->configuration());
    updateSnapshot(controller_->snapshot());
    clearPresentation(QStringLiteral("等待定位任务"));

    QSettings settings;
    restoreGeometry(
        settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
    ui_->monitorSplitter->setSizes({1000, 360});
    const QByteArray splitterState =
        settings.value(QStringLiteral("mainWindow/monitorSplitter")).toByteArray();
    if (!splitterState.isEmpty()) {
        ui_->monitorSplitter->restoreState(splitterState);
    }

    ui_->mainTabWidget->setCurrentWidget(ui_->monitorTab);
}

MainWindow::~MainWindow()
{
    saveUiState();
}

bool MainWindow::hasUnsavedChanges() const noexcept
{
    return dirty_;
}

void MainWindow::initializeTables()
{
    ui_->cameraTable->setHorizontalHeaderLabels({
        QStringLiteral("角色"), QStringLiteral("型号"),
        QStringLiteral("序列号"), QStringLiteral("曝光 (μs)"),
        QStringLiteral("增益")});
    ui_->cameraTable->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    ui_->cameraTable->verticalHeader()->setVisible(false);
    ui_->cameraTable->verticalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    ui_->cameraTable->setAlternatingRowColors(true);

    for (int row = 0; row < static_cast<int>(cameraRoleOrder.size()); ++row) {
        auto* roleItem = new QTableWidgetItem(
            cameraRoleText(cameraRoleOrder[static_cast<std::size_t>(row)]));
        roleItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ui_->cameraTable->setItem(row, 0, roleItem);

        auto* modelItem = new QTableWidgetItem;
        modelItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ui_->cameraTable->setItem(row, 1, modelItem);

        cameraSerials_[row] = new QLineEdit(ui_->cameraTable);
        cameraExposures_[row] = new QDoubleSpinBox(ui_->cameraTable);
        cameraExposures_[row]->setRange(0.001, 10000000.0);
        cameraExposures_[row]->setDecimals(3);
        cameraGains_[row] = new QDoubleSpinBox(ui_->cameraTable);
        cameraGains_[row]->setRange(-1000.0, 1000.0);
        cameraGains_[row]->setDecimals(3);
        ui_->cameraTable->setCellWidget(row, 2, cameraSerials_[row]);
        ui_->cameraTable->setCellWidget(row, 3, cameraExposures_[row]);
        ui_->cameraTable->setCellWidget(row, 4, cameraGains_[row]);
    }

    ui_->lightTable->setHorizontalHeaderLabels({
        QStringLiteral("逻辑角色"), QStringLiteral("物理输出"),
        QStringLiteral("亮度 0~1")});
    ui_->lightTable->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    ui_->lightTable->verticalHeader()->setVisible(false);
    ui_->lightTable->verticalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    ui_->lightTable->setAlternatingRowColors(true);

    for (int row = 0; row < static_cast<int>(lightRoleOrder.size()); ++row) {
        auto* roleItem = new QTableWidgetItem(
            lightRoleText(lightRoleOrder[static_cast<std::size_t>(row)]));
        roleItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ui_->lightTable->setItem(row, 0, roleItem);

        lightSelectors_[row] = new QLineEdit(ui_->lightTable);
        lightIntensities_[row] = new QDoubleSpinBox(ui_->lightTable);
        lightIntensities_[row]->setRange(0.0, 1.0);
        lightIntensities_[row]->setDecimals(3);
        lightIntensities_[row]->setSingleStep(0.01);
        ui_->lightTable->setCellWidget(row, 1, lightSelectors_[row]);
        ui_->lightTable->setCellWidget(row, 2, lightIntensities_[row]);
    }
}

void MainWindow::connectUi()
{
    for (std::size_t index = 0; index < cameraSerials_.size(); ++index) {
        connect(cameraSerials_[index], &QLineEdit::textChanged,
                this, [this] { markDirty(); });
        connect(cameraExposures_[index], &QDoubleSpinBox::valueChanged,
                this, [this] { markDirty(); });
        connect(cameraGains_[index], &QDoubleSpinBox::valueChanged,
                this, [this] { markDirty(); });
    }
    for (std::size_t index = 0; index < lightSelectors_.size(); ++index) {
        connect(lightSelectors_[index], &QLineEdit::textChanged,
                this, [this] { markDirty(); });
        connect(lightIntensities_[index], &QDoubleSpinBox::valueChanged,
                this, [this] { markDirty(); });
    }

    connect(ui_->captureTimeoutSpinBox, &QSpinBox::valueChanged,
            this, [this] { markDirty(); });
    connect(ui_->triggerLineComboBox, &QComboBox::currentIndexChanged,
            this, [this] { markDirty(); });
    connect(ui_->triggerActivationComboBox, &QComboBox::currentIndexChanged,
            this, [this] { markDirty(); });
    connect(ui_->triggerDelaySpinBox, &QDoubleSpinBox::valueChanged,
            this, [this] { markDirty(); });
    connect(ui_->lightSerialLineEdit, &QLineEdit::textChanged,
            this, [this] { markDirty(); });
    connect(ui_->algorithmPathLineEdit, &QLineEdit::textChanged,
            this, [this] { markDirty(); });
    connect(ui_->tcpAddressLineEdit, &QLineEdit::textChanged,
            this, [this] { markDirty(); });
    connect(ui_->tcpPortSpinBox, &QSpinBox::valueChanged,
            this, [this] { markDirty(); });
    connect(ui_->heartbeatTimeoutSpinBox, &QSpinBox::valueChanged,
            this, [this] { markDirty(); });
    connect(ui_->maximumFrameBytesSpinBox, &QSpinBox::valueChanged,
            this, [this] { markDirty(); });
    connect(ui_->acknowledgementIntervalSpinBox, &QSpinBox::valueChanged,
            this, [this] { markDirty(); });
    connect(ui_->maximumRetriesSpinBox, &QSpinBox::valueChanged,
            this, [this] { markDirty(); });
    connect(ui_->logDirectoryLineEdit, &QLineEdit::textChanged,
            this, [this] { markDirty(); });
    connect(ui_->logLevelComboBox, &QComboBox::currentIndexChanged,
            this, [this] { markDirty(); });
    connect(ui_->maximumFileMiBSpinBox, &QSpinBox::valueChanged,
            this, [this] { markDirty(); });
    connect(ui_->maximumFilesSpinBox, &QSpinBox::valueChanged,
            this, [this] { markDirty(); });

    connect(ui_->browseAlgorithmButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择算法主配置"),
            ui_->algorithmPathLineEdit->text(),
            QStringLiteral("YAML (*.yaml *.yml);;所有文件 (*.*)"));
        if (!path.isEmpty()) ui_->algorithmPathLineEdit->setText(path);
    });
    connect(ui_->validateAlgorithmButton, &QPushButton::clicked, this, [this] {
        ui_->validateAlgorithmButton->setEnabled(false);
        resetTextColor(ui_->algorithmValidationValue);
        ui_->algorithmValidationValue->setText(
            QStringLiteral("正在视觉线程校验资源…"));
        controller_->requestAlgorithmValidation(
            ui_->algorithmPathLineEdit->text().trimmed());
    });

    connect(ui_->applyConfigurationButton, &QPushButton::clicked,
            this, [this] { applyDraft(); });
    connect(ui_->revertConfigurationButton, &QPushButton::clicked,
            this, [this] { loadDraft(controller_->configuration()); });
    connect(ui_->clearDiagnosticButton, &QPushButton::clicked,
            ui_->diagnosticTextEdit, &QPlainTextEdit::clear);
    connect(ui_->mainTabWidget, &QTabWidget::currentChanged,
            this, [this] { updateApplyActions(); });

    connect(ui_->openMonitorButton, &QPushButton::clicked,
            this, &MainWindow::monitorModeRequested);
    connect(ui_->monitorWindowAction, &QAction::triggered,
            this, &MainWindow::monitorModeRequested);
    connect(ui_->exitAction, &QAction::triggered, this, &QWidget::close);

    connect(ui_->retryInitializationButton, &QPushButton::clicked,
            controller_, &ApplicationController::retryInitialization);
    connect(ui_->retryInitializationAction, &QAction::triggered,
            controller_, &ApplicationController::retryInitialization);
    connect(ui_->retryTcpButton, &QPushButton::clicked,
            controller_, &ApplicationController::retryTcp);
    connect(ui_->retryTcpAction, &QAction::triggered,
            controller_, &ApplicationController::retryTcp);
    connect(ui_->recoverVisionButton, &QPushButton::clicked,
            controller_, &ApplicationController::requestLocalRecovery);
    connect(ui_->recoverVisionAction, &QAction::triggered,
            controller_, &ApplicationController::requestLocalRecovery);

    connect(ui_->calibrationWorkspace,
            &CalibrationWorkspace::theoreticalPointFileSelected,
            controller_, &ApplicationController::requestCalibrationPointFile);
    connect(ui_->calibrationWorkspace,
            &CalibrationWorkspace::captureRequested,
            controller_, &ApplicationController::requestCalibrationCapture);
    connect(ui_->calibrationWorkspace,
            &CalibrationWorkspace::detectionRequested,
            this, [this](CalibrationMode) {
                controller_->requestCalibrationDetection();
            });
    connect(ui_->calibrationWorkspace,
            &CalibrationWorkspace::calibrationPointSelected,
            controller_,
            &ApplicationController::requestCalibrationPointSelection);
    connect(ui_->calibrationWorkspace,
            &CalibrationWorkspace::solveRequested,
            this, [this](CalibrationMode) {
                controller_->requestCalibrationSolve();
            });
    connect(ui_->calibrationWorkspace,
            &CalibrationWorkspace::validationRequested,
            this, [this](CalibrationMode) {
                controller_->requestCalibrationEvaluation();
            });
    connect(ui_->calibrationWorkspace,
            &CalibrationWorkspace::applyRequested,
            this, [this](CalibrationMode) {
                controller_->requestCalibrationApply();
            });
    connect(ui_->calibrationWorkspace,
            &CalibrationWorkspace::resetRequested,
            this, [this](CalibrationMode) {
                controller_->requestCalibrationReset();
            });
}

void MainWindow::connectController()
{
    connect(controller_, &ApplicationController::snapshotChanged,
            this, [this](const ApplicationSnapshot& value) {
                updateSnapshot(value);
            });
    connect(controller_, &ApplicationController::taskPresentationChanged,
            this, [this](const TaskPresentationSnapshot& value) {
                updatePresentation(value);
            });
    connect(controller_, &ApplicationController::configurationApplied,
            this, [this](const ApplicationConfig& value) {
                loadDraft(value);
            });
    connect(controller_, &ApplicationController::algorithmValidationFinished,
            this, [this](const AlgorithmValidationCompletion& completion) {
                ui_->validateAlgorithmButton->setEnabled(true);
                if (const auto* error =
                        std::get_if<vision::core::Error>(&completion.status)) {
                    setSemanticTextColor(
                        ui_->algorithmValidationValue, QColor(180, 0, 0));
                    ui_->algorithmValidationValue->setText(
                        QStringLiteral("校验失败：%1")
                            .arg(QString::fromStdString(error->message)));
                }
                else {
                    setSemanticTextColor(
                        ui_->algorithmValidationValue, QColor(0, 128, 0));
                    ui_->algorithmValidationValue->setText(
                        QStringLiteral("Edge/Mark 配置及引用资源校验通过"));
                }
            });
    connect(controller_, &ApplicationController::actionRejected,
            this, [this](const QString& reason) {
                ui_->validateAlgorithmButton->setEnabled(true);
                ui_->statusBar->showMessage(reason, 10000);
                QMessageBox::warning(
                    this, QStringLiteral("操作未完成"), reason);
            });
    connect(controller_, &ApplicationController::logMessage,
            this, [this](const QString& message) {
                appendDiagnostic(message);
            });
    connect(controller_, &ApplicationController::calibrationSnapshotChanged,
            ui_->calibrationWorkspace,
            &CalibrationWorkspace::applySnapshot);
}

void MainWindow::loadDraft(const ApplicationConfig& configuration)
{
    loading_ = true;
    draft_ = configuration;
    normalizeRoleOrder(draft_);

    for (std::size_t index = 0; index < draft_.vision.cameras.size(); ++index) {
        const auto& camera = draft_.vision.cameras[index];
        ui_->cameraTable->item(static_cast<int>(index), 0)->setText(
            cameraRoleText(camera.role));
        ui_->cameraTable->item(static_cast<int>(index), 1)->setText(
            cameraModelText(camera.model));
        cameraSerials_[index]->setText(
            QString::fromStdString(camera.serialNumber));
        cameraExposures_[index]->setValue(
            camera.exposureTime ? camera.exposureTime->count() : 1.0);
        cameraGains_[index]->setValue(camera.gain.value_or(0.0));
    }
    ui_->captureTimeoutSpinBox->setValue(
        static_cast<int>(draft_.vision.captureTimeout.count()));
    ui_->triggerLineComboBox->setCurrentIndex(
        draft_.vision.productionTrigger.inputLine ==
            vision::hardware::HikInputLine::Line0 ? 0 : 1);
    ui_->triggerActivationComboBox->setCurrentIndex(
        draft_.vision.productionTrigger.activation ==
            vision::hardware::HikTriggerActivation::RisingEdge ? 0 : 1);
    ui_->triggerDelaySpinBox->setValue(
        draft_.vision.productionTrigger.delay.count());

    ui_->lightSerialLineEdit->setText(QString::fromStdString(
        draft_.vision.lightController.serialNumber));
    for (std::size_t index = 0;
         index < draft_.vision.lightController.channels.size(); ++index) {
        const auto& channel = draft_.vision.lightController.channels[index];
        ui_->lightTable->item(static_cast<int>(index), 0)->setText(
            lightRoleText(channel.role));
        lightSelectors_[index]->setText(
            QString::fromStdString(channel.outputSelector));
        lightIntensities_[index]->setValue(channel.normalizedIntensity);
    }

    ui_->algorithmPathLineEdit->setText(
        nativeText(draft_.vision.algorithmConfigPath));
    ui_->tcpAddressLineEdit->setText(draft_.tcp.listenAddress.toString());
    ui_->tcpPortSpinBox->setValue(draft_.tcp.port);
    ui_->heartbeatTimeoutSpinBox->setValue(
        static_cast<int>(draft_.tcp.heartbeatTimeout.count()));
    ui_->maximumFrameBytesSpinBox->setValue(
        static_cast<int>(draft_.tcp.maximumFrameBytes));
    ui_->acknowledgementIntervalSpinBox->setValue(static_cast<int>(
        draft_.resultDelivery.acknowledgementInterval.count()));
    ui_->maximumRetriesSpinBox->setValue(
        draft_.resultDelivery.maximumRetries);
    ui_->logDirectoryLineEdit->setText(nativeText(draft_.logging.directory));
    ui_->logLevelComboBox->setCurrentText(
        QString::fromStdString(draft_.logging.level));
    ui_->maximumFileMiBSpinBox->setValue(static_cast<int>(
        draft_.logging.maximumFileBytes / (1024U * 1024U)));
    ui_->maximumFilesSpinBox->setValue(
        static_cast<int>(draft_.logging.maximumFiles));

    loading_ = false;
    dirty_ = false;
    dirtyCount_ = 0;
    dirtySummary_.clear();
    updateApplyActions();
}

ApplicationConfig MainWindow::collectDraft() const
{
    ApplicationConfig candidate = draft_;
    for (std::size_t index = 0; index < candidate.vision.cameras.size(); ++index) {
        auto& camera = candidate.vision.cameras[index];
        camera.serialNumber = cameraSerials_[index]->text().trimmed().toStdString();
        camera.exposureTime = vision::hardware::HikMicroseconds(
            cameraExposures_[index]->value());
        camera.gain = cameraGains_[index]->value();
    }
    candidate.vision.captureTimeout =
        std::chrono::milliseconds(ui_->captureTimeoutSpinBox->value());
    candidate.vision.productionTrigger.mode =
        vision::hardware::HikTriggerMode::Hardware;
    candidate.vision.productionTrigger.inputLine =
        ui_->triggerLineComboBox->currentIndex() == 0
            ? vision::hardware::HikInputLine::Line0
            : vision::hardware::HikInputLine::Line2;
    candidate.vision.productionTrigger.activation =
        ui_->triggerActivationComboBox->currentIndex() == 0
            ? vision::hardware::HikTriggerActivation::RisingEdge
            : vision::hardware::HikTriggerActivation::FallingEdge;
    candidate.vision.productionTrigger.delay =
        vision::hardware::HikMicroseconds(ui_->triggerDelaySpinBox->value());

    candidate.vision.lightController.serialNumber =
        ui_->lightSerialLineEdit->text().trimmed().toStdString();
    for (std::size_t index = 0;
         index < candidate.vision.lightController.channels.size(); ++index) {
        auto& channel = candidate.vision.lightController.channels[index];
        channel.outputSelector =
            lightSelectors_[index]->text().trimmed().toStdString();
        channel.normalizedIntensity = lightIntensities_[index]->value();
    }

    candidate.vision.algorithmConfigPath =
        nativePath(ui_->algorithmPathLineEdit->text().trimmed());
    candidate.tcp.listenAddress.setAddress(
        ui_->tcpAddressLineEdit->text().trimmed());
    candidate.tcp.port = static_cast<quint16>(ui_->tcpPortSpinBox->value());
    candidate.tcp.heartbeatTimeout =
        std::chrono::milliseconds(ui_->heartbeatTimeoutSpinBox->value());
    candidate.tcp.maximumFrameBytes = ui_->maximumFrameBytesSpinBox->value();
    candidate.resultDelivery.acknowledgementInterval =
        std::chrono::milliseconds(
            ui_->acknowledgementIntervalSpinBox->value());
    candidate.resultDelivery.maximumRetries =
        ui_->maximumRetriesSpinBox->value();
    candidate.logging.directory =
        nativePath(ui_->logDirectoryLineEdit->text().trimmed());
    candidate.logging.level = ui_->logLevelComboBox->currentText().toStdString();
    candidate.logging.maximumFileBytes =
        static_cast<std::size_t>(ui_->maximumFileMiBSpinBox->value()) *
        1024U * 1024U;
    candidate.logging.maximumFiles =
        static_cast<std::size_t>(ui_->maximumFilesSpinBox->value());
    return candidate;
}

void MainWindow::markDirty()
{
    if (loading_) return;
    const auto summary = summarizeChanges(draft_, collectDraft());
    dirtyCount_ = summary.count;
    dirtySummary_ = summary.categories.join(QStringLiteral("、"));
    dirty_ = dirtyCount_ > 0;
    updateApplyActions();
}

void MainWindow::updateApplyActions()
{
    const bool runtimeAvailable = snapshot_.runtimeInitialized ||
        (snapshot_.applicationState == ApplicationState::StartFailed &&
         snapshot_.visionState == VisionServiceState::Faulted);
    const bool applyStateStable =
        snapshot_.configurationApplyState == ConfigurationApplyState::Idle ||
        snapshot_.configurationApplyState == ConfigurationApplyState::Failed;
    const bool idle = runtimeAvailable && applyStateStable &&
        snapshot_.taskState == TaskState::Idle &&
        !snapshot_.calibrationMaintenanceLocked;

    ui_->applyConfigurationButton->setEnabled(dirty_ && idle);
    ui_->revertConfigurationButton->setEnabled(dirty_ && applyStateStable);
    ui_->visionConfigTab->setEnabled(applyStateStable);
    ui_->communicationSystemTab->setEnabled(applyStateStable);

    ui_->dirtyStateLabel->setText(dirty_
        ? QStringLiteral("%1 项未应用修改 · %2")
            .arg(dirtyCount_).arg(dirtySummary_)
        : QStringLiteral("配置已同步 · 修订 %1")
            .arg(snapshot_.activeConfigurationRevision));

    const QWidget* current = ui_->mainTabWidget->currentWidget();
    const bool onConfigurationPage = current == ui_->visionConfigTab ||
        current == ui_->communicationSystemTab;
    ui_->configurationActionBar->setVisible(onConfigurationPage);
}

void MainWindow::applyDraft()
{
    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("应用配置"),
        QStringLiteral(
            "配置将在任务空闲时应用并原子保存。设备或 TCP 可能短暂重启，是否继续？"));
    if (answer != QMessageBox::Yes) return;
    controller_->requestApplyConfiguration(collectDraft());
}

void MainWindow::updateSnapshot(const ApplicationSnapshot& snapshot)
{
    previousTaskState_ = snapshot_.taskState;
    snapshot_ = snapshot;

    ui_->applicationStateValue->setText(
        applicationStateText(snapshot.applicationState));
    ui_->visionStateValue->setText(visionStateText(snapshot.visionState));
    if (snapshot.tcpConnection) {
        ui_->tcpStateValue->setText(
            QStringLiteral("已连接（代次 %1）")
                .arg(snapshot.tcpConnection->generation));
    }
    else if (snapshot.tcpListening) {
        ui_->tcpStateValue->setText(QStringLiteral("监听中"));
    }
    else {
        ui_->tcpStateValue->setText(QStringLiteral("未监听"));
    }
    ui_->taskStateValue->setText(taskStateText(snapshot.taskState));
    ui_->activeTaskValue->setText(
        snapshot.activeTaskId.value_or(QStringLiteral("—")));
    ui_->configurationRevisionValue->setText(
        QString::number(snapshot.activeConfigurationRevision));

    const bool shuttingDown =
        snapshot.applicationState == ApplicationState::ShuttingDown ||
        snapshot.applicationState == ApplicationState::Stopped;
    const bool canRetryInitialization = !shuttingDown &&
        snapshot.applicationState == ApplicationState::StartFailed &&
        !snapshot.runtimeInitialized;
    const bool canRetryTcp = !shuttingDown && snapshot.runtimeInitialized &&
        snapshot.visionState == VisionServiceState::Ready &&
        !snapshot.tcpListening;
    const bool canRecover = !shuttingDown && snapshot.runtimeInitialized &&
        snapshot.visionState == VisionServiceState::Faulted &&
        snapshot.taskState == TaskState::Idle &&
        !snapshot.calibrationMaintenanceLocked &&
        (snapshot.configurationApplyState == ConfigurationApplyState::Idle ||
         snapshot.configurationApplyState == ConfigurationApplyState::Failed);

    ui_->retryInitializationButton->setEnabled(canRetryInitialization);
    ui_->retryInitializationAction->setEnabled(canRetryInitialization);
    ui_->retryTcpButton->setEnabled(canRetryTcp);
    ui_->retryTcpAction->setEnabled(canRetryTcp);
    ui_->recoverVisionButton->setEnabled(canRecover);
    ui_->recoverVisionAction->setEnabled(canRecover);

    if (snapshot.lastError) {
        const QString error = QString::fromStdString(snapshot.lastError->message);
        ui_->resultErrorValue->setText(error);
        ui_->statusBar->showMessage(error);
    }
    else {
        ui_->resultErrorValue->setText(QStringLiteral("无"));
        ui_->statusBar->clearMessage();
    }

    if (snapshot.taskState == TaskState::Preparing &&
        previousTaskState_ != TaskState::Preparing) {
        clearPresentation(QStringLiteral("等待采集"));
    }
    updateApplyActions();
}

void MainWindow::updatePresentation(
    const TaskPresentationSnapshot& presentation)
{
    const auto& result = presentation.result;
    QColor color(180, 0, 0);
    if (result.outcome == QStringLiteral("OK")) {
        color = QColor(0, 128, 0);
    }
    else if (result.outcome != QStringLiteral("NG")) {
        color = QColor(184, 92, 0);
    }

    ui_->resultTaskValue->setText(result.taskLabel);
    ui_->resultModeValue->setText(locateModeText(result.mode));
    ui_->resultCompletedValue->setText(
        presentation.completedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    ui_->resultOutcomeValue->setText(result.outcome);
    setSemanticTextColor(ui_->resultOutcomeValue, color);
    ui_->resultXValue->setText(QString::number(result.hasPose ? result.x : 0.0, 'f', 3));
    ui_->resultYValue->setText(QString::number(result.hasPose ? result.y : 0.0, 'f', 3));
    ui_->resultRValue->setText(QString::number(result.hasPose ? result.r : 0.0, 'f', 3));
    ui_->resultErrorValue->setText(result.error
        ? QString::fromStdString(result.error->message)
        : QStringLiteral("无"));

    const QString overlay = QStringLiteral("%1 | %2 | X %3  Y %4  R %5")
        .arg(result.taskLabel, result.outcome)
        .arg(result.hasPose ? result.x : 0.0, 0, 'f', 3)
        .arg(result.hasPose ? result.y : 0.0, 0, 'f', 3)
        .arg(result.hasPose ? result.r : 0.0, 0, 'f', 3);

    if (result.mode == vision::core::LocateMode::Edge) {
        ui_->imageStack->setCurrentWidget(ui_->edgeImagePage);
        ui_->edgeImageView->clearImage(QStringLiteral("未取得 Center 图像"));
        for (const auto& image : presentation.images) {
            if (image.role == vision::core::CameraRole::Center) {
                ui_->edgeImageView->setImage(image.image);
                break;
            }
        }
        ui_->edgeImageView->setOverlay(QStringLiteral("Center"), overlay, color);
        return;
    }

    ui_->imageStack->setCurrentWidget(ui_->markImagePage);
    const std::array<vision::core::CameraRole, 4> roles{
        vision::core::CameraRole::TopLeft,
        vision::core::CameraRole::TopRight,
        vision::core::CameraRole::BottomLeft,
        vision::core::CameraRole::BottomRight};
    const std::array<ImageView*, 4> views{
        ui_->topLeftImageView,
        ui_->topRightImageView,
        ui_->bottomLeftImageView,
        ui_->bottomRightImageView};

    for (std::size_t index = 0; index < roles.size(); ++index) {
        views[index]->clearImage(QStringLiteral("未取得图像"));
        for (const auto& image : presentation.images) {
            if (image.role == roles[index]) {
                views[index]->setImage(image.image);
                break;
            }
        }
        views[index]->setOverlay(cameraRoleText(roles[index]), overlay, color);
    }
}

void MainWindow::clearPresentation(const QString& message)
{
    ui_->edgeImageView->clearImage(message);
    for (ImageView* view : {
             ui_->topLeftImageView,
             ui_->topRightImageView,
             ui_->bottomLeftImageView,
             ui_->bottomRightImageView}) {
        view->clearImage(message);
    }
    ui_->resultTaskValue->setText(QStringLiteral("—"));
    ui_->resultModeValue->setText(QStringLiteral("—"));
    ui_->resultCompletedValue->setText(QStringLiteral("—"));
    ui_->resultOutcomeValue->setText(message);
    resetTextColor(ui_->resultOutcomeValue);
    ui_->resultXValue->setText(QStringLiteral("0.000"));
    ui_->resultYValue->setText(QStringLiteral("0.000"));
    ui_->resultRValue->setText(QStringLiteral("0.000"));
    ui_->resultErrorValue->setText(QStringLiteral("无"));
}

void MainWindow::appendDiagnostic(const QString& message)
{
    ui_->diagnosticTextEdit->appendPlainText(
        QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                 message));
}

void MainWindow::saveUiState()
{
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(
        QStringLiteral("mainWindow/monitorSplitter"),
        ui_->monitorSplitter->saveState());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    emit applicationExitRequested();
}

} // namespace vision::app
