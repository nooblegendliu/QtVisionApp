#include <VisionHardware/HikHardware.h>

#include "MvCamera.h"
#include "MvLightController.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <variant>

#include "VisionHardware/support/HikHardwareTestConfig.h"

namespace vision::test::hardware {
namespace {

namespace hik = vision::hardware::hik;

static_assert(!std::is_copy_constructible_v<hik::MvCamera>);
static_assert(!std::is_copy_assignable_v<hik::MvCamera>);
static_assert(!std::is_move_constructible_v<hik::MvCamera>);
static_assert(!std::is_move_assignable_v<hik::MvCamera>);
static_assert(!std::is_copy_constructible_v<hik::MvLightController>);
static_assert(!std::is_copy_assignable_v<hik::MvLightController>);
static_assert(!std::is_move_constructible_v<hik::MvLightController>);
static_assert(!std::is_move_assignable_v<hik::MvLightController>);

TEST(DISABLED_Hardware_Mvs_HikMvsEnvironmentSmokeTests, MissingControllerReturnsNoPartialPorts) {
    RecordingHardwareDiagnostics diagnostics;
    const auto outcome = vision::hardware::createHikHardware(
        validHikCameraConfiguration(), validHikLightConfiguration(), nullptr, &diagnostics);

    ASSERT_TRUE(std::holds_alternative<vision::core::Error>(outcome));
    const auto& error = std::get<vision::core::Error>(outcome);
    EXPECT_EQ(error.code, vision::core::ErrorCode::HardwareUnavailable);
    EXPECT_FALSE(error.diagnostic.empty());
    EXPECT_EQ(diagnostics.count("sdk.initialize"), 1U);
    EXPECT_EQ(diagnostics.count("light.enumerate"), 1U);
    EXPECT_EQ(diagnostics.count("sdk.finalize"), 1U);
    EXPECT_FALSE(diagnostics.discarded);
}

} // namespace
} // namespace vision::test::hardware
