#include "VisionApp/Ui/support/CalibrationUiTestSupport.h"
#include "TestSupport/QtEventLoopTestSupport.h"
#include "TestSupport/QtTcpTestSupport.h"

#include <VisionCore/Ports.h>
#include <opencv2/imgproc.hpp>
#include <QApplication>
#include <QDir>
#include <QGraphicsSimpleTextItem>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QTabWidget>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace vision::test::app {
using namespace calibration_support;

CalibrationUiSession::CalibrationUiSession(const QString& name)
    : runtime(std::make_shared<CalibrationUiRuntimeState>())
{
    require(qobject_cast<QApplication*>(QCoreApplication::instance()) != nullptr,
            "QApplication was not initialized by UI test environment.");
    require(resources_.isValid(), "Cannot create UI test resource directory.");
    QSettings().clear();
    outputDirectory = QDir(qEnvironmentVariable("VISIONTEST_CALIB_UI_OUTPUT")).filePath(name);
    require(QDir().mkpath(outputDirectory), "Cannot create screenshot directory.");
    journal_.setFileName(outputDirectory + "/events.jsonl");
    require(journal_.open(QIODevice::WriteOnly | QIODevice::Append), "Cannot open UI journal.");
    generateResources(resources_.path());
    pointFile_ = resources_.path() + "/points.csv";
    const cv::Mat image = gridImage(false);
    require(QImage(image.data, image.cols, image.rows, static_cast<qsizetype>(image.step),
                   QImage::Format_Grayscale8).save(outputDirectory + "/input_grid.png"),
            "Cannot save simulated input image.");
    const auto state = runtime;
    CoreFactory factory = [state](const VisionRuntimeConfig&,
        vision::hardware::ICaptureObserver*, IDiagnosticSink* diagnostics,
        ILocateFrameObserver* frameObserver) -> CoreFactoryOutcome {
        ++state->factoryCalls;
        if (state->failFactory.exchange(false))
            return Error{ErrorCode::InternalFailure, "Injected runtime creation failure", "Calibration apply"};
        if (QThread::currentThread() == QCoreApplication::instance()->thread())
            state->threadContract = false;
        return std::make_unique<VisionCore>(std::make_unique<GridCamera>(state),
            std::make_unique<GridLight>(state), std::make_unique<UnusedLocateAlgorithm>(state),
            diagnostics, frameObserver);
    };
    controller = std::make_unique<ApplicationController>(testConfig(resources_.path()),
        std::move(factory), nullptr, nullptr, nativePath(resources_.path() + "/application.json"));
    window = std::make_unique<MainWindow>(controller.get(), QStringLiteral("5×5 模拟采图"));
    workspace = window->findChild<CalibrationWorkspace*>("calibrationWorkspace");
    require(workspace != nullptr, "Production calibration workspace not found.");
    view = workspace->findChild<CalibrationImageView*>("centerCalibrationImageView");
    require(view != nullptr, "Production center image view not found.");
    pointClicks = std::make_unique<QSignalSpy>(workspace, &CalibrationWorkspace::calibrationPointSelected);
    require(pointClicks->isValid(), "Cannot observe UI point selection.");
    QObject::connect(controller.get(), &ApplicationController::calibrationSnapshotChanged,
        window.get(), [this](const CalibrationSnapshot& value) {
            snapshot = value;
            ++snapshotRevision;
            record("snapshot");
        });
    QObject::connect(controller.get(), &ApplicationController::shutdownFinished,
        window.get(), [this] { stopped = true; window->hide(); });
    QObject::connect(window.get(), &MainWindow::applicationExitRequested,
        controller.get(), &ApplicationController::shutdown);
    auto* tabs = window->findChild<QTabWidget*>("mainTabWidget");
    auto* page = window->findChild<QWidget*>("calibrationTab");
    require(tabs && page, "Production calibration tab not found.");
    tabs->setCurrentWidget(page);
    window->setWindowTitle(QStringLiteral("中心标定 UI 测试｜5×5 模拟图像"));
    const QSize available = QApplication::primaryScreen()->availableGeometry().size() - QSize(40, 80);
    const double scale = std::min({1.0, available.width() / 1440.0, available.height() / 900.0});
    window->resize(qRound(1440 * scale), qRound(900 * scale));
    window->show();
}

