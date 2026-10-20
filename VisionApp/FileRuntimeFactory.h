#pragma once

#include "VisionRuntimeFactory.h"

#include <filesystem>

namespace vision::app {

// Explicit offline development backend: file-backed Center frame, real algorithm.
// Loading, capture and destruction happen in the existing vision worker thread.
[[nodiscard]] CoreFactory makeFileCoreFactory(std::filesystem::path imagePath);

} // namespace vision::app
