#include "DiagnosticBridge.h"

#include <gtest/gtest.h>

#include <optional>

#include "TestSupport/QtEventLoopTestSupport.h"

namespace vision::test::app {
namespace {

TEST(App_DiagnosticBridgeTests, SubmitCopiesFieldsFrameContextAndEmitsExactlyOnce) {
    support::ensureQCoreApplication();
    vision::app::DiagnosticBridge bridge;
    std::optional<vision::app::AppDiagnostic> received;
    std::size_t signalCount = 0;
    QObject::connect(&bridge, &vision::app::DiagnosticBridge::diagnosticReady,
                     &bridge, [&](const auto& diagnostic) {
                         ++signalCount;
                         received = diagnostic;
                     });

    {
        vision::core::DiagnosticEvent source{
            vision::core::DiagnosticSeverity::Warning,
            "VisionCore",
            "locate.capture",
            "capture warning",
            {{std::chrono::steady_clock::time_point{std::chrono::milliseconds{5}},
              77U, "center-camera", vision::core::CameraRole::Center}}};
        bridge.submit(source);
        source.component = "changed";
        source.frames.front().cameraId = "changed-camera";
    }

    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(signalCount, 1U);
    EXPECT_EQ(received->severity, vision::core::DiagnosticSeverity::Warning);
    EXPECT_EQ(received->component, QStringLiteral("VisionCore"));
    EXPECT_EQ(received->operation, QStringLiteral("locate.capture"));
    EXPECT_EQ(received->message, QStringLiteral("capture warning"));
    ASSERT_EQ(received->frames.size(), 1);
    EXPECT_EQ(received->frames.front(), QStringLiteral("camera=center-camera role=1 frame=77"));
    EXPECT_TRUE(received->occurredAt.isValid());
}

} // namespace
} // namespace vision::test::app