CalibrationUiSession::~CalibrationUiSession()
{
    if (testing::Test::HasFailure() && workspace && window && window->isVisible()) {
        try { saveScreenshot("failure.png"); } catch (...) {}
    }
    closeAndWait();
    EXPECT_EQ(runtime->locateCalls.load(), 0);
    EXPECT_TRUE(runtime->threadContract.load());
    if (started_) EXPECT_TRUE(runtime->cameraDestroyed.load());
    pointClicks.reset();
    window.reset();
    controller.reset();
}

void CalibrationUiSession::start()
{
    started_ = true;
    controller->start();
    waitFor([this] { return controller->snapshot().applicationState == ApplicationState::Ready
                           && snapshot.backendAvailable; }, "runtime and calibration initialization");
    prepareInput();
    record("ready");
}

const char* CalibrationUiSession::modeButton(const char* centerName, const char* cornerName) const
{
    return mode == CalibrationMode::Center ? centerName : cornerName;
}

void CalibrationUiSession::selectRole(CameraRole role)
{
    activeRole = role;
    const char* name = "centerCalibrationImageView";
    switch (role) {
    case CameraRole::TopLeft: name = "topLeftCalibrationImageView"; break;
    case CameraRole::TopRight: name = "topRightCalibrationImageView"; break;
    case CameraRole::BottomRight: name = "bottomRightCalibrationImageView"; break;
    case CameraRole::BottomLeft: name = "bottomLeftCalibrationImageView"; break;
    default: break;
    }
    view = workspace->findChild<CalibrationImageView*>(name);
    require(view != nullptr, "Missing role image view");
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();
    view->fitImage();
}

void CalibrationUiSession::switchMode(CalibrationMode selected)
{
    require(!snapshot.maintenanceLocked, "Reset maintenance before switching input mode");
    mode = selected;
    auto* tabs = workspace->findChild<QTabWidget*>("calibrationModeTabs");
    require(tabs != nullptr, "Missing calibration mode tabs");
    QTest::mouseClick(tabs->tabBar(), Qt::LeftButton, Qt::NoModifier,
                     tabs->tabBar()->tabRect(mode == CalibrationMode::Center ? 0 : 1).center());
    selectRole(mode == CalibrationMode::Center ? CameraRole::Center : CameraRole::TopLeft);
    prepareInput();
}

void CalibrationUiSession::prepareInput()
{
    const std::vector<CameraRole> roles = mode == CalibrationMode::Center
        ? std::vector<CameraRole>{CameraRole::Center}
        : std::vector<CameraRole>{CameraRole::TopLeft,CameraRole::TopRight,
                                 CameraRole::BottomRight,CameraRole::BottomLeft};
    for (const auto role : roles) {
        const int previous = snapshotRevision;
        controller->requestCalibrationPointFile(mode, role, pointFile_);
        waitFor([this, previous, role] {
            if (snapshotRevision <= previous) return false;
            for (const auto& item : snapshot.roles)
                if (item.role == role) return item.pointFileValid;
            return false;
        }, "point file");
    }
    require(snapshot.state == CalibrationProcessState::ReadyToCapture, "Inputs are incomplete");
    require(button(modeButton("centerCaptureButton","cornerCaptureButton"))->isEnabled(), "Capture must be enabled.");
}

void CalibrationUiSession::waitFor(const std::function<bool()>& predicate, const char* operation)
{
    if (!support::processEventsUntil(predicate, 5s)) {
        record(QString("timeout: ") + operation);
        saveScreenshot("failure.png");
        throw std::runtime_error(std::string("UI phase timed out: ") + operation);
    }
}

QPushButton* CalibrationUiSession::button(const char* name) const
{
    auto* value = workspace->findChild<QPushButton*>(QString::fromLatin1(name));
    require(value != nullptr, "Production button not found.");
    return value;
}

void CalibrationUiSession::clickButton(const char* name)
{
    auto* value = button(name);
    require(value->isEnabled() && value->isVisible(), "Button must be visible and enabled.");
    QTest::mouseClick(value, Qt::LeftButton);
}

