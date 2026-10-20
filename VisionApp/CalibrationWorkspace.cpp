#include "CalibrationWorkspace.h"

#include "CalibrationImageView.h"
#include "ui_CalibrationWorkspace.h"

#include <QAbstractItemView>
#include <QColor>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTimer>
#include <QVariant>

#include <array>
#include <algorithm>
#include <utility>

namespace vision::app {
namespace {

constexpr std::array<const char*, 4> cornerRoleNames{
    "TopLeft", "TopRight", "BottomRight", "BottomLeft"};

constexpr std::array<vision::core::CameraRole, 4> cornerRoles{
    vision::core::CameraRole::TopLeft,
    vision::core::CameraRole::TopRight,
    vision::core::CameraRole::BottomRight,
    vision::core::CameraRole::BottomLeft};

const QString centerSplitterSizesKey{
    QStringLiteral("calibration/centerSplitterSizesV2")};
const QString cornerSplitterSizesKey{
    QStringLiteral("calibration/cornerSplitterSizesV2")};

QVariantList serializeSplitterSizes(const QList<int>& sizes)
{
    QVariantList values;
    values.reserve(sizes.size());
    for (const int size : sizes) {
        values.append(size);
    }
    return values;
}

QList<int> deserializeSplitterSizes(const QVariant& storedValue)
{
    const QVariantList values = storedValue.toList();
    if (values.size() != 2) {
        return {};
    }

    bool firstValid = false;
    bool secondValid = false;
    const int first = values[0].toInt(&firstValid);
    const int second = values[1].toInt(&secondValid);
    if (!firstValid || !secondValid || first <= 0 || second <= 0) {
        return {};
    }
    return {first, second};
}

std::size_t modeIndex(CalibrationMode mode)
{
    return mode == CalibrationMode::Center ? 0U : 1U;
}

QString modeTitle(CalibrationMode mode)
{
    return mode == CalibrationMode::Center
        ? QStringLiteral("中心相机")
        : QStringLiteral("四角相机");
}

QString stageText(CalibrationUiStage stage)
{
    switch (stage) {
    case CalibrationUiStage::NoInput:
        return QStringLiteral("未准备：请选择理论点文件");
    case CalibrationUiStage::InputSelected:
        return QStringLiteral("理论点文件已选择，等待采集");
    case CalibrationUiStage::Capturing:
        return QStringLiteral("采集处理中");
    case CalibrationUiStage::ImageCaptured:
        return QStringLiteral("图像采集完成，等待圆心检测");
    case CalibrationUiStage::Detecting:
        return QStringLiteral("圆心检测处理中");
    case CalibrationUiStage::SelectingAnchors:
        return QStringLiteral("请在图像中依次选择P1和P2");
    case CalibrationUiStage::PointsDetected:
        return QStringLiteral("圆心检测完成，等待计算矩阵");
    case CalibrationUiStage::Solving:
        return QStringLiteral("矩阵计算处理中");
    case CalibrationUiStage::CandidateReady:
        return QStringLiteral("候选矩阵已生成，等待质量验证");
    case CalibrationUiStage::Validating:
        return QStringLiteral("候选矩阵质量验证中");
    case CalibrationUiStage::ReadyToApply:
        return QStringLiteral("质量验证通过，候选标定待应用");
    case CalibrationUiStage::Applying:
        return QStringLiteral("候选标定应用中");
    case CalibrationUiStage::Completed:
        return QStringLiteral("校正已完成");
    case CalibrationUiStage::Failed:
        return QStringLiteral("校正失败");
    }
    return QStringLiteral("未知状态");
}

int completedStepCount(CalibrationUiStage stage)
{
    switch (stage) {
    case CalibrationUiStage::NoInput: return 0;
    case CalibrationUiStage::InputSelected: return 1;
    case CalibrationUiStage::Capturing: return 1;
    case CalibrationUiStage::ImageCaptured: return 2;
    case CalibrationUiStage::Detecting: return 2;
    case CalibrationUiStage::SelectingAnchors: return 3;
    case CalibrationUiStage::PointsDetected: return 3;
    case CalibrationUiStage::Solving: return 3;
    case CalibrationUiStage::CandidateReady: return 4;
    case CalibrationUiStage::Validating: return 4;
    case CalibrationUiStage::ReadyToApply: return 5;
    case CalibrationUiStage::Applying: return 5;
    case CalibrationUiStage::Completed: return 6;
    case CalibrationUiStage::Failed: return 0;
    }
    return 0;
}

bool isOperationInProgress(CalibrationUiStage stage)
{
    switch (stage) {
    case CalibrationUiStage::Capturing:
    case CalibrationUiStage::Detecting:
    case CalibrationUiStage::Solving:
    case CalibrationUiStage::Validating:
    case CalibrationUiStage::Applying:
        return true;
    default:
        return false;
    }
}

CalibrationUiStage uiStage(CalibrationProcessState state)
{
    switch (state) {
    case CalibrationProcessState::NoInput: return CalibrationUiStage::NoInput;
    case CalibrationProcessState::ReadyToCapture: return CalibrationUiStage::InputSelected;
    case CalibrationProcessState::Capturing: return CalibrationUiStage::Capturing;
    case CalibrationProcessState::ImageReady: return CalibrationUiStage::ImageCaptured;
    case CalibrationProcessState::Detecting: return CalibrationUiStage::Detecting;
    case CalibrationProcessState::SelectingAnchors: return CalibrationUiStage::SelectingAnchors;
    case CalibrationProcessState::ReadyToSolve: return CalibrationUiStage::PointsDetected;
    case CalibrationProcessState::Solving: return CalibrationUiStage::Solving;
    case CalibrationProcessState::CandidateReady: return CalibrationUiStage::CandidateReady;
    case CalibrationProcessState::Evaluating: return CalibrationUiStage::Validating;
    case CalibrationProcessState::ReadyToApply: return CalibrationUiStage::ReadyToApply;
    case CalibrationProcessState::Applying: return CalibrationUiStage::Applying;
    case CalibrationProcessState::Completed: return CalibrationUiStage::Completed;
    case CalibrationProcessState::Failed: return CalibrationUiStage::Failed;
    }
    return CalibrationUiStage::Failed;
}

int cornerRoleIndex(vision::core::CameraRole role)
{
    for (int index = 0; index < static_cast<int>(cornerRoles.size()); ++index) {
        if (cornerRoles[static_cast<std::size_t>(index)] == role) return index;
    }
    return -1;
}

int displayRoleIndex(vision::core::CameraRole role)
{
    if (role == vision::core::CameraRole::Center) return 0;
    const int corner = cornerRoleIndex(role);
    return corner < 0 ? -1 : corner + 1;
}

void setLabelColor(QLabel* label, const QColor& color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

void setReadOnlyItem(QTableWidget* table, int row, int column, const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    table->setItem(row, column, item);
}

void initializeReadOnlyTable(QTableWidget* table)
{
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

} // namespace

CalibrationWorkspace::CalibrationWorkspace(QWidget* parent)
    : QWidget(parent),
      ui_(std::make_unique<Ui::CalibrationWorkspace>())
{
    ui_->setupUi(this);
    initializeTables();
    initializeImageViews();
    initializeLayout();
    connectUi();
    restoreUiState();
    resetPage(CalibrationMode::Center, false);
    resetPage(CalibrationMode::FourCorners, false);
    setBackendAvailable(false);
}

CalibrationWorkspace::~CalibrationWorkspace()
{
    saveUiState();
}

void CalibrationWorkspace::setBackendAvailable(bool available)
{
    backendAvailable_ = available;
    updatePage(CalibrationMode::Center);
    updatePage(CalibrationMode::FourCorners);
}

bool CalibrationWorkspace::backendAvailable() const noexcept
{
    return backendAvailable_;
}

void CalibrationWorkspace::setStage(
    CalibrationMode mode,
    CalibrationUiStage stage,
    QString message)
{
    auto& state = pageState(mode);
    state.stage = stage;
    state.message = std::move(message);
    updatePage(mode);
}

void CalibrationWorkspace::applySnapshot(const CalibrationSnapshot& snapshot)
{
    backendAvailable_ = snapshot.backendAvailable;
    maintenanceLocked_ = snapshot.maintenanceLocked;
    auto& state = pageState(snapshot.mode);
    state.stage = uiStage(snapshot.state);
    state.message = snapshot.message;

    auto viewForRole = [this](vision::core::CameraRole role)
        -> CalibrationImageView* {
        switch (role) {
        case vision::core::CameraRole::Center:
            return ui_->centerCalibrationImageView;
        case vision::core::CameraRole::TopLeft:
            return ui_->topLeftCalibrationImageView;
        case vision::core::CameraRole::TopRight:
            return ui_->topRightCalibrationImageView;
        case vision::core::CameraRole::BottomRight:
            return ui_->bottomRightCalibrationImageView;
        case vision::core::CameraRole::BottomLeft:
            return ui_->bottomLeftCalibrationImageView;
        default:
            return nullptr;
        }
    };

    for (const CalibrationRoleSnapshot& role : snapshot.roles) {
        if (role.role == vision::core::CameraRole::Center) {
            state.theoreticalPointPath = role.pointFilePath;
            ui_->centerPointValidationValue->setText(role.pointFileValid
                ? QStringLiteral("通过 · %1 点").arg(role.theoreticalPointCount)
                : QStringLiteral("—"));
        } else {
            const int index = cornerRoleIndex(role.role);
            if (index >= 0) {
                state.cornerPointPaths[static_cast<std::size_t>(index)] =
                    role.pointFilePath;
            }
        }

        CalibrationImageView* view = viewForRole(role.role);
        if (view == nullptr) continue;
        const int imageIndex = displayRoleIndex(role.role);
        if (imageIndex >= 0 && !role.rectifiedImage.isNull() &&
            displayedImages_[static_cast<std::size_t>(imageIndex)] !=
                role.rectifiedImage) {
            displayedImages_[static_cast<std::size_t>(imageIndex)] =
                role.rectifiedImage;
            view->setImage(role.rectifiedImage);
        }

        QVector<CalibrationOverlayPoint> overlay;
        if (role.hasSolution) {
            overlay.reserve(role.points.size());
            for (const auto& point : role.points) {
                overlay.push_back(CalibrationOverlayPoint{
                    -1,
                    QStringLiteral("P%1").arg(point.pointIndex + 1U),
                    point.imagePoint,
                    point.used});
            }
        } else {
            overlay.reserve(role.detectionPoints.size());
            for (int index = 0; index < role.detectionPoints.size(); ++index) {
                QString label;
                if (role.p1DetectionIndex &&
                    *role.p1DetectionIndex == index) {
                    label = QStringLiteral("P1");
                } else if (role.p2DetectionIndex &&
                           *role.p2DetectionIndex == index) {
                    label = QStringLiteral("P2");
                }
                overlay.push_back(CalibrationOverlayPoint{
                    index, label, role.detectionPoints[index], true});
            }
        }
        view->setCalibrationPoints(overlay);
        view->setPointSelectionEnabled(
            snapshot.state == CalibrationProcessState::SelectingAnchors ||
            snapshot.state == CalibrationProcessState::ReadyToSolve);
        view->setOverlay(
            role.role == vision::core::CameraRole::Center
                ? QStringLiteral("Center")
                : QString::fromLatin1(cornerRoleNames[static_cast<std::size_t>(
                      std::max(0, cornerRoleIndex(role.role)))]),
            snapshot.message,
            snapshot.error ? QColor(180, 0, 0) : QColor(0, 128, 0));

        if (role.role == vision::core::CameraRole::Center) {
            ui_->centerDetectedPointsValue->setText(
                QString::number(role.detectionPoints.size()));
            ui_->centerPointTable->setRowCount(role.points.size());
            for (int row = 0; row < role.points.size(); ++row) {
                const auto& point = role.points[row];
                setReadOnlyItem(ui_->centerPointTable, row, 0,
                    QStringLiteral("P%1").arg(point.pointIndex + 1U));
                setReadOnlyItem(ui_->centerPointTable, row, 1,
                    QString::number(point.imagePoint.x(), 'f', 3));
                setReadOnlyItem(ui_->centerPointTable, row, 2,
                    QString::number(point.imagePoint.y(), 'f', 3));
                setReadOnlyItem(ui_->centerPointTable, row, 3,
                    QString::number(point.physicalPoint.x(), 'f', 6));
                setReadOnlyItem(ui_->centerPointTable, row, 4,
                    QString::number(point.physicalPoint.y(), 'f', 6));
                setReadOnlyItem(ui_->centerPointTable, row, 5,
                    QString::number(point.residual, 'f', 6));
                setReadOnlyItem(ui_->centerPointTable, row, 6,
                    point.used ? QStringLiteral("是") : QStringLiteral("否"));
            }
            if (role.hasSolution) {
                for (int row = 0; row < 3; ++row) {
                    for (int column = 0; column < 3; ++column) {
                        setReadOnlyItem(ui_->centerMatrixTable, row, column,
                            QString::number(
                                role.imageToPhysical[static_cast<std::size_t>(
                                    row * 3 + column)], 'g', 12));
                    }
                }
            }
            if (role.evaluated) {
                ui_->centerRmsValue->setText(
                    QStringLiteral("%1 mm").arg(role.rmsError, 0, 'f', 6));
                ui_->centerMaximumResidualValue->setText(
                    QStringLiteral("%1 mm").arg(role.maximumError, 0, 'f', 6));
                ui_->centerValidationPointsValue->setText(
                    QStringLiteral("%1 / %2")
                        .arg(role.usedPointCount).arg(role.detectedPointCount));
                ui_->centerValidationResultValue->setText(
                    role.passed ? QStringLiteral("通过")
                                : QStringLiteral("未通过"));
            }
        } else {
            const int row = cornerRoleIndex(role.role);
            if (row >= 0) {
                setReadOnlyItem(ui_->cornerQualityTable, row, 1,
                    QString::number(role.detectionPoints.size()));
                setReadOnlyItem(ui_->cornerQualityTable, row, 2,
                    role.evaluated
                        ? QString::number(role.rmsError, 'f', 6)
                        : QStringLiteral("—"));
                setReadOnlyItem(ui_->cornerQualityTable, row, 3,
                    role.evaluated
                        ? QString::number(role.maximumError, 'f', 6)
                        : QStringLiteral("—"));
                setReadOnlyItem(ui_->cornerQualityTable, row, 4,
                    role.evaluated
                        ? (role.passed ? QStringLiteral("通过")
                                       : QStringLiteral("未通过"))
                        : QStringLiteral("—"));
                QString matrixText = QStringLiteral("—");
                if (role.hasSolution) {
                    QStringList values;
                    for (double value : role.imageToPhysical) {
                        values.push_back(QString::number(value, 'g', 8));
                    }
                    matrixText = values.join(QStringLiteral(", "));
                }
                setReadOnlyItem(ui_->cornerCandidateTable, row, 1, matrixText);
                setReadOnlyItem(ui_->cornerCandidateTable, row, 2,
                    role.evaluated && role.passed
                        ? QStringLiteral("候选可用") : QStringLiteral("—"));
            }
        }
    }

    if (snapshot.mode == CalibrationMode::FourCorners) {
        const bool allFiles = std::all_of(
            state.cornerPointPaths.begin(), state.cornerPointPaths.end(),
            [](const QString& path) { return !path.isEmpty(); });
        ui_->cornerPointValidationValue->setText(
            allFiles
                ? (snapshot.state == CalibrationProcessState::NoInput
                       ? QStringLiteral("四个文件点数不一致")
                       : QStringLiteral("四个文件均已校验"))
                : QStringLiteral("请选择四个角色文件"));
        ui_->cornerCommonReferenceValue->setText(
            allFiles ? QStringLiteral("角色文件完整") : QStringLiteral("不完整"));
        ui_->cornerRoleMatchValue->setText(
            allFiles ? QStringLiteral("角色绑定完成") : QStringLiteral("不完整"));
        ui_->cornerAxisConsistencyValue->setText(
            snapshot.state >= CalibrationProcessState::ReadyToSolve
                ? QStringLiteral("四路P1/P2已建立") : QStringLiteral("待建立"));
        ui_->cornerCandidateCompletenessValue->setText(
            snapshot.state == CalibrationProcessState::ReadyToApply ||
            snapshot.state == CalibrationProcessState::Applying ||
            snapshot.state == CalibrationProcessState::Completed
                ? QStringLiteral("四路质量通过") : QStringLiteral("待完成"));
    }
    updatePage(snapshot.mode);
}

void CalibrationWorkspace::initializeTables()
{
    ui_->centerPointTable->setHorizontalHeaderLabels({
        QStringLiteral("点号"), QStringLiteral("像素 U"),
        QStringLiteral("像素 V"), QStringLiteral("实际 X"),
        QStringLiteral("实际 Y"), QStringLiteral("残差"),
        QStringLiteral("是否采用")});
    initializeReadOnlyTable(ui_->centerPointTable);

    ui_->centerMatrixTable->horizontalHeader()->setVisible(false);
    ui_->centerMatrixTable->verticalHeader()->setVisible(false);
    ui_->centerMatrixTable->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    ui_->centerMatrixTable->verticalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    ui_->centerMatrixTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui_->centerMatrixTable->setSelectionMode(QAbstractItemView::NoSelection);

    ui_->cornerQualityTable->setHorizontalHeaderLabels({
        QStringLiteral("相机角色"), QStringLiteral("检测点数"),
        QStringLiteral("RMS"), QStringLiteral("最大残差"),
        QStringLiteral("质量结果")});
    initializeReadOnlyTable(ui_->cornerQualityTable);

    ui_->cornerCandidateTable->setHorizontalHeaderLabels({
        QStringLiteral("相机角色"), QStringLiteral("候选矩阵"),
        QStringLiteral("应用状态")});
    initializeReadOnlyTable(ui_->cornerCandidateTable);

    for (int row = 0; row < static_cast<int>(cornerRoleNames.size()); ++row) {
        const QString role = QString::fromLatin1(
            cornerRoleNames[static_cast<std::size_t>(row)]);
        setReadOnlyItem(ui_->cornerQualityTable, row, 0, role);
        setReadOnlyItem(ui_->cornerCandidateTable, row, 0, role);
    }
}

void CalibrationWorkspace::initializeImageViews()
{
    ui_->centerCalibrationImageView->setOverlay(
        QStringLiteral("Center"), QStringLiteral("等待后端图像"));
    ui_->topLeftCalibrationImageView->setOverlay(
        QStringLiteral("TopLeft"), QStringLiteral("等待后端图像"));
    ui_->topRightCalibrationImageView->setOverlay(
        QStringLiteral("TopRight"), QStringLiteral("等待后端图像"));
    ui_->bottomLeftCalibrationImageView->setOverlay(
        QStringLiteral("BottomLeft"), QStringLiteral("等待后端图像"));
    ui_->bottomRightCalibrationImageView->setOverlay(
        QStringLiteral("BottomRight"), QStringLiteral("等待后端图像"));
}

void CalibrationWorkspace::initializeLayout()
{
    ui_->workspaceLayout->setStretch(0, 0);
    ui_->workspaceLayout->setStretch(1, 1);
    ui_->centerPageLayout->setStretch(0, 0);
    ui_->centerPageLayout->setStretch(1, 0);
    ui_->centerPageLayout->setStretch(2, 1);
    ui_->centerPageLayout->setStretch(3, 0);
    ui_->cornerPageLayout->setStretch(0, 0);
    ui_->cornerPageLayout->setStretch(1, 0);
    ui_->cornerPageLayout->setStretch(2, 1);
    ui_->cornerPageLayout->setStretch(3, 0);

    for (QSplitter* splitter : {
             ui_->centerCalibrationSplitter,
             ui_->cornerCalibrationSplitter}) {
        splitter->setStretchFactor(0, 7);
        splitter->setStretchFactor(1, 3);
        splitter->setCollapsible(0, false);
        splitter->setCollapsible(1, true);
    }
    ui_->centerCalibrationSplitter->setSizes(centerExpandedSizes_);
    ui_->cornerCalibrationSplitter->setSizes(cornerExpandedSizes_);

    ui_->cornerImageGrid->setRowStretch(0, 1);
    ui_->cornerImageGrid->setRowStretch(1, 1);
    ui_->cornerImageGrid->setColumnStretch(0, 1);
    ui_->cornerImageGrid->setColumnStretch(1, 1);
}

void CalibrationWorkspace::connectUi()
{
    connect(ui_->centerFitImageButton, &QPushButton::clicked,
            ui_->centerCalibrationImageView,
            &CalibrationImageView::fitImage);
    connect(ui_->centerActualPixelsButton, &QPushButton::clicked,
            ui_->centerCalibrationImageView,
            &CalibrationImageView::showActualPixels);
    connect(ui_->cornerFitImagesButton, &QPushButton::clicked,
            this, [this] {
                for (CalibrationImageView* view : {
                         ui_->topLeftCalibrationImageView,
                         ui_->topRightCalibrationImageView,
                         ui_->bottomLeftCalibrationImageView,
                         ui_->bottomRightCalibrationImageView}) {
                    view->fitImage();
                }
            });
    connect(ui_->cornerActualPixelsButton, &QPushButton::clicked,
            this, [this] {
                for (CalibrationImageView* view : {
                         ui_->topLeftCalibrationImageView,
                         ui_->topRightCalibrationImageView,
                         ui_->bottomLeftCalibrationImageView,
                         ui_->bottomRightCalibrationImageView}) {
                    view->showActualPixels();
                }
            });
    connect(ui_->centerToggleDataButton, &QPushButton::clicked,
            this, [this] { toggleDataPanel(CalibrationMode::Center); });
    connect(ui_->cornerToggleDataButton, &QPushButton::clicked,
            this, [this] { toggleDataPanel(CalibrationMode::FourCorners); });
    connect(ui_->calibrationModeTabs, &QTabWidget::currentChanged,
            this, [this](int index) {
                const CalibrationMode mode = index == 0
                    ? CalibrationMode::Center
                    : CalibrationMode::FourCorners;
                QTimer::singleShot(0, this, [this, mode] {
                    refreshImageLayout(mode);
                });
            });

    connect(ui_->centerChoosePointsButton, &QPushButton::clicked,
            this, [this] {
                chooseTheoreticalPointFile(
                    CalibrationMode::Center,
                    vision::core::CameraRole::Center);
            });
    const std::array<QPushButton*, 4> cornerFileButtons{
        ui_->topLeftChoosePointsButton,
        ui_->topRightChoosePointsButton,
        ui_->bottomRightChoosePointsButton,
        ui_->bottomLeftChoosePointsButton};
    for (std::size_t index = 0; index < cornerFileButtons.size(); ++index) {
        connect(cornerFileButtons[index], &QPushButton::clicked,
                this, [this, index] {
                    chooseTheoreticalPointFile(
                        CalibrationMode::FourCorners, cornerRoles[index]);
                });
    }

    const std::array<CalibrationImageView*, 5> selectableViews{
        ui_->centerCalibrationImageView,
        ui_->topLeftCalibrationImageView,
        ui_->topRightCalibrationImageView,
        ui_->bottomRightCalibrationImageView,
        ui_->bottomLeftCalibrationImageView};
    const std::array<vision::core::CameraRole, 5> selectableRoles{
        vision::core::CameraRole::Center,
        vision::core::CameraRole::TopLeft,
        vision::core::CameraRole::TopRight,
        vision::core::CameraRole::BottomRight,
        vision::core::CameraRole::BottomLeft};
    for (std::size_t index = 0; index < selectableViews.size(); ++index) {
        connect(selectableViews[index],
                &CalibrationImageView::calibrationPointClicked,
                this, [this, role = selectableRoles[index]](int pointIndex) {
                    emit calibrationPointSelected(role, pointIndex);
                });
    }

    connect(ui_->centerResetButton, &QPushButton::clicked,
            this, [this] { resetPage(CalibrationMode::Center, true); });
    connect(ui_->cornerResetButton, &QPushButton::clicked,
            this, [this] { resetPage(CalibrationMode::FourCorners, true); });

    connect(ui_->centerCaptureButton, &QPushButton::clicked,
            this, [this] { emit captureRequested(CalibrationMode::Center); });
    connect(ui_->cornerCaptureButton, &QPushButton::clicked,
            this, [this] { emit captureRequested(CalibrationMode::FourCorners); });
    connect(ui_->centerDetectButton, &QPushButton::clicked,
            this, [this] { emit detectionRequested(CalibrationMode::Center); });
    connect(ui_->cornerDetectButton, &QPushButton::clicked,
            this, [this] { emit detectionRequested(CalibrationMode::FourCorners); });
    connect(ui_->centerSolveButton, &QPushButton::clicked,
            this, [this] { emit solveRequested(CalibrationMode::Center); });
    connect(ui_->cornerSolveButton, &QPushButton::clicked,
            this, [this] { emit solveRequested(CalibrationMode::FourCorners); });
    connect(ui_->centerVerifyButton, &QPushButton::clicked,
            this, [this] { emit validationRequested(CalibrationMode::Center); });
    connect(ui_->cornerVerifyButton, &QPushButton::clicked,
            this, [this] { emit validationRequested(CalibrationMode::FourCorners); });
    connect(ui_->centerApplyButton, &QPushButton::clicked,
            this, [this] { emit applyRequested(CalibrationMode::Center); });
    connect(ui_->cornerApplyButton, &QPushButton::clicked,
            this, [this] { emit applyRequested(CalibrationMode::FourCorners); });
}

void CalibrationWorkspace::restoreUiState()
{
    QSettings settings;
    const QList<int> savedCenterSizes = deserializeSplitterSizes(
        settings.value(centerSplitterSizesKey));
    const QList<int> savedCornerSizes = deserializeSplitterSizes(
        settings.value(cornerSplitterSizesKey));
    if (!savedCenterSizes.isEmpty()) {
        centerExpandedSizes_ = savedCenterSizes;
    }
    if (!savedCornerSizes.isEmpty()) {
        cornerExpandedSizes_ = savedCornerSizes;
    }

    // Older builds persisted QList<int> as an unregistered streamed user type.
    // Do not read those values: QSettings would emit QVariant::load warnings.
    settings.remove(QStringLiteral("calibration/centerSplitterSizes"));
    settings.remove(QStringLiteral("calibration/cornerSplitterSizes"));

    centerDataCollapsed_ = settings.value(
        QStringLiteral("calibration/centerDataCollapsed"), false).toBool();
    cornerDataCollapsed_ = settings.value(
        QStringLiteral("calibration/cornerDataCollapsed"), false).toBool();
    ui_->centerCalibrationSplitter->setSizes(centerExpandedSizes_);
    ui_->cornerCalibrationSplitter->setSizes(cornerExpandedSizes_);

    ui_->centerDataScrollArea->setVisible(!centerDataCollapsed_);
    ui_->cornerDataScrollArea->setVisible(!cornerDataCollapsed_);
    ui_->centerToggleDataButton->setText(centerDataCollapsed_
        ? QStringLiteral("显示数据") : QStringLiteral("隐藏数据"));
    ui_->cornerToggleDataButton->setText(cornerDataCollapsed_
        ? QStringLiteral("显示数据") : QStringLiteral("隐藏数据"));
}

void CalibrationWorkspace::saveUiState() const
{
    QSettings settings;
    QList<int> centerSizes = centerExpandedSizes_;
    QList<int> cornerSizes = cornerExpandedSizes_;
    if (!centerDataCollapsed_) {
        const QList<int> current = ui_->centerCalibrationSplitter->sizes();
        if (current.size() == 2 && current[0] > 0 && current[1] > 0) {
            centerSizes = current;
        }
    }
    if (!cornerDataCollapsed_) {
        const QList<int> current = ui_->cornerCalibrationSplitter->sizes();
        if (current.size() == 2 && current[0] > 0 && current[1] > 0) {
            cornerSizes = current;
        }
    }
    settings.setValue(
        centerSplitterSizesKey,
        serializeSplitterSizes(centerSizes));
    settings.setValue(
        cornerSplitterSizesKey,
        serializeSplitterSizes(cornerSizes));
    settings.setValue(
        QStringLiteral("calibration/centerDataCollapsed"),
        centerDataCollapsed_);
    settings.setValue(
        QStringLiteral("calibration/cornerDataCollapsed"),
        cornerDataCollapsed_);
}

void CalibrationWorkspace::toggleDataPanel(CalibrationMode mode)
{
    QSplitter* splitter = mode == CalibrationMode::Center
        ? ui_->centerCalibrationSplitter
        : ui_->cornerCalibrationSplitter;
    QWidget* dataPanel = mode == CalibrationMode::Center
        ? static_cast<QWidget*>(ui_->centerDataScrollArea)
        : static_cast<QWidget*>(ui_->cornerDataScrollArea);
    QPushButton* button = mode == CalibrationMode::Center
        ? ui_->centerToggleDataButton
        : ui_->cornerToggleDataButton;
    QList<int>& expandedSizes = mode == CalibrationMode::Center
        ? centerExpandedSizes_ : cornerExpandedSizes_;
    bool& collapsed = mode == CalibrationMode::Center
        ? centerDataCollapsed_ : cornerDataCollapsed_;

    if (!collapsed) {
        const QList<int> current = splitter->sizes();
        if (current.size() == 2 && current[0] > 0 && current[1] > 0) {
            expandedSizes = current;
        }
        dataPanel->hide();
        collapsed = true;
        button->setText(QStringLiteral("显示数据"));
    }
    else {
        dataPanel->show();
        collapsed = false;
        splitter->setSizes(expandedSizes);
        button->setText(QStringLiteral("隐藏数据"));
    }

    QTimer::singleShot(0, this, [this, mode] {
        refreshImageLayout(mode);
    });
}

void CalibrationWorkspace::refreshImageLayout(CalibrationMode mode)
{
    if (mode == CalibrationMode::Center) {
        displayedImages_[0] = QImage{};
        ui_->centerCalibrationImageView->refreshForLayoutChange();
        return;
    }
    for (CalibrationImageView* view : {
             ui_->topLeftCalibrationImageView,
             ui_->topRightCalibrationImageView,
             ui_->bottomLeftCalibrationImageView,
             ui_->bottomRightCalibrationImageView}) {
        view->refreshForLayoutChange();
    }
}

void CalibrationWorkspace::chooseTheoreticalPointFile(
    CalibrationMode mode, vision::core::CameraRole role)
{
    const auto& state = pageState(mode);
    QString selectedPath = state.theoreticalPointPath;
    if (mode == CalibrationMode::FourCorners) {
        const int index = cornerRoleIndex(role);
        if (index < 0) return;
        selectedPath = state.cornerPointPaths[static_cast<std::size_t>(index)];
    }
    const QString initialPath = selectedPath.isEmpty()
        ? QString()
        : QFileInfo(selectedPath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择%1理论点文件").arg(modeTitle(mode)),
        initialPath,
        QStringLiteral("理论点文件 (*.*)"));
    if (path.isEmpty()) return;

    clearResults(mode);
    auto& updated = pageState(mode);
    if (mode == CalibrationMode::Center) {
        updated.theoreticalPointPath = path;
    } else {
        updated.cornerPointPaths[static_cast<std::size_t>(
            cornerRoleIndex(role))] = path;
    }
    updated.message = QStringLiteral("正在校验理论点文件…");
    updatePage(mode);
    emit theoreticalPointFileSelected(mode, role, path);
}

void CalibrationWorkspace::resetPage(CalibrationMode mode, bool notify)
{
    clearResults(mode);
    pageState(mode) = PageState{};
    updatePage(mode);
    if (notify) emit resetRequested(mode);
}

void CalibrationWorkspace::clearResults(CalibrationMode mode)
{
    if (mode == CalibrationMode::Center) {
        displayedImages_[0] = QImage{};
        ui_->centerPointTable->setRowCount(0);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                setReadOnlyItem(
                    ui_->centerMatrixTable, row, column, QStringLiteral("—"));
            }
        }
        for (QLabel* value : {
                 ui_->centerDetectedPointsValue,
                 ui_->centerRmsValue,
                 ui_->centerMaximumResidualValue,
                 ui_->centerValidationPointsValue,
                 ui_->centerValidationResultValue}) {
            value->setText(QStringLiteral("—"));
        }
        ui_->centerCalibrationImageView->clearImage();
        ui_->centerCalibrationImageView->setOverlay(
            QStringLiteral("Center"), QStringLiteral("等待后端图像"));
        return;
    }

