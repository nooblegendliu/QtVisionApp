#include "ProtocolMapper.h"

#include <variant>

namespace vision::app {

vision::core::LocateMode toCoreMode(
    vision::tcp::TcpLocateMode mode) noexcept
{
    return mode == vision::tcp::TcpLocateMode::Edge
        ? vision::core::LocateMode::Edge
        : vision::core::LocateMode::MarkPoint;
}

vision::tcp::TcpLocateMode toTcpMode(
    vision::core::LocateMode mode) noexcept
{
    return mode == vision::core::LocateMode::Edge
        ? vision::tcp::TcpLocateMode::Edge
        : vision::tcp::TcpLocateMode::MarkPoint;
}

vision::tcp::TcpErrorCode toTcpErrorCode(
    vision::core::ErrorCode code) noexcept
{
    using vision::core::ErrorCode;
    using vision::tcp::TcpErrorCode;
    switch (code) {
    case ErrorCode::NotReady: return TcpErrorCode::NotReady;
    case ErrorCode::Busy: return TcpErrorCode::Busy;
    case ErrorCode::InvalidConfiguration: return TcpErrorCode::InvalidConfiguration;
    case ErrorCode::HardwareUnavailable: return TcpErrorCode::HardwareUnavailable;
    case ErrorCode::HardwareFailure: return TcpErrorCode::HardwareFailure;
    case ErrorCode::HardwareTimeout: return TcpErrorCode::HardwareTimeout;
    case ErrorCode::InvalidImage: return TcpErrorCode::InvalidImage;
    case ErrorCode::AlgorithmFailure: return TcpErrorCode::AlgorithmFailure;
    case ErrorCode::AlgorithmTimeout: return TcpErrorCode::AlgorithmTimeout;
    case ErrorCode::InvalidResult: return TcpErrorCode::InvalidResult;
    case ErrorCode::InternalFailure: return TcpErrorCode::InternalFailure;
    }
    return TcpErrorCode::InternalFailure;
}

VisionServiceState toVisionServiceState(
    vision::core::CoreState state) noexcept
{
    using vision::core::CoreState;
    switch (state) {
    case CoreState::Ready:
    case CoreState::Locating:
        return VisionServiceState::Ready;
    case CoreState::Faulted:
        return VisionServiceState::Faulted;
    case CoreState::ShuttingDown:
    case CoreState::Stopped:
        return VisionServiceState::Stopped;
    }
    return VisionServiceState::Faulted;
}

vision::tcp::TcpApplicationState toTcpApplicationState(
    ApplicationState state) noexcept
{
    using vision::tcp::TcpApplicationState;
    switch (state) {
    case ApplicationState::Starting:
    case ApplicationState::StartFailed:
        return TcpApplicationState::Starting;
    case ApplicationState::Ready:
        return TcpApplicationState::Ready;
    case ApplicationState::ShuttingDown:
        return TcpApplicationState::ShuttingDown;
    case ApplicationState::Stopped:
        return TcpApplicationState::Stopped;
    }
    return TcpApplicationState::Stopped;
}

vision::tcp::TcpVisionState toTcpVisionState(
    VisionServiceState state) noexcept
{
    using vision::tcp::TcpVisionState;
    switch (state) {
    case VisionServiceState::Starting: return TcpVisionState::Starting;
    case VisionServiceState::Ready: return TcpVisionState::Ready;
    case VisionServiceState::Faulted: return TcpVisionState::Faulted;
    case VisionServiceState::Recovering: return TcpVisionState::Recovering;
    case VisionServiceState::Reconfiguring: return TcpVisionState::Recovering;
    case VisionServiceState::Stopped: return TcpVisionState::Stopped;
    }
    return TcpVisionState::Stopped;
}

vision::tcp::TcpTaskState toTcpTaskState(TaskState state) noexcept
{
    using vision::tcp::TcpTaskState;
    switch (state) {
    case TaskState::Idle: return TcpTaskState::Idle;
    case TaskState::Preparing: return TcpTaskState::Preparing;
    case TaskState::Running: return TcpTaskState::Running;
    case TaskState::AwaitingResultAck: return TcpTaskState::AwaitingResultAck;
    }
    return TcpTaskState::Idle;
}

vision::tcp::LocateResultReply makeLocateReply(
    const TcpTaskContext& task,
    const vision::core::LocateOutcome& outcome)
{
    vision::tcp::LocateResultReply reply;
    reply.taskId = task.taskId;
    reply.connection = task.connection;
    reply.mode = task.tcpMode;

    if (const auto* result =
            std::get_if<vision::core::LocateResult>(&outcome)) {
        if (result->decision == vision::core::LocateDecision::Ok) {
            reply.outcome = vision::tcp::LocateReplyOutcome::Ok;
            reply.x = result->x;
            reply.y = result->y;
            reply.r = result->r;
        }
        else {
            reply.outcome = vision::tcp::LocateReplyOutcome::Ng;
            reply.x = 0.0;
            reply.y = 0.0;
            reply.r = 0.0;
        }
        return reply;
    }

    const auto& error = std::get<vision::core::Error>(outcome);
    reply.outcome = vision::tcp::LocateReplyOutcome::Error;
    reply.errorCode = toTcpErrorCode(error.code);
    reply.message = QString::fromStdString(error.message);
    return reply;
}

TaskDisplayResult makeTaskDisplayResult(
    const TaskContext& task,
    const vision::core::LocateOutcome& outcome)
{
    TaskDisplayResult display;
    display.mode = task.options.mode;
    display.taskLabel = task.taskId;

    if (const auto* result =
            std::get_if<vision::core::LocateResult>(&outcome)) {
        if (result->decision == vision::core::LocateDecision::Ok) {
            display.outcome = QStringLiteral("OK");
            display.hasPose = true;
            display.x = result->x;
            display.y = result->y;
            display.r = result->r;
        }
        else {
            display.outcome = QStringLiteral("NG");
            display.hasPose = true;
            display.x = 0.0;
            display.y = 0.0;
            display.r = 0.0;
        }
    }
    else {
        display.outcome = QStringLiteral("ERROR");
        display.error = std::get<vision::core::Error>(outcome);
    }
    return display;
}

} // namespace vision::app
