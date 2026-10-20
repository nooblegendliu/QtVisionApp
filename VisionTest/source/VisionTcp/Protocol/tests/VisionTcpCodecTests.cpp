#include "VisionTcp/support/VisionTcpServerFixture.h"

#include <QAbstractSocket>

#include <gtest/gtest.h>

#include <vector>

namespace vision::test::tcp {
namespace {

class Tcp_VisionTcpCodecTests : public VisionTcpServerFixture {};

TEST_F(Tcp_VisionTcpCodecTests, FragmentedCoalescedCrlfAndBlankLinesEmitAllTypedCommands) {
    startServer();
    support::JsonLineClient peer;
    const auto connection = connectClient(peer);
    ASSERT_TRUE(connection.isValid());

    std::vector<vision::tcp::PrepareLocateCommand> prepares;
    std::vector<vision::tcp::QueryStatusCommand> queries;
    std::vector<vision::tcp::RecoverCommand> recovers;
    std::vector<vision::tcp::ResultAck> acknowledgements;
    QObject::connect(server_.get(), &vision::tcp::VisionTcpServer::prepareLocateRequested,
                     server_.get(), [&](const auto& value) { prepares.push_back(value); });
    QObject::connect(server_.get(), &vision::tcp::VisionTcpServer::queryStatusRequested,
                     server_.get(), [&](const auto& value) { queries.push_back(value); });
    QObject::connect(server_.get(), &vision::tcp::VisionTcpServer::recoverRequested,
                     server_.get(), [&](const auto& value) { recovers.push_back(value); });
    QObject::connect(server_.get(), &vision::tcp::VisionTcpServer::resultAcknowledged,
                     server_.get(), [&](const auto& value) { acknowledgements.push_back(value); });

    peer.sendBytes("{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"1\"");
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_TRUE(prepares.empty());
    peer.sendBytes(
        ",\"mode\":\"EDGE\"}\r\n\n"
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"2\",\"mode\":\"MARK_POINT\"}\n"
        "{\"version\":1,\"type\":\"queryStatus\",\"sequence\":0}\n"
        "{\"version\":1,\"type\":\"recover\",\"sequence\":4294967295}\n"
        "{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"999\"}\n"
        "{\"version\":1,\"type\":\"heartbeat\",\"sequence\":25}\r\n");

    ASSERT_TRUE(support::processEventsUntil([&] {
        return prepares.size() == 2 && queries.size() == 1 && recovers.size() == 1 &&
            acknowledgements.size() == 1;
    }));
    EXPECT_EQ(prepares[0].taskId, QStringLiteral("1"));
    EXPECT_EQ(prepares[0].mode, vision::tcp::TcpLocateMode::Edge);
    EXPECT_EQ(prepares[1].mode, vision::tcp::TcpLocateMode::MarkPoint);
    EXPECT_EQ(queries.front().sequence, 0U);
    EXPECT_EQ(recovers.front().sequence, std::numeric_limits<quint32>::max());
    EXPECT_EQ(acknowledgements.front().taskId, QStringLiteral("999"));
    EXPECT_EQ(prepares.front().connection, connection);
    EXPECT_EQ(recovers.front().connection, connection);

    const auto heartbeat = peer.takeObject();
    ASSERT_TRUE(heartbeat.has_value());
    EXPECT_EQ(heartbeat->value(QStringLiteral("type")).toString(), QStringLiteral("heartbeatAck"));
    EXPECT_EQ(heartbeat->value(QStringLiteral("sequence")).toInt(), 25);
}

TEST_F(Tcp_VisionTcpCodecTests, InvalidUtf8NonObjectVersionAndTypeReturnProtocolErrors) {
    startServer();
    support::JsonLineClient peer;
    ASSERT_TRUE(connectClient(peer).isValid());

    QByteArray invalidUtf8 = "{\"version\":1,\"type\":\"heartbeat\",\"sequence\":";
    invalidUtf8.append(char(0xff));
    invalidUtf8.append("}\n");
    peer.sendBytes(invalidUtf8);
    expectProtocolError(peer.takeObject());

    const std::vector<QByteArray> invalidFrames{
        "[]\n",
        "{bad json}\n",
        "{\"type\":\"heartbeat\",\"sequence\":1}\n",
        "{\"version\":\"1\",\"type\":\"heartbeat\",\"sequence\":1}\n",
        "{\"version\":1,\"sequence\":1}\n",
        "{\"version\":1,\"type\":7,\"sequence\":1}\n",
        "{\"version\":1,\"type\":\"unknown\"}\n"};
    for (const auto& frame : invalidFrames) {
        peer.sendBytes(frame);
        expectProtocolError(peer.takeObject());
    }
}

TEST_F(Tcp_VisionTcpCodecTests, InvalidTaskIdModeAndSequenceFieldMatrixIsRejected) {
    startServer();
    support::JsonLineClient peer;
    ASSERT_TRUE(connectClient(peer).isValid());

    const std::vector<QByteArray> taskAndModeCases{
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":0,\"mode\":\"EDGE\"}\n",
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"0\",\"mode\":\"EDGE\"}\n",
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"01\",\"mode\":\"EDGE\"}\n",
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"-1\",\"mode\":\"EDGE\"}\n",
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"1.0\",\"mode\":\"EDGE\"}\n",
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"1\",\"mode\":\"edge\"}\n",
        "{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"0\"}\n"};
    for (const auto& frame : taskAndModeCases) {
        peer.sendBytes(frame);
        expectProtocolError(peer.takeObject());
    }

    const std::vector<QString> sequenceValues{
        QStringLiteral("null"), QStringLiteral("\"1\""), QStringLiteral("-1"),
        QStringLiteral("1.5"), QStringLiteral("4294967296")};
    for (const QString& type : {QStringLiteral("heartbeat"), QStringLiteral("queryStatus"),
                                QStringLiteral("recover")}) {
        for (const QString& value : sequenceValues) {
            peer.sendBytes(QStringLiteral("{\"version\":1,\"type\":\"%1\",\"sequence\":%2}\n")
                               .arg(type, value).toUtf8());
            expectProtocolError(peer.takeObject());
        }
    }
}

TEST_F(Tcp_VisionTcpCodecTests, UnsupportedVersionReturnsStableErrorAndDisconnects) {
    startServer();
    support::JsonLineClient peer;
    ASSERT_TRUE(connectClient(peer).isValid());

    peer.sendBytes("{\"version\":2,\"type\":\"heartbeat\",\"sequence\":1}\n");

    expectProtocolError(peer.takeObject(), QStringLiteral("UNSUPPORTED_VERSION"));
    EXPECT_TRUE(support::processEventsUntil([&] {
        return peer.socket().state() == QAbstractSocket::UnconnectedState;
    }));
}

TEST_F(Tcp_VisionTcpCodecTests, FrameAtLimitIsAcceptedAndOneByteOverLimitDisconnects) {
    constexpr qsizetype maximum = 256;
    startServer(std::chrono::seconds{5}, maximum);
    support::JsonLineClient peer;
    ASSERT_TRUE(connectClient(peer).isValid());

    const QByteArray prefix = "{\"version\":1,\"type\":\"heartbeat\",\"sequence\":1,\"padding\":\"";
    const QByteArray suffix = "\"}";
    ASSERT_LE(prefix.size() + suffix.size(), maximum);
    QByteArray exact = prefix + QByteArray(maximum - prefix.size() - suffix.size(), 'a') + suffix;
    ASSERT_EQ(exact.size(), maximum);
    peer.sendBytes(exact + "\n");
    const auto accepted = peer.takeObject();
    ASSERT_TRUE(accepted.has_value());
    EXPECT_EQ(accepted->value(QStringLiteral("type")).toString(), QStringLiteral("heartbeatAck"));

    peer.sendBytes(QByteArray(maximum + 1, 'x'));
    expectProtocolError(peer.takeObject(), QStringLiteral("MESSAGE_TOO_LARGE"));
    EXPECT_TRUE(support::processEventsUntil([&] {
        return peer.socket().state() == QAbstractSocket::UnconnectedState;
    }));
}

} // namespace
} // namespace vision::test::tcp
