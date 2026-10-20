#include "VisionApp/Ui/support/CalibrationUiTestSupport.h"
#include <QPushButton>
#include <QGraphicsPixmapItem>
#include <QScrollBar>
#include <QTimer>
#include <QTest>
#include <QFileInfo>
#include <gtest/gtest.h>
#include <array>
#include <cmath>

namespace vision::test::app {
namespace {
using vision::app::CalibrationProcessState;

class DISABLED_App_Ui_CalibrationUiSelectionTests : public testing::Test {
protected:
    void SetUp() override
    {
        session = std::make_unique<CalibrationUiSession>(
            QString::fromLatin1(testing::UnitTest::GetInstance()->current_test_info()->name()));
        session->start();
    }
    void TearDown() override { session.reset(); }
    std::unique_ptr<CalibrationUiSession> session;
};

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, MainFlowScreenshotsAndSolve)
{
    auto& s = *session;
    s.detect();
    EXPECT_EQ(s.runtime->captures.load(), 1);
    ASSERT_FALSE(s.center().p1DetectionIndex);
    ASSERT_FALSE(s.center().p2DetectionIndex);
    bool imageShown = false;
    for (auto* item : s.view->scene()->items()) {
        if (auto* image = dynamic_cast<QGraphicsPixmapItem*>(item))
            imageShown |= !image->pixmap().isNull() && image->pixmap().size() == QSize(640, 480);
    }
    EXPECT_TRUE(imageShown);
    {
        std::lock_guard lock(s.runtime->mutex);
        const auto& changes = s.runtime->lightChanges;
        ASSERT_GE(changes.size(), 2U);
        EXPECT_TRUE(changes[changes.size() - 2]);
        EXPECT_FALSE(changes.back());
    }
    s.saveScreenshot("01_detected.png");
    s.clickGrid(0, 0);
    EXPECT_EQ(s.center().p1DetectionIndex, s.detectionIndex(0, 0));
    EXPECT_FALSE(s.center().p2DetectionIndex);
    EXPECT_TRUE(s.hasLabel("P1"));
    EXPECT_FALSE(s.hasLabel("P2"));
    EXPECT_FALSE(s.button("centerSolveButton")->isEnabled());
    s.saveScreenshot("02_p1_selected.png");
    s.clickGrid(0, 1);
    EXPECT_EQ(s.center().p2DetectionIndex, s.detectionIndex(0, 1));
    EXPECT_EQ(s.snapshot.state, CalibrationProcessState::ReadyToSolve);
    EXPECT_TRUE(s.hasLabel("P1"));
    EXPECT_TRUE(s.hasLabel("P2"));
    ASSERT_TRUE(s.button("centerSolveButton")->isEnabled());
    s.saveScreenshot("03_p2_selected.png");
    s.clickButton("centerSolveButton");
    s.waitFor([&] { return s.snapshot.state == CalibrationProcessState::CandidateReady; }, "candidate solve");
    ASSERT_TRUE(s.center().hasSolution);
    ASSERT_EQ(s.center().points.size(), 25);
    const auto& h = s.center().imageToPhysical;
    // Independent known synthetic geometry, not solver-produced residuals.
    for (int row = 0; row < 5; ++row) for (int col = 0; col < 5; ++col) {
        const double x = 160 + 80 * col, y = 80 + 80 * row;
        const double divisor = h[6] * x + h[7] * y + h[8];
        ASSERT_GT(std::abs(divisor), 1e-9);
        EXPECT_NEAR((h[0] * x + h[1] * y + h[2]) / divisor, col * 10, 0.2);
        EXPECT_NEAR((h[3] * x + h[4] * y + h[5]) / divisor, row * 10, 0.2);
    }
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, EightCornerNeighbourDirections)
{
    session.reset();
    const std::array<std::array<int, 4>, 8> selections{{
        {0,0,0,1}, {0,0,1,0}, {0,4,0,3}, {0,4,1,4},
        {4,4,4,3}, {4,4,3,4}, {4,0,4,1}, {4,0,3,0}}};
    for (std::size_t i = 0; i < selections.size(); ++i) {
        SCOPED_TRACE(i);
        // Separate controller, resources and maintenance session for each direction.
        CalibrationUiSession run(QString("EightCornerNeighbourDirections/direction_%1").arg(i));
        run.start();
        run.detect();
        const auto& p = selections[i];
        run.clickGrid(p[0], p[1]);
        run.clickGrid(p[2], p[3]);
        EXPECT_EQ(run.center().p1DetectionIndex, run.detectionIndex(p[0], p[1]));
        EXPECT_EQ(run.center().p2DetectionIndex, run.detectionIndex(p[2], p[3]));
        EXPECT_EQ(run.snapshot.state, CalibrationProcessState::ReadyToSolve);
        EXPECT_TRUE(run.hasLabel("P1") && run.hasLabel("P2"));
        EXPECT_TRUE(run.button("centerSolveButton")->isEnabled());
    }
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, RejectsInteriorP1)
{
    auto& s = *session;
    s.detect(); s.clickGrid(2, 2);
    EXPECT_FALSE(s.center().p1DetectionIndex);
    EXPECT_FALSE(s.center().p2DetectionIndex);
    EXPECT_FALSE(s.hasLabel("P1"));
    EXPECT_EQ(s.snapshot.state, CalibrationProcessState::SelectingAnchors);
    EXPECT_FALSE(s.button("centerSolveButton")->isEnabled());
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, InvalidP2CanRetry)
{
    auto& s = *session;
    s.detect(); s.clickGrid(0, 0);
    for (const auto& cell : {QPoint(1, 1), QPoint(2, 0)}) {
        s.clickGrid(cell.y(), cell.x());
        EXPECT_EQ(s.center().p1DetectionIndex, s.detectionIndex(0, 0));
        EXPECT_FALSE(s.center().p2DetectionIndex);
        EXPECT_FALSE(s.hasLabel("P2"));
        EXPECT_FALSE(s.button("centerSolveButton")->isEnabled());
    }
    s.clickGrid(0, 1);
    EXPECT_FALSE(s.snapshot.error);
    EXPECT_EQ(s.snapshot.state, CalibrationProcessState::ReadyToSolve);
    EXPECT_TRUE(s.hasLabel("P1") && s.hasLabel("P2"));
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, BlankClickPreservesSelection)
{
    auto& s = *session;
    s.detect(); s.clickGrid(0, 0);
    const auto count = s.pointClicks->count();
    s.clickViewport(s.view->mapFromScene(QPointF(320, 40)));
    EXPECT_EQ(s.pointClicks->count(), count);
    EXPECT_EQ(s.center().p1DetectionIndex, s.detectionIndex(0, 0));
    EXPECT_FALSE(s.center().p2DetectionIndex);
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, DragDoesNotSelect)
{
    auto& s = *session;
    s.detect();
    for (int i = 0; i < 4; ++i) s.zoom(120);
    const auto count = s.pointClicks->count();
    const QPoint target = s.gridViewport(2, 2);
    s.drag(target - QPoint(35, 20), QPoint(35, 20));
    EXPECT_EQ(s.pointClicks->count(), count);
    EXPECT_FALSE(s.center().p1DetectionIndex);
    EXPECT_FALSE(s.hasLabel("P1"));
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, ZoomAndPanPreserveCircleMapping)
{
    auto& s = *session;
    s.detect();
    const double oldScale = s.view->transform().m11();
    s.zoom(120); s.zoom(120);
    EXPECT_GT(s.view->transform().m11(), oldScale);
    const QPoint before = s.gridViewport(2, 2);
    s.drag(s.view->viewport()->rect().center(), QPoint(20, 15));
    EXPECT_NE(s.gridViewport(2, 2), before);
    EXPECT_EQ(s.pointClicks->count(), 0);
    s.clickGrid(0, 0); s.clickGrid(0, 1);
    EXPECT_EQ(s.center().p1DetectionIndex, s.detectionIndex(0, 0));
    EXPECT_EQ(s.center().p2DetectionIndex, s.detectionIndex(0, 1));
    EXPECT_EQ(s.snapshot.state, CalibrationProcessState::ReadyToSolve);
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, SelectingNewCornerClearsOldP2)
{
    auto& s = *session;
    s.detect(); s.clickGrid(0, 0); s.clickGrid(0, 1);
    s.clickGrid(4, 4);
    EXPECT_EQ(s.center().p1DetectionIndex, s.detectionIndex(4, 4));
    EXPECT_FALSE(s.center().p2DetectionIndex);
    EXPECT_TRUE(s.hasLabel("P1")); EXPECT_FALSE(s.hasLabel("P2"));
    EXPECT_FALSE(s.button("centerSolveButton")->isEnabled());
    s.clickGrid(4, 3);
    EXPECT_EQ(s.snapshot.state, CalibrationProcessState::ReadyToSolve);
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, ResetAllowsNewCaptureAndSelection)
{
    auto& s = *session;
    s.detect(); s.clickGrid(0, 0); s.clickGrid(0, 1);
    s.clickButton("centerResetButton");
    s.waitFor([&] { return s.snapshot.state == CalibrationProcessState::NoInput
        && !s.snapshot.maintenanceLocked; }, "reset");
    EXPECT_FALSE(s.center().p1DetectionIndex);
    EXPECT_FALSE(s.center().p2DetectionIndex);
    EXPECT_FALSE(s.hasLabel("P1") || s.hasLabel("P2"));
    EXPECT_FALSE(s.controller->snapshot().calibrationMaintenanceLocked);
    s.prepareInput(); s.detect(); s.clickGrid(0, 0); s.clickGrid(0, 1);
    EXPECT_EQ(s.runtime->captures.load(), 2);
    EXPECT_EQ(s.snapshot.state, CalibrationProcessState::ReadyToSolve);
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, IncompleteGridDoesNotEnableSelection)
{
    auto& s = *session;
    s.runtime->missingCircle = true;
    s.detect(false);
    EXPECT_TRUE(s.center().detectionPoints.isEmpty());
    const auto count = s.pointClicks->count();
    s.clickViewport(s.view->mapFromScene(QPointF(160, 80)));
    EXPECT_EQ(s.pointClicks->count(), count);
    EXPECT_FALSE(s.center().p1DetectionIndex);
    EXPECT_FALSE(s.button("centerSolveButton")->isEnabled());
}

TEST_F(DISABLED_App_Ui_CalibrationUiSelectionTests, InteractiveF8AndCloseLifecycle)
{
    auto& s = *session;
    s.window->activateWindow();
    QTimer::singleShot(200, s.window.get(), [&s] {
        QTest::keyClick(s.window.get(), Qt::Key_F8);
        s.window->close();
    });
    s.runInteractive();
    EXPECT_TRUE(s.stopped);
    EXPECT_TRUE(QFileInfo::exists(s.outputDirectory + "/manual_001.png"));
}
} // namespace
} // namespace vision::test::app