    const std::array<CalibrationImageView*, 4> views{
        ui_->topLeftCalibrationImageView,
        ui_->topRightCalibrationImageView,
        ui_->bottomRightCalibrationImageView,
        ui_->bottomLeftCalibrationImageView};
    for (std::size_t index = 0; index < views.size(); ++index) {
        displayedImages_[index + 1U] = QImage{};
        views[index]->clearImage();
        views[index]->setOverlay(
            QString::fromLatin1(cornerRoleNames[index]),
            QStringLiteral("等待后端图像"));
    }

    for (int row = 0; row < static_cast<int>(cornerRoleNames.size()); ++row) {
        for (int column = 1; column < ui_->cornerQualityTable->columnCount(); ++column) {
            setReadOnlyItem(
                ui_->cornerQualityTable, row, column, QStringLiteral("—"));
        }
        for (int column = 1; column < ui_->cornerCandidateTable->columnCount(); ++column) {
            setReadOnlyItem(
                ui_->cornerCandidateTable, row, column, QStringLiteral("—"));
        }
    }
    for (QLabel* value : {
             ui_->cornerCommonReferenceValue,
             ui_->cornerRoleMatchValue,
             ui_->cornerAxisConsistencyValue,
             ui_->cornerCandidateCompletenessValue}) {
        value->setText(QStringLiteral("—"));
    }
}

void CalibrationWorkspace::updatePage(CalibrationMode mode)
{
    const auto& state = pageState(mode);
    QLabel* stateLabel = mode == CalibrationMode::Center
        ? ui_->centerPageStateLabel
        : ui_->cornerPageStateLabel;
    QLabel* backendLabel = mode == CalibrationMode::Center
        ? ui_->centerBackendStateLabel
        : ui_->cornerBackendStateLabel;

    if (mode == CalibrationMode::Center) {
        ui_->centerPointPathLineEdit->setText(state.theoreticalPointPath);
    } else {
        const std::array<QLineEdit*, 4> edits{
            ui_->cornerPointPathLineEdit,
            ui_->topRightPointPathLineEdit,
            ui_->bottomRightPointPathLineEdit,
            ui_->bottomLeftPointPathLineEdit};
        for (std::size_t index = 0; index < edits.size(); ++index) {
            edits[index]->setText(state.cornerPointPaths[index]);
        }
    }
    const QString description = state.message.isEmpty()
        ? stageText(state.stage)
        : state.message;
    stateLabel->setText(description);

    if (backendAvailable_) {
        backendLabel->setText(QStringLiteral("校正后端已连接"));
        setLabelColor(backendLabel, QColor(0, 128, 0));
    }
    else {
        backendLabel->setText(QStringLiteral("后端未接入：仅文件选择与界面预览可用"));
        setLabelColor(backendLabel, QColor(184, 92, 0));
    }

    updateStepPresentation(mode);
    updateActionAvailability(mode);
}

void CalibrationWorkspace::updateStepPresentation(CalibrationMode mode)
{
    const std::array<QLabel*, 6> centerSteps{
        ui_->centerStep1Label, ui_->centerStep2Label,
        ui_->centerStep3Label, ui_->centerStep4Label,
        ui_->centerStep5Label, ui_->centerStep6Label};
    const std::array<QLabel*, 6> cornerSteps{
        ui_->cornerStep1Label, ui_->cornerStep2Label,
        ui_->cornerStep3Label, ui_->cornerStep4Label,
        ui_->cornerStep5Label, ui_->cornerStep6Label};
    const auto& steps = mode == CalibrationMode::Center
        ? centerSteps : cornerSteps;
    const int completed = completedStepCount(pageState(mode).stage);
    const int current = completed < static_cast<int>(steps.size())
        ? completed : -1;

    for (int index = 0; index < static_cast<int>(steps.size()); ++index) {
        QFont font = steps[static_cast<std::size_t>(index)]->font();
        font.setBold(index == current);
        steps[static_cast<std::size_t>(index)]->setFont(font);
        if (index < completed) {
            setLabelColor(
                steps[static_cast<std::size_t>(index)], QColor(0, 128, 0));
        }
        else if (index == current) {
            setLabelColor(
                steps[static_cast<std::size_t>(index)], QColor(0, 102, 170));
        }
        else {
            steps[static_cast<std::size_t>(index)]->setPalette(QPalette());
        }
    }
}

void CalibrationWorkspace::updateActionAvailability(CalibrationMode mode)
{
    const auto stage = pageState(mode).stage;
    bool hasInput = !pageState(mode).theoreticalPointPath.isEmpty();
    if (mode == CalibrationMode::FourCorners) {
        hasInput = std::all_of(
            pageState(mode).cornerPointPaths.begin(),
            pageState(mode).cornerPointPaths.end(),
            [](const QString& path) { return !path.isEmpty(); });
    }
    const bool operationInProgress = isOperationInProgress(stage);
    QPushButton* capture = mode == CalibrationMode::Center
        ? ui_->centerCaptureButton : ui_->cornerCaptureButton;
    QPushButton* detect = mode == CalibrationMode::Center
        ? ui_->centerDetectButton : ui_->cornerDetectButton;
    QPushButton* solve = mode == CalibrationMode::Center
        ? ui_->centerSolveButton : ui_->cornerSolveButton;
    QPushButton* verify = mode == CalibrationMode::Center
        ? ui_->centerVerifyButton : ui_->cornerVerifyButton;
    QPushButton* apply = mode == CalibrationMode::Center
        ? ui_->centerApplyButton : ui_->cornerApplyButton;
    QPushButton* reset = mode == CalibrationMode::Center
        ? ui_->centerResetButton : ui_->cornerResetButton;

    capture->setEnabled(backendAvailable_ && hasInput &&
        !operationInProgress && stage != CalibrationUiStage::ReadyToApply);
    detect->setEnabled(backendAvailable_ &&
        stage == CalibrationUiStage::ImageCaptured);
    solve->setEnabled(backendAvailable_ &&
        stage == CalibrationUiStage::PointsDetected);
    verify->setEnabled(backendAvailable_ &&
        stage == CalibrationUiStage::CandidateReady);
    apply->setEnabled(backendAvailable_ &&
        stage == CalibrationUiStage::ReadyToApply);
    ui_->centerChoosePointsButton->setEnabled(
        mode != CalibrationMode::Center ||
        (!operationInProgress && !maintenanceLocked_));
    const std::array<QPushButton*, 4> cornerFileButtons{
        ui_->topLeftChoosePointsButton,
        ui_->topRightChoosePointsButton,
        ui_->bottomRightChoosePointsButton,
        ui_->bottomLeftChoosePointsButton};
    for (QPushButton* button : cornerFileButtons) {
        button->setEnabled(
            mode != CalibrationMode::FourCorners ||
            (!operationInProgress && !maintenanceLocked_));
    }
    reset->setEnabled(stage != CalibrationUiStage::Applying &&
        (hasInput || maintenanceLocked_ || stage != CalibrationUiStage::NoInput));

    const QString unavailableTip = QStringLiteral(
        "校正后端尚未接入，本轮只提供界面和操作门禁。");
    for (QPushButton* button : {capture, detect, solve, verify, apply}) {
        button->setToolTip(backendAvailable_ ? QString() : unavailableTip);
    }
}

CalibrationWorkspace::PageState& CalibrationWorkspace::pageState(
    CalibrationMode mode)
{
    return pageStates_[modeIndex(mode)];
}

const CalibrationWorkspace::PageState& CalibrationWorkspace::pageState(
    CalibrationMode mode) const
{
    return pageStates_[modeIndex(mode)];
}

} // namespace vision::app
