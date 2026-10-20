#include "VisionApp/Coordination/support/ControllerScenario.h"

namespace vision::test::app { namespace {

TEST(App_ApplicationControllerRecoveryTests,
     RecoveryAfterHardwareFailureReturnsServiceToReady)
{
    ensureApplication();
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const auto state = std::make_shared<RuntimeState>();
    state->failNextCapture.store(true);
    const quint16 port = config.tcp.port;
    vision::app::ApplicationController controller(
        std::move(config),
        successfulFactory(state),
        nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);
    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));

    ClientPeer client;
    client.connectTo(port);
    ASSERT_TRUE(processEventsUntil([&] {
        return client.socket.state() == QAbstractSocket::ConnectedState;
    }));
    client.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"4001\",\"mode\":\"EDGE\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState ==
            vision::app::TaskState::AwaitingResultAck;
    }));
    EXPECT_EQ(
        controller.snapshot().visionState,
        vision::app::VisionServiceState::Faulted);
    client.send(
        "{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"4001\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState == vision::app::TaskState::Idle;
    }));

    controller.requestLocalRecovery();
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().visionState ==
            vision::app::VisionServiceState::Ready;
    }));
    EXPECT_FALSE(controller.snapshot().lastError.has_value());

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerRecoveryTests,
     TcpRecoveryPreservesSequenceAndBusyRecoveryIsRejected)
{
    ensureApplication();
    auto config = controllerConfig();
    const auto state = std::make_shared<RuntimeState>();
    const quint16 port = config.tcp.port;
    vision::app::ApplicationController controller(
        std::move(config), successfulFactory(state), nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);
    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));
    ClientPeer client;
    client.connectTo(port);
    ASSERT_TRUE(processEventsUntil([&] {
        return client.socket.state() == QAbstractSocket::ConnectedState;
    }));

    client.send("{\"version\":1,\"type\":\"recover\",\"sequence\":71}\n");
    std::optional<QJsonObject> recoveryReply;
    ASSERT_TRUE(processEventsUntil([&] {
        for (const auto& object : client.takeAvailableObjects()) {
            if (object.value(QStringLiteral("type")).toString() ==
                QStringLiteral("recoverResult")) recoveryReply = object;
        }
        return recoveryReply.has_value();
    }));
    EXPECT_EQ(recoveryReply->value(QStringLiteral("sequence")).toInt(), 71);
    EXPECT_EQ(recoveryReply->value(QStringLiteral("outcome")).toString(),
              QStringLiteral("OK"));

    state->blockCapture = true;
    client.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"6201\",\"mode\":\"EDGE\"}\n");
    ASSERT_TRUE(processEventsUntil([&] { return state->captureEntered.load(); }));
    client.send("{\"version\":1,\"type\":\"recover\",\"sequence\":72}\n");
    std::optional<QJsonObject> busyReply;
    ASSERT_TRUE(processEventsUntil([&] {
        for (const auto& object : client.takeAvailableObjects()) {
            if (object.value(QStringLiteral("type")).toString() ==
                QStringLiteral("recoverResult")) busyReply = object;
        }
        return busyReply.has_value();
    }));
    EXPECT_EQ(busyReply->value(QStringLiteral("sequence")).toInt(), 72);
    EXPECT_EQ(busyReply->value(QStringLiteral("errorCode")).toString(),
              QStringLiteral("BUSY"));

    {
        std::lock_guard lock(state->mutex);
        state->releaseCapture = true;
    }
    state->releaseCondition.notify_all();
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState ==
            vision::app::TaskState::AwaitingResultAck;
    }));
    client.send("{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"6201\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState == vision::app::TaskState::Idle;
    }));

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerRecoveryTests,
     StableErrorsStaleConnectionAndClosingAreIsolated)
{
    ensureApplication();
    auto config = controllerConfig();
    const auto state = std::make_shared<RuntimeState>();
    const quint16 port = config.tcp.port;
    vision::app::ApplicationController controller(
        std::move(config), successfulFactory(state), nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy rejected(
        &controller, &vision::app::ApplicationController::actionRejected);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);
    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));

    ClientPeer oldClient;
    oldClient.connectTo(port);
    ASSERT_TRUE(processEventsUntil([&] {
        return oldClient.socket.state() == QAbstractSocket::ConnectedState;
    }));
    state->failNextCapture.store(true);
    oldClient.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"6601\",\"mode\":\"EDGE\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState ==
                vision::app::TaskState::AwaitingResultAck &&
            controller.snapshot().visionState ==
                vision::app::VisionServiceState::Faulted;
    }));
    oldClient.send(
        "{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"6601\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState == vision::app::TaskState::Idle;
    }));

    state->failNextRecovery.store(true);
    controller.requestLocalRecovery();
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().visionState ==
            vision::app::VisionServiceState::Faulted;
    }));
    ASSERT_TRUE(controller.snapshot().lastError.has_value());
    EXPECT_EQ(controller.snapshot().lastError->code,
              vision::core::ErrorCode::HardwareFailure);

    state->failNextRecovery.store(true);
    oldClient.send("{\"version\":1,\"type\":\"recover\",\"sequence\":81}\n");
    std::optional<QJsonObject> errorReply;
    ASSERT_TRUE(processEventsUntil([&] {
        for (const auto& object : oldClient.takeAvailableObjects()) {
            if (object.value(QStringLiteral("type")).toString() ==
                QStringLiteral("recoverResult")) {
                errorReply = object;
            }
        }
        return errorReply.has_value();
    }));
    EXPECT_EQ(errorReply->value(QStringLiteral("sequence")).toInt(), 81);
    EXPECT_EQ(errorReply->value(QStringLiteral("errorCode")).toString(),
              QStringLiteral("HARDWARE_FAILURE"));

    state->blockRecovery = true;
    state->releaseRecovery = false;
    state->recoveryEntered.store(false);
    oldClient.send("{\"version\":1,\"type\":\"recover\",\"sequence\":82}\n");
    ASSERT_TRUE(processEventsUntil([&] { return state->recoveryEntered.load(); }));
    oldClient.socket.disconnectFromHost();
    ASSERT_TRUE(processEventsUntil([&] {
        return oldClient.socket.state() == QAbstractSocket::UnconnectedState;
    }));
    ClientPeer newClient;
    newClient.connectTo(port);
    ASSERT_TRUE(processEventsUntil([&] {
        return newClient.socket.state() == QAbstractSocket::ConnectedState;
    }));
    {
        std::lock_guard lock(state->mutex);
        state->releaseRecovery = true;
    }
    state->releaseCondition.notify_all();
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().visionState ==
            vision::app::VisionServiceState::Ready;
    }));
    EXPECT_FALSE(processEventsUntil(
        [&] { return !newClient.takeAvailableObjects().empty(); }, 80ms));

    controller.shutdown();
    controller.requestLocalRecovery();
    EXPECT_EQ(rejected.count(), 1);
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

} }
