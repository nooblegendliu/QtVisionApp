#pragma once

#include "VisionRuntimeFactory.h"

namespace vision::app {

// Development-only runtime. Activation is additionally guarded by main().
[[nodiscard]] CoreFactory makeSimulationCoreFactory(
    SimulationConfig config);

} // namespace vision::app
