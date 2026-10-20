#include "VisionTcp/support/VisionTcpServerFixture.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>

namespace vision::test::tcp {
namespace {

class Tcp_VisionTcpRepliesTests : public VisionTcpServerFixture {};

TEST_F(Tcp_VisionTcpRepliesTests, SerializesAllReplyTypesAndOptionalFields) {
    startServer();
    support::JsonLineClient peer;
    const auto connection = connectClient(peer);
    ASSERT_TRUE(connection.isValid());

    vision::tcp::LocatePreparedReply prepared;
    prepared.taskId = QStringLiteral("11");
    prepared.mode = vision::tcp::TcpLocateMode::MarkPoint;
    prepared.connection = connection;
    ASSERT_TRUE(server_->sendLocatePrepared(prepared));
    auto object = peer.takeObject();
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("type")).toString(), QStringLiteral("locatePrepared"));
    EXPECT_EQ(object->value(QStringLiteral("taskId")).toString(), QStringLiteral("11"));
    EXPECT_EQ(object->value(QStringLiteral("mode")).toString(), QStringLiteral("MARK_POINT"));

    vision::tcp::PrepareRejectedReply rejected;
    rejected.taskId = QStringLiteral("12");
    rejected.errorCode = vision::tcp::TcpErrorCode::Busy;
    rejected.message = QStringLiteral("busy");
    rejected.activeTaskId = QStringLiteral("10");
    rejected.connection = connection;
    ASSERT_TRUE(server_->sendPrepareRejected(rejected));
    object = peer.takeObject();
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("type")).toString(), QStringLiteral("prepareRejected"));
    EXPECT_EQ(object->value(QStringLiteral("errorCode")).toString(), QStringLiteral("BUSY"));
    EXPECT_EQ(object->value(QStringLiteral("activeTaskId")).toString(), QStringLiteral("10"));

    vision::tcp::LocateResultReply ok;
    ok.taskId = QStringLiteral("13");
    ok.mode = vision::tcp::TcpLocateMode::Edge;
    ok.outcome = vision::tcp::LocateReplyOutcome::Ok;
    ok.x = 1.25;
    ok.y = -2.5;
    ok.r = 3.75;
    ok.connection = connection;
    ASSERT_TRUE(server_->sendLocateResult(ok));
    object = peer.takeObject();
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("outcome")).toString(), QStringLiteral("OK"));
    EXPECT_DOUBLE_EQ(object->value(QStringLiteral("x")).toDouble(), 1.25);
    EXPECT_DOUBLE_EQ(object->value(QStringLiteral("y")).toDouble(), -2.5);
    EXPECT_DOUBLE_EQ(object->value(QStringLiteral("r")).toDouble(), 3.75);
    EXPECT_FALSE(object->contains(QStringLiteral("errorCode")));

    auto ng = ok;
    ng.taskId = QStringLiteral("14");
    ng.outcome = vision::tcp::LocateReplyOutcome::Ng;
    ng.x = 99.0;
    ng.y = 98.0;
    ng.r = 97.0;
    ASSERT_TRUE(server_->sendLocateResult(ng));
    object = peer.takeObject();
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("outcome")).toString(), QStringLiteral("NG"));
    EXPECT_DOUBLE_EQ(object->value(QStringLiteral("x")).toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(object->value(QStringLiteral("y")).toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(object->value(QStringLiteral("r")).toDouble(), 0.0);

    auto error = ok;
    error.taskId = QStringLiteral("15");
    error.outcome = vision::tcp::LocateReplyOutcome::Error;
    error.errorCode = vision::tcp::TcpErrorCode::AlgorithmTimeout;
    error.message = QStringLiteral("timed out");
    ASSERT_TRUE(server_->sendLocateResult(error));
    object = peer.takeObject();
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("outcome")).toString(), QStringLiteral("ERROR"));
    EXPECT_EQ(object->value(QStringLiteral("errorCode")).toString(), QStringLiteral("ALGORITHM_TIMEOUT"));
    EXPECT_EQ(object->value(QStringLiteral("message")).toString(), QStringLiteral("timed out"));
    EXPECT_FALSE(object->contains(QStringLiteral("x")));

    vision::tcp::StatusReply status;
    status.sequence = std::numeric_limits<quint32>::max();
    status.applicationState = vision::tcp::TcpApplicationState::Ready;
    status.visionState = vision::tcp::TcpVisionState::Faulted;
    status.taskState = vision::tcp::TcpTaskState::AwaitingResultAck;
    status.activeTaskId = QStringLiteral("15");
    status.lastErrorCode = vision::tcp::TcpErrorCode::HardwareFailure;
    status.connection = connection;
    ASSERT_TRUE(server_->sendStatus(status));
    object = peer.takeObject();
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("type")).toString(), QStringLiteral("statusReply"));
    EXPECT_EQ(object->value(QStringLiteral("applicationState")).toString(), QStringLiteral("READY"));
    EXPECT_EQ(object->value(QStringLiteral("visionState")).toString(), QStringLiteral("FAULTED"));
    EXPECT_EQ(object->value(QStringLiteral("taskState")).toString(), QStringLiteral("AWAITING_RESULT_ACK"));
    EXPECT_EQ(object->value(QStringLiteral("activeTaskId")).toString(), QStringLiteral("15"));
    EXPECT_EQ(object->value(QStringLiteral("lastErrorCode")).toString(), QStringLiteral("HARDWARE_FAILURE"));

    vision::tcp::RecoverReply recovered;
    recovered.sequence = 20;
    recovered.success = true;
    recovered.connection = connection;
    ASSERT_TRUE(server_->sendRecoverResult(recovered));
    object = peer.takeObject();
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("outcome")).toString(), QStringLiteral("OK"));
    EXPECT_FALSE(object->contains(QStringLiteral("errorCode")));

    recovered.sequence = 21;
    recovered.success = false;
    recovered.errorCode = vision::tcp::TcpErrorCode::NotReady;
    recovered.message = QStringLiteral("not ready");
    ASSERT_TRUE(server_->sendRecoverResult(recovered));
    object = peer.takeObject();
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("outcome")).toString(), QStringLiteral("ERROR"));
    EXPECT_EQ(object->value(QStringLiteral("errorCode")).toString(), QStringLiteral("NOT_READY"));
}