const CalibrationRoleSnapshot& CalibrationUiSession::center() const
{
    for (const auto& role : snapshot.roles) if (role.role == activeRole) return role;
    throw std::runtime_error("Center snapshot is missing.");
}

void CalibrationUiSession::detect(bool complete)
{
    const int captures = runtime->captures;
    clickButton(modeButton("centerCaptureButton","cornerCaptureButton"));
    waitFor([this] { return snapshot.state == CalibrationProcessState::ImageReady; }, "capture");
    require(runtime->captures == captures + 1, "Expected exactly one capture.");
    require(!runtime->lightOn, "Calibration capture must switch the light off.");
    require(center().rectifiedImage.size() == QSize(640, 480), "Unexpected captured image size.");
    const int revision = snapshotRevision;
    clickButton(modeButton("centerDetectButton","cornerDetectButton"));
    waitFor([this, complete, revision] {
        return snapshotRevision > revision && snapshot.state == (complete
            ? CalibrationProcessState::SelectingAnchors : CalibrationProcessState::ImageReady);
    }, "detection");
    require(!snapshot.error.has_value(), "Detection returned an error.");
    if (complete) {
        require(center().detectionPoints.size() == 25, "Expected 25 detected circles.");
        std::vector<int> indices;
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 5; ++col) indices.push_back(detectionIndex(row, col));
        std::sort(indices.begin(), indices.end());
        require(std::adjacent_find(indices.begin(), indices.end()) == indices.end(), "Circle matches are not unique.");
    }
    require(!button(modeButton("centerSolveButton","cornerSolveButton"))->isEnabled(), "Solve must wait for P1/P2.");
}

int CalibrationUiSession::detectionIndex(int row, int column) const
{
    const QPointF expected(160 + 80 * column, 80 + 80 * row);
    int result = -1;
    for (int i = 0; i < center().detectionPoints.size(); ++i) {
        const auto delta = center().detectionPoints[i] - expected;
        if (QPointF::dotProduct(delta, delta) <= 1.0) {
            require(result == -1, "Ambiguous detected circle match.");
            result = i;
        }
    }
    require(result >= 0, "Known synthetic circle was not detected within one pixel.");
    return result;
}

QPoint CalibrationUiSession::gridViewport(int row, int column) const
{
    return view->mapFromScene(center().detectionPoints[detectionIndex(row, column)]);
}

void CalibrationUiSession::clickGrid(int row, int column)
{
    const int index = detectionIndex(row, column);
    const int revision = snapshotRevision;
    const qsizetype count = pointClicks->count();
    clickViewport(gridViewport(row, column));
    waitFor([this, revision] { return snapshotRevision > revision; }, "point selection reply");
    require(pointClicks->count() == count + 1, "Expected one UI selection signal.");
    const auto arguments = pointClicks->last();
    require(qvariant_cast<CameraRole>(arguments[0]) == activeRole
        && arguments[1].toInt() == index, "UI selected wrong role or detection index.");
}

void CalibrationUiSession::clickViewport(QPoint position)
{
    require(view->viewport()->rect().contains(position), "Click lies outside the image viewport.");
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
    QApplication::processEvents();
}

void CalibrationUiSession::drag(QPoint start, QPoint delta)
{
    require(view->viewport()->rect().contains(start + delta), "Drag endpoint is outside viewport.");
    QTest::mouseMove(view->viewport(), start);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
    const QPoint end = start + delta;
    QTest::mouseMove(view->viewport(), start + delta / 2);
    QTest::mouseMove(view->viewport(), end);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
    QApplication::processEvents();
}

void CalibrationUiSession::zoom(int angleDelta)
{
    const QPoint point = view->viewport()->rect().center();
    // AnchorUnderMouse reads the real cursor/last move, not only wheel.position().
    QTest::mouseMove(view->viewport(), point);
    QWheelEvent wheel(point, view->viewport()->mapToGlobal(point), {}, {0, angleDelta},
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view->viewport(), &wheel);
    QApplication::processEvents();
}

bool CalibrationUiSession::hasLabel(const QString& text) const
{
    for (auto* item : view->scene()->items()) {
        auto* label = dynamic_cast<QGraphicsSimpleTextItem*>(item);
        if (label && label->isVisible() && label->text() == text) return true;
    }
    return false;
}

