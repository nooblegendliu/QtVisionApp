#pragma once

#include "AppTypes.h"

#include <VisionTcp/VisionTcpTypes.h>

namespace vision::app {

[[nodiscard]] vision::core::LocateMode toCoreMode(
    vision::tcp::TcpLocateMode mode) noexcept;
[[nodiscard]] vision::tcp::TcpLocateMode toTcpMode(
    vision::core::LocateMode mode) noexcept;
[[nodiscard]] vision::tcp::TcpErrorCode toTcpErrorCode(
    vision::core::ErrorCode code) noexcept;
[[nodiscard]] VisionServiceState toVisionServiceState(
    vision::core::CoreState state) noexcept;
[[nodiscard]] vision::tcp::TcpApplicationState toTcpApplicationState(
    ApplicationState state) noexcept;
[[nodiscard]] vision::tcp::TcpVisionState toTcpVisionState(
    VisionServiceState state) noexcept;
[[nodiscard]] vision::tcp::TcpTaskState toTcpTaskState(
    TaskState state) noexcept;

[[nodiscard]] vision::tcp::LocateResultReply makeLocateReply(
    const TcpTaskContext& task,
    const vision::core::LocateOutcome& outcome);
[[nodiscard]] TaskDisplayResult makeTaskDisplayResult(
    const TaskContext& task,
    const vision::core::LocateOutcome& outcome);

} // namespace vision::app