TEST_F(Tcp_VisionTcpRepliesTests, RejectsInvalidLocateResultCombinationsWithoutWriting) {
    startServer();
    support::JsonLineClient peer;
    const auto connection = connectClient(peer);
    vision::tcp::LocateResultReply reply;
    reply.taskId = QStringLiteral("1");
    reply.connection = connection;
    reply.outcome = vision::tcp::LocateReplyOutcome::Error;
    EXPECT_FALSE(server_->sendLocateResult(reply));

    reply.outcome = vision::tcp::LocateReplyOutcome::Ok;
    reply.x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(server_->sendLocateResult(reply));
    reply.outcome = vision::tcp::LocateReplyOutcome::Ng;
    EXPECT_FALSE(server_->sendLocateResult(reply));
    EXPECT_FALSE(support::processEventsUntil(
        [&] { return !peer.receivedBytes().isEmpty(); }, std::chrono::milliseconds{50}));
}

TEST_F(Tcp_VisionTcpRepliesTests, EverySendEntryRejectsOldGenerationWithoutWritingToNewClient) {
    startServer();
    support::JsonLineClient oldPeer;
    const auto oldConnection = connectClient(oldPeer);
    oldPeer.socket().disconnectFromHost();
    ASSERT_TRUE(support::processEventsUntil([&] { return !server_->activeConnection().has_value(); }));
    support::JsonLineClient newPeer;
    const auto newConnection = connectClient(newPeer);
    ASSERT_GT(newConnection.generation, oldConnection.generation);

    vision::tcp::LocatePreparedReply prepared;
    prepared.taskId = QStringLiteral("1");
    prepared.connection = oldConnection;
    vision::tcp::PrepareRejectedReply rejected;
    rejected.taskId = QStringLiteral("1");
    rejected.connection = oldConnection;
    vision::tcp::LocateResultReply result;
    result.taskId = QStringLiteral("1");
    result.outcome = vision::tcp::LocateReplyOutcome::Ok;
    result.connection = oldConnection;
    vision::tcp::StatusReply status;
    status.connection = oldConnection;
    vision::tcp::RecoverReply recover;
    recover.success = true;
    recover.connection = oldConnection;

    EXPECT_FALSE(server_->sendLocatePrepared(prepared));
    EXPECT_FALSE(server_->sendPrepareRejected(rejected));
    EXPECT_FALSE(server_->sendLocateResult(result));
    EXPECT_FALSE(server_->sendStatus(status));
    EXPECT_FALSE(server_->sendRecoverResult(recover));
    EXPECT_FALSE(support::processEventsUntil(
        [&] { return !newPeer.receivedBytes().isEmpty(); }, std::chrono::milliseconds{50}));
}

