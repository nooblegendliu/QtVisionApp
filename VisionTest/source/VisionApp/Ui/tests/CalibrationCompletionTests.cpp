#include "VisionApp/Ui/support/CalibrationUiTestSupport.h"
#include <gtest/gtest.h>
#include <QPushButton>

namespace vision::test::app { namespace {
using namespace vision::app;
using vision::core::CameraRole;

TEST(DISABLED_App_Ui_CalibrationCompletion, CenterAndFourCornersEvaluateApplyResetAndReenter) {
    for (auto mode : {CalibrationMode::Center, CalibrationMode::FourCorners}) {
        SCOPED_TRACE(static_cast<int>(mode));
        CalibrationUiSession s(mode == CalibrationMode::Center ? "CenterComplete" : "CornersComplete");
        s.start();
        if (mode == CalibrationMode::FourCorners) s.switchMode(mode);
        s.detect();
        const std::vector<CameraRole> roles = mode == CalibrationMode::Center
            ? std::vector<CameraRole>{CameraRole::Center}
            : std::vector<CameraRole>{CameraRole::BottomRight, CameraRole::TopLeft,
                                     CameraRole::BottomLeft, CameraRole::TopRight};
        for (const auto role : roles) {
            s.selectRole(role);
            ASSERT_EQ(s.center().detectionPoints.size(),25);
            s.clickGrid(0,0);s.clickGrid(0,1);
            ASSERT_TRUE(s.center().p1DetectionIndex);
            ASSERT_TRUE(s.center().p2DetectionIndex);
        }
        ASSERT_EQ(s.snapshot.state, CalibrationProcessState::ReadyToSolve);
        s.clickButton(s.modeButton("centerSolveButton","cornerSolveButton"));
        s.waitFor([&]{return s.snapshot.state == CalibrationProcessState::CandidateReady;},"solve");
        s.clickButton(s.modeButton("centerVerifyButton","cornerVerifyButton"));
        s.waitFor([&]{return s.snapshot.state == CalibrationProcessState::ReadyToApply;},"quality");
        for (const auto role : roles) {
            s.selectRole(role);
            EXPECT_TRUE(s.center().passed);EXPECT_EQ(s.center().points.size(),25);
        }
        s.saveScreenshot("verified.png");
        const int factories=s.runtime->factoryCalls;
        s.clickButton(s.modeButton("centerApplyButton","cornerApplyButton"));
        s.waitFor([&]{return s.snapshot.state == CalibrationProcessState::Completed;},"apply");
        EXPECT_EQ(s.runtime->factoryCalls,factories+1);
        EXPECT_FALSE(s.controller->snapshot().calibrationMaintenanceLocked);
        s.clickButton(s.modeButton("centerResetButton","cornerResetButton"));
        s.waitFor([&]{return s.snapshot.state == CalibrationProcessState::NoInput;},"reset");
        for (const auto& role:s.snapshot.roles) {
            EXPECT_FALSE(role.hasSolution);EXPECT_TRUE(role.detectionPoints.empty());
        }
        s.prepareInput();s.detect();
        EXPECT_EQ(s.runtime->locateCalls,0);
    }
}

TEST(DISABLED_App_Ui_CalibrationCompletion, MissingFourthGridCannotSolveAndRecaptureRestoresSelection) {
    CalibrationUiSession s("CornerMissingAndRetry");s.start();s.switchMode(CalibrationMode::FourCorners);
    s.runtime->missingCircle=true;s.detect(false);
    s.selectRole(CameraRole::BottomLeft);
    EXPECT_TRUE(s.center().detectionPoints.empty());
    EXPECT_FALSE(s.button("cornerSolveButton")->isEnabled());
    EXPECT_FALSE(s.button("cornerApplyButton")->isEnabled());
    s.runtime->missingCircle=false;s.detect();
    for (const auto role:{CameraRole::TopLeft,CameraRole::TopRight,CameraRole::BottomRight,CameraRole::BottomLeft}) {
        s.selectRole(role);s.clickGrid(0,0);s.clickGrid(0,1);
    }
    EXPECT_TRUE(s.button("cornerSolveButton")->isEnabled());
}
} }