void CalibrationUiSession::record(const QString& event)
{
    QJsonObject value{{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"event", event}, {"state", static_cast<int>(snapshot.state)},
        {"revision", snapshotRevision}, {"imageWidth", 640}, {"imageHeight", 480}};
    if (window) {
        value["windowWidth"] = window->width(); value["windowHeight"] = window->height();
        value["devicePixelRatio"] = window->devicePixelRatioF();
        value["logicalDpi"] = window->logicalDpiX();
    }
    for (const auto& role : snapshot.roles) if (role.role == CameraRole::Center) {
        value["detectedCount"] = role.detectionPoints.size();
        value["p1"] = role.p1DetectionIndex ? QJsonValue(*role.p1DetectionIndex) : QJsonValue();
        value["p2"] = role.p2DetectionIndex ? QJsonValue(*role.p2DetectionIndex) : QJsonValue();
    }
    if (snapshot.error) {
        value["errorCode"] = static_cast<int>(snapshot.error->code);
        value["message"] = QString::fromStdString(snapshot.error->message);
        value["diagnostic"] = QString::fromStdString(snapshot.error->diagnostic);
    }
    const QByteArray line = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    require(journal_.write(line) == line.size() && journal_.flush(), "Cannot write UI journal.");
}

void CalibrationUiSession::saveScreenshot(const QString& fileName)
{
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();
    workspace->repaint();
    const QPixmap screenshot = workspace->grab();
    require(!screenshot.isNull() && screenshot.save(outputDirectory + "/" + fileName), "Cannot save UI screenshot.");
    record("screenshot: " + fileName);
}

void CalibrationUiSession::closeAndWait()
{
    if (!started_ || stopped) return;
    controller->shutdown();
    if (!support::processEventsUntil([this] { return stopped; }, 5s)) {
        ADD_FAILURE() << "UI test shutdown timed out; exiting process without destroying a running QThread.";
        std::fflush(nullptr);
        std::_Exit(3);
    }
}

void CalibrationUiSession::runInteractive()
{
    auto* shortcut = new QShortcut(QKeySequence(Qt::Key_F8), window.get());
    int screenshotNumber = 0;
    QObject::connect(shortcut, &QShortcut::activated, window.get(), [this, &screenshotNumber] {
        try {
            const QString name = QString("manual_%1.png").arg(++screenshotNumber, 3, 10, QLatin1Char('0'));
            saveScreenshot(name);
            std::printf("Screenshot: %s\n", qPrintable(QDir::toNativeSeparators(outputDirectory + "/" + name)));
        } catch (const std::exception& error) { ADD_FAILURE() << error.what(); }
    });
    // Manual waiting is event-driven and intentionally has no human-interaction deadline.
    QEventLoop loop;
    QTimer shutdownDeadline;
    shutdownDeadline.setSingleShot(true);
    QObject::connect(window.get(), &MainWindow::applicationExitRequested,
        &shutdownDeadline, [&shutdownDeadline] { shutdownDeadline.start(5000); });
    QObject::connect(&shutdownDeadline, &QTimer::timeout, &loop, [] {
        ADD_FAILURE() << "Manual UI shutdown timed out.";
        std::fflush(nullptr);
        std::_Exit(3);
    });
    // Reuse the same known file after a user reset; the file dialog is outside this test.
    const auto resetConnection = QObject::connect(controller.get(),
        &ApplicationController::calibrationSnapshotChanged, window.get(),
        [this](const CalibrationSnapshot& value) {
            if (value.state == CalibrationProcessState::NoInput && !value.maintenanceLocked) {
                QTimer::singleShot(0, window.get(), [this] {
                    if (stopped) return;
                    controller->requestCalibrationPointFile(CalibrationMode::Center,
                        CameraRole::Center, pointFile_);
                });
            }
        });
    QObject::connect(controller.get(), &ApplicationController::shutdownFinished, &loop, &QEventLoop::quit);
    record("manual_observation_started_not_automatic_acceptance");
    if (!stopped) loop.exec();
    QObject::disconnect(resetConnection);
    delete shortcut;
    record("manual_observation_closed_not_automatic_acceptance");
}

} // namespace vision::test::app
