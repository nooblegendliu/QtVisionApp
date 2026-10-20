#include "ProtocolMapper.h"

#include <gtest/gtest.h>

#include <array>
#include <variant>

namespace vision::test::app {
namespace {

TEST(App_ProtocolMapperTests, MapsEveryModeStateAndErrorEnum) {
    using namespace vision::app;
    EXPECT_EQ(toCoreMode(vision::tcp::TcpLocateMode::Edge), vision::core::LocateMode::Edge);
    EXPECT_EQ(toCoreMode(vision::tcp::TcpLocateMode::MarkPoint), vision::core::LocateMode::MarkPoint);
    EXPECT_EQ(toTcpMode(vision::core::LocateMode::Edge), vision::tcp::TcpLocateMode::Edge);
    EXPECT_EQ(toTcpMode(vision::core::LocateMode::MarkPoint), vision::tcp::TcpLocateMode::MarkPoint);

    const std::array errorCases{
        std::pair{vision::core::ErrorCode::NotReady, vision::tcp::TcpErrorCode::NotReady},
        std::pair{vision::core::ErrorCode::Busy, vision::tcp::TcpErrorCode::Busy},
        std::pair{vision::core::ErrorCode::InvalidConfiguration, vision::tcp::TcpErrorCode::InvalidConfiguration},
        std::pair{vision::core::ErrorCode::HardwareUnavailable, vision::tcp::TcpErrorCode::HardwareUnavailable},
        std::pair{vision::core::ErrorCode::HardwareFailure, vision::tcp::TcpErrorCode::HardwareFailure},
        std::pair{vision::core::ErrorCode::HardwareTimeout, vision::tcp::TcpErrorCode::HardwareTimeout},
        std::pair{vision::core::ErrorCode::InvalidImage, vision::tcp::TcpErrorCode::InvalidImage},
        std::pair{vision::core::ErrorCode::AlgorithmFailure, vision::tcp::TcpErrorCode::AlgorithmFailure},
        std::pair{vision::core::ErrorCode::AlgorithmTimeout, vision::tcp::TcpErrorCode::AlgorithmTimeout},
        std::pair{vision::core::ErrorCode::InvalidResult, vision::tcp::TcpErrorCode::InvalidResult},
        std::pair{vision::core::ErrorCode::InternalFailure, vision::tcp::TcpErrorCode::InternalFailure}};
    for (const auto& [source, expected] : errorCases) EXPECT_EQ(toTcpErrorCode(source), expected);

    EXPECT_EQ(toVisionServiceState(vision::core::CoreState::Ready), VisionServiceState::Ready);
    EXPECT_EQ(toVisionServiceState(vision::core::CoreState::Locating), VisionServiceState::Ready);
    EXPECT_EQ(toVisionServiceState(vision::core::CoreState::Faulted), VisionServiceState::Faulted);
    EXPECT_EQ(toVisionServiceState(vision::core::CoreState::ShuttingDown), VisionServiceState::Stopped);
    EXPECT_EQ(toVisionServiceState(vision::core::CoreState::Stopped), VisionServiceState::Stopped);

    const std::array applicationCases{
        std::pair{ApplicationState::Starting, vision::tcp::TcpApplicationState::Starting},
        std::pair{ApplicationState::Ready, vision::tcp::TcpApplicationState::Ready},
        std::pair{ApplicationState::StartFailed, vision::tcp::TcpApplicationState::Starting},
        std::pair{ApplicationState::ShuttingDown, vision::tcp::TcpApplicationState::ShuttingDown},
        std::pair{ApplicationState::Stopped, vision::tcp::TcpApplicationState::Stopped}};
    for (const auto& [source, expected] : applicationCases) EXPECT_EQ(toTcpApplicationState(source), expected);

    const std::array visionCases{
        std::pair{VisionServiceState::Starting, vision::tcp::TcpVisionState::Starting},
        std::pair{VisionServiceState::Ready, vision::tcp::TcpVisionState::Ready},
        std::pair{VisionServiceState::Faulted, vision::tcp::TcpVisionState::Faulted},
        std::pair{VisionServiceState::Recovering, vision::tcp::TcpVisionState::Recovering},
        std::pair{VisionServiceState::Reconfiguring, vision::tcp::TcpVisionState::Recovering},
        std::pair{VisionServiceState::Stopped, vision::tcp::TcpVisionState::Stopped}};
    for (const auto& [source, expected] : visionCases) EXPECT_EQ(toTcpVisionState(source), expected);

    const std::array taskCases{
        std::pair{TaskState::Idle, vision::tcp::TcpTaskState::Idle},
        std::pair{TaskState::Preparing, vision::tcp::TcpTaskState::Preparing},
        std::pair{TaskState::Running, vision::tcp::TcpTaskState::Running},
        std::pair{TaskState::AwaitingResultAck, vision::tcp::TcpTaskState::AwaitingResultAck}};
    for (const auto& [source, expected] : taskCases) EXPECT_EQ(toTcpTaskState(source), expected);
}

vision::app::TcpTaskContext taskContext() {
    vision::app::TcpTaskContext task;
    task.taskId = QStringLiteral("42");
    task.connection = vision::tcp::ConnectionContext{7};
    task.tcpMode = vision::tcp::TcpLocateMode::MarkPoint;
    task.options = vision::core::LocateOptions{vision::core::LocateMode::MarkPoint};
    return task;
}

TEST(App_ProtocolMapperTests, LocateReplyPreservesOkZerosNgAndMapsStableError) {
    const auto task = taskContext();
    const vision::core::LocateOutcome ok = vision::core::LocateResult{
        1.25, -2.5, 3.75, vision::core::LocateDecision::Ok};
    auto reply = vision::app::makeLocateReply(task, ok);
    EXPECT_EQ(reply.outcome, vision::tcp::LocateReplyOutcome::Ok);
    EXPECT_DOUBLE_EQ(reply.x, 1.25);
    EXPECT_DOUBLE_EQ(reply.y, -2.5);
    EXPECT_DOUBLE_EQ(reply.r, 3.75);
    EXPECT_EQ(reply.taskId, task.taskId);
    EXPECT_EQ(reply.connection, task.connection);

    const vision::core::LocateOutcome ng = vision::core::LocateResult{
        99.0, 98.0, 97.0, vision::core::LocateDecision::Ng};
    reply = vision::app::makeLocateReply(task, ng);
    EXPECT_EQ(reply.outcome, vision::tcp::LocateReplyOutcome::Ng);
    EXPECT_DOUBLE_EQ(reply.x, 0.0);
    EXPECT_DOUBLE_EQ(reply.y, 0.0);
    EXPECT_DOUBLE_EQ(reply.r, 0.0);

    const vision::core::LocateOutcome error = vision::core::Error{
        vision::core::ErrorCode::InvalidImage, "invalid image", "detail"};
    reply = vision::app::makeLocateReply(task, error);
    EXPECT_EQ(reply.outcome, vision::tcp::LocateReplyOutcome::Error);
    EXPECT_EQ(reply.errorCode, vision::tcp::TcpErrorCode::InvalidImage);
    EXPECT_EQ(reply.message, QStringLiteral("invalid image"));
}

TEST(App_ProtocolMapperTests, TaskDisplayResultMaintainsPoseAndErrorSemantics) {
    const auto task = taskContext();
    auto display = vision::app::makeTaskDisplayResult(
        task, vision::core::LocateResult{1.0, 2.0, 3.0, vision::core::LocateDecision::Ok});
    EXPECT_EQ(display.outcome, QStringLiteral("OK"));
    EXPECT_TRUE(display.hasPose);
    EXPECT_DOUBLE_EQ(display.x, 1.0);
    EXPECT_FALSE(display.error.has_value());

    display = vision::app::makeTaskDisplayResult(
        task, vision::core::LocateResult{8.0, 9.0, 10.0, vision::core::LocateDecision::Ng});
    EXPECT_EQ(display.outcome, QStringLiteral("NG"));
    EXPECT_TRUE(display.hasPose);
    EXPECT_DOUBLE_EQ(display.x, 0.0);
    EXPECT_DOUBLE_EQ(display.y, 0.0);
    EXPECT_DOUBLE_EQ(display.r, 0.0);

    display = vision::app::makeTaskDisplayResult(
        task, vision::core::Error{vision::core::ErrorCode::Busy, "busy", "detail"});
    EXPECT_EQ(display.outcome, QStringLiteral("ERROR"));
    EXPECT_FALSE(display.hasPose);
    ASSERT_TRUE(display.error.has_value());
    EXPECT_EQ(display.error->code, vision::core::ErrorCode::Busy);
}

} // namespace
} // namespace vision::test::app
