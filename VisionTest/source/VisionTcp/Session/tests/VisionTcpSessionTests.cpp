#include "VisionTcp/support/VisionTcpServerFixture.h"

#include <QAbstractSocket>
#include <QTcpServer>

#include <gtest/gtest.h>

namespace vision::test::tcp {
namespace {

using namespace std::chrono_literals;

class Tcp_VisionTcpSessionTests : public VisionTcpServerFixture {};

TEST_F(Tcp_VisionTcpSessionTests, StartStopAreIdempotentAndInvalidConfigurationIsRejected) {
    auto invalid = makeConfig();
    invalid.port = 0;
    server_ = std::make_unique<vision::tcp::VisionTcpServer>(invalid);
    EXPECT_FALSE(server_->start());
    EXPECT_FALSE(server_->isListening());
    EXPECT_FALSE(server_->lastError().isEmpty());

    auto valid = makeConfig();
    ASSERT_NE(valid.port, 0);
    server_ = std::make_unique<vision::tcp::VisionTcpServer>(valid);
    EXPECT_TRUE(server_->start());
    EXPECT_TRUE(server_->start());
    EXPECT_TRUE(server_->isListening());
    server_->stop();
    server_->stop();
    EXPECT_FALSE(server_->isListening());
}

TEST_F(Tcp_VisionTcpSessionTests, StartReportsListenFailureWhenPortIsOccupied) {
    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, 0));
    auto config = makeConfig();
    config.port = blocker.serverPort();
    server_ = std::make_unique<vision::tcp::VisionTcpServer>(config);

    EXPECT_FALSE(server_->start());
    EXPECT_FALSE(server_->isListening());
    EXPECT_FALSE(server_->lastError().isEmpty());
}

TEST_F(Tcp_VisionTcpSessionTests, ReconfigureRequiresStoppedServerAndAppliesNewPort) {
    startServer();
    auto replacement = makeConfig();
    ASSERT_NE(replacement.port, 0);
    ASSERT_NE(replacement.port, server_->serverPort());
    EXPECT_FALSE(server_->reconfigure(replacement));

    support::JsonLineClient peer;
    ASSERT_TRUE(connectClient(peer).isValid());
    EXPECT_FALSE(server_->reconfigure(replacement));

    server_->stop();
    EXPECT_TRUE(server_->reconfigure(replacement));
    EXPECT_TRUE(server_->start());
    EXPECT_EQ(server_->serverPort(), replacement.port);
}

TEST_F(Tcp_VisionTcpSessionTests, RejectsSecondClientAndKeepsFirstGenerationActive) {
    startServer();
    support::JsonLineClient first;
    const auto firstContext = connectClient(first);
    ASSERT_TRUE(firstContext.isValid());
    support::JsonLineClient second;
    second.connectToLocalHost(server_->serverPort());

    expectProtocolError(second.takeObject(), QStringLiteral("SINGLE_CLIENT_ONLY"));
    EXPECT_EQ(server_->activeConnection(), firstContext);
    EXPECT_EQ(first.socket().state(), QAbstractSocket::ConnectedState);
    EXPECT_TRUE(support::processEventsUntil([&] {
        return second.socket().state() == QAbstractSocket::UnconnectedState;
    }));
}

TEST_F(Tcp_VisionTcpSessionTests, ConnectionGenerationIncrementsAfterDisconnectAndExplicitDisconnectIsScoped) {
    startServer();
    support::JsonLineClient first;
    const auto firstContext = connectClient(first);
    ASSERT_TRUE(firstContext.isValid());

    server_->disconnectClient(vision::tcp::ConnectionContext{firstContext.generation + 10});
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_EQ(first.socket().state(), QAbstractSocket::ConnectedState);
    server_->disconnectClient(firstContext);
    ASSERT_TRUE(support::processEventsUntil([&] {
        return first.socket().state() == QAbstractSocket::UnconnectedState &&
            !server_->activeConnection().has_value();
    }));

    support::JsonLineClient second;
    const auto secondContext = connectClient(second);
    EXPECT_GT(secondContext.generation, firstContext.generation);
    EXPECT_EQ(server_->activeConnection(), secondContext);
}

TEST_F(Tcp_VisionTcpSessionTests, HeartbeatRestartsTimeoutAndSilenceEventuallyDisconnects) {
    startServer(150ms);
    support::JsonLineClient peer;
    ASSERT_TRUE(connectClient(peer).isValid());

    EXPECT_FALSE(support::processEventsUntil([] { return false; }, 90ms));
    peer.sendBytes("{\"version\":1,\"type\":\"heartbeat\",\"sequence\":4}\n");
    ASSERT_TRUE(peer.takeObject().has_value());
    EXPECT_FALSE(support::processEventsUntil(
        [&] { return peer.socket().state() == QAbstractSocket::UnconnectedState; }, 90ms));
    EXPECT_EQ(peer.socket().state(), QAbstractSocket::ConnectedState);

    EXPECT_TRUE(support::processEventsUntil(
        [&] { return peer.socket().state() == QAbstractSocket::UnconnectedState; }, 300ms));
    EXPECT_FALSE(server_->activeConnection().has_value());
}

} // namespace
} // namespace vision::test::tcp
