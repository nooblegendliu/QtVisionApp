#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <VisionTcp/VisionTcpServer.h>

#include "TestSupport/QtTcpTestSupport.h"

namespace vision::test::tcp {

class VisionTcpServerFixture : public ::testing::Test {
protected:
    void SetUp() override {
        support::ensureQCoreApplication();
        qRegisterMetaType<vision::tcp::ConnectionContext>();
        qRegisterMetaType<vision::tcp::PrepareLocateCommand>();
        qRegisterMetaType<vision::tcp::QueryStatusCommand>();
        qRegisterMetaType<vision::tcp::RecoverCommand>();
        qRegisterMetaType<vision::tcp::ResultAck>();
    }

    void TearDown() override {
        if (server_) {
            server_->stop();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        server_.reset();
    }

    vision::tcp::TcpServerConfig makeConfig(
        std::chrono::milliseconds heartbeatTimeout = std::chrono::seconds{5},
        qsizetype maximumFrameBytes = 1024) {
        vision::tcp::TcpServerConfig config;
        config.listenAddress = QHostAddress::LocalHost;
        config.port = support::reserveLocalTcpPort();
        config.heartbeatTimeout = heartbeatTimeout;
        config.maximumFrameBytes = maximumFrameBytes;
        return config;
    }

    void startServer(std::chrono::milliseconds heartbeatTimeout = std::chrono::seconds{5},
                     qsizetype maximumFrameBytes = 1024) {
        auto config = makeConfig(heartbeatTimeout, maximumFrameBytes);
        ASSERT_NE(config.port, 0);
        server_ = std::make_unique<vision::tcp::VisionTcpServer>(config);
        ASSERT_TRUE(server_->start()) << server_->lastError().toStdString();
    }

    vision::tcp::ConnectionContext connectClient(support::JsonLineClient& peer) {
        std::optional<vision::tcp::ConnectionContext> connected;
        const auto signalConnection = QObject::connect(
            server_.get(), &vision::tcp::VisionTcpServer::clientConnected,
            server_.get(), [&](vision::tcp::ConnectionContext context) {
                connected = context;
            });
        peer.connectToLocalHost(server_->serverPort());
        EXPECT_TRUE(support::processEventsUntil([&] { return connected.has_value(); }));
        QObject::disconnect(signalConnection);
        return connected.value_or(vision::tcp::ConnectionContext{});
    }

    std::unique_ptr<vision::tcp::VisionTcpServer> server_;
};

inline void expectProtocolError(const std::optional<QJsonObject>& object,
                                const QString& errorCode = QStringLiteral("PROTOCOL_ERROR")) {
    ASSERT_TRUE(object.has_value());
    EXPECT_EQ(object->value(QStringLiteral("type")).toString(), QStringLiteral("protocolError"));
    EXPECT_EQ(object->value(QStringLiteral("errorCode")).toString(), errorCode);
}

} // namespace vision::test::tcp
