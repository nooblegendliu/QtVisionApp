#include "VisionApp/Ui/support/CalibrationUiTestSupport.h"
#include <gtest/gtest.h>

namespace vision::test::app {
TEST(DISABLED_App_ManualUi_CalibrationUiInteractiveTests, CenterGrid5x5)
{
    CalibrationUiSession session("Interactive");
    session.start();
    session.runInteractive();
    // Closing the demonstration is not evidence of automatic interaction coverage.
    EXPECT_TRUE(session.stopped);
}
} // namespace vision::test::app