TEST(Tcp_VisionTcpProtocolNameTests, CoversEveryPublicEnumValue) {
    using namespace vision::tcp;
    const std::array modeCases{
        std::pair{TcpLocateMode::Edge, QStringLiteral("EDGE")},
        std::pair{TcpLocateMode::MarkPoint, QStringLiteral("MARK_POINT")}};
    for (const auto& [value, name] : modeCases) EXPECT_EQ(protocolName(value), name);

    const std::array errorCases{
        std::pair{TcpErrorCode::ProtocolError, QStringLiteral("PROTOCOL_ERROR")},
        std::pair{TcpErrorCode::UnsupportedVersion, QStringLiteral("UNSUPPORTED_VERSION")},
        std::pair{TcpErrorCode::MessageTooLarge, QStringLiteral("MESSAGE_TOO_LARGE")},
        std::pair{TcpErrorCode::SingleClientOnly, QStringLiteral("SINGLE_CLIENT_ONLY")},
        std::pair{TcpErrorCode::NotReady, QStringLiteral("NOT_READY")},
        std::pair{TcpErrorCode::Busy, QStringLiteral("BUSY")},
        std::pair{TcpErrorCode::InvalidConfiguration, QStringLiteral("INVALID_CONFIGURATION")},
        std::pair{TcpErrorCode::HardwareUnavailable, QStringLiteral("HARDWARE_UNAVAILABLE")},
        std::pair{TcpErrorCode::HardwareFailure, QStringLiteral("HARDWARE_FAILURE")},
        std::pair{TcpErrorCode::HardwareTimeout, QStringLiteral("HARDWARE_TIMEOUT")},
        std::pair{TcpErrorCode::InvalidImage, QStringLiteral("INVALID_IMAGE")},
        std::pair{TcpErrorCode::AlgorithmFailure, QStringLiteral("ALGORITHM_FAILURE")},
        std::pair{TcpErrorCode::AlgorithmTimeout, QStringLiteral("ALGORITHM_TIMEOUT")},
        std::pair{TcpErrorCode::InvalidResult, QStringLiteral("INVALID_RESULT")},
        std::pair{TcpErrorCode::InternalFailure, QStringLiteral("INTERNAL_FAILURE")}};
    for (const auto& [value, name] : errorCases) EXPECT_EQ(protocolName(value), name);

    const std::array outcomeCases{
        std::pair{LocateReplyOutcome::Ok, QStringLiteral("OK")},
        std::pair{LocateReplyOutcome::Ng, QStringLiteral("NG")},
        std::pair{LocateReplyOutcome::Error, QStringLiteral("ERROR")}};
    for (const auto& [value, name] : outcomeCases) EXPECT_EQ(protocolName(value), name);

    const std::array applicationCases{
        std::pair{TcpApplicationState::Starting, QStringLiteral("STARTING")},
        std::pair{TcpApplicationState::Ready, QStringLiteral("READY")},
        std::pair{TcpApplicationState::ShuttingDown, QStringLiteral("SHUTTING_DOWN")},
        std::pair{TcpApplicationState::Stopped, QStringLiteral("STOPPED")}};
    for (const auto& [value, name] : applicationCases) EXPECT_EQ(protocolName(value), name);

    const std::array visionCases{
        std::pair{TcpVisionState::Starting, QStringLiteral("STARTING")},
        std::pair{TcpVisionState::Ready, QStringLiteral("READY")},
        std::pair{TcpVisionState::Faulted, QStringLiteral("FAULTED")},
        std::pair{TcpVisionState::Recovering, QStringLiteral("RECOVERING")},
        std::pair{TcpVisionState::Stopped, QStringLiteral("STOPPED")}};
    for (const auto& [value, name] : visionCases) EXPECT_EQ(protocolName(value), name);

    const std::array taskCases{
        std::pair{TcpTaskState::Idle, QStringLiteral("IDLE")},
        std::pair{TcpTaskState::Preparing, QStringLiteral("PREPARING")},
        std::pair{TcpTaskState::Running, QStringLiteral("RUNNING")},
        std::pair{TcpTaskState::AwaitingResultAck, QStringLiteral("AWAITING_RESULT_ACK")}};
    for (const auto& [value, name] : taskCases) EXPECT_EQ(protocolName(value), name);
}

} // namespace
} // namespace vision::test::tcp
