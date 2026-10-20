#include "VisionApp/Coordination/support/ControllerScenario.h"

namespace vision::test::app { namespace {

TEST(App_ApplicationControllerTaskDeliveryTests,
     ReadyGatesTcpAndBusyRejectsWithoutQueueingBothLocateModes)
{
    ensureApplication();
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const auto state = std::make_shared<RuntimeState>();
    const quint16 port = config.tcp.port;
    vision::app::ApplicationController controller(
        std::move(config),
        successfulFactory(state),
        nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy presentationSpy(
        &controller,
        &vision::app::ApplicationController::taskPresentationChanged);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    EXPECT_FALSE(controller.isTcpListening());
    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));
    EXPECT_EQ(controller.tcpPort(), port);
    EXPECT_EQ(
        controller.snapshot().applicationState,
        vision::app::ApplicationState::Ready);

    ClientPeer client;
    client.connectTo(port);
    ASSERT_TRUE(processEventsUntil([&] {
        return client.socket.state() == QAbstractSocket::ConnectedState;
    }));
    client.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"1001\",\"mode\":\"EDGE\"}\n"
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"1002\",\"mode\":\"MARK_POINT\"}\n"
        "{\"version\":1,\"type\":\"queryStatus\",\"sequence\":9}\n");

    std::vector<QJsonObject> replies;
    ASSERT_TRUE(processEventsUntil([&] {
        auto available = client.takeAvailableObjects();
        replies.insert(replies.end(), available.begin(), available.end());
        bool hasResult = false;
        bool hasBusy = false;
        bool hasStatus = false;
        for (const auto& object : replies) {
            const QString type = object.value(QStringLiteral("type")).toString();
            hasResult |= type == QStringLiteral("locateResult");
            hasBusy |= type == QStringLiteral("prepareRejected");
            hasStatus |= type == QStringLiteral("statusReply");
        }
        return hasResult && hasBusy && hasStatus;
    }));

    bool busyWasStable = false;
    for (const auto& object : replies) {
        if (object.value(QStringLiteral("type")).toString() ==
            QStringLiteral("prepareRejected")) {
            busyWasStable =
                object.value(QStringLiteral("taskId")).toString() ==
                    QStringLiteral("1002") &&
                object.value(QStringLiteral("errorCode")).toString() ==
                    QStringLiteral("BUSY");
        }
    }
    EXPECT_TRUE(busyWasStable);
    auto modes = capturedModes(state);
    ASSERT_EQ(modes.size(), 1U);
    EXPECT_EQ(modes[0], vision::core::LocateMode::Edge);

    client.send(
        "{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"1001\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState == vision::app::TaskState::Idle;
    }));
    client.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"1003\",\"mode\":\"MARK_POINT\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return capturedModes(state).size() == 2U &&
            controller.snapshot().taskState ==
                vision::app::TaskState::AwaitingResultAck;
    }));
    modes = capturedModes(state);
    EXPECT_EQ(modes[1], vision::core::LocateMode::MarkPoint);
    ASSERT_GE(presentationSpy.count(), 2);
    const auto edgePresentation = qvariant_cast<
        vision::app::TaskPresentationSnapshot>(presentationSpy.at(0).at(0));
    ASSERT_EQ(edgePresentation.images.size(), 1);
    EXPECT_EQ(edgePresentation.images[0].role,
              vision::core::CameraRole::Center);
    EXPECT_EQ(edgePresentation.images[0].image.format(),
              QImage::Format_Grayscale8);
    EXPECT_EQ(edgePresentation.images[0].image.constScanLine(0)[0], 10U);

    const auto markPresentation = qvariant_cast<
        vision::app::TaskPresentationSnapshot>(presentationSpy.last().at(0));
    ASSERT_EQ(markPresentation.images.size(), 4);
    std::vector<vision::core::CameraRole> roles;
    for (const auto& snapshot : markPresentation.images) {
        roles.push_back(snapshot.role);
        EXPECT_FALSE(snapshot.image.isNull());
        EXPECT_EQ(snapshot.image.constScanLine(0)[0], 10U);
    }
    EXPECT_NE(std::find(roles.begin(), roles.end(),
                        vision::core::CameraRole::TopLeft), roles.end());
    EXPECT_NE(std::find(roles.begin(), roles.end(),
                        vision::core::CameraRole::TopRight), roles.end());
    EXPECT_NE(std::find(roles.begin(), roles.end(),
                        vision::core::CameraRole::BottomLeft), roles.end());
    EXPECT_NE(std::find(roles.begin(), roles.end(),
                        vision::core::CameraRole::BottomRight), roles.end());

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
    EXPECT_TRUE(state->cameraDestroyed.load());
    EXPECT_NE(state->creationThread, QCoreApplication::instance()->thread());
    EXPECT_EQ(state->cameraDestructionThread, state->creationThread);
}

TEST(App_ApplicationControllerTaskDeliveryTests,
     DisconnectDoesNotCancelLocateAndOldResultIsNotSentToNewGeneration)
{
    ensureApplication();
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const auto state = std::make_shared<RuntimeState>();
    state->blockCapture = true;
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

    ClientPeer oldClient;
    oldClient.connectTo(port);
    ASSERT_TRUE(processEventsUntil([&] {
        return oldClient.socket.state() == QAbstractSocket::ConnectedState;
    }));
    oldClient.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"2001\",\"mode\":\"EDGE\"}\n");
    ASSERT_TRUE(processEventsUntil([&] { return state->captureEntered.load(); }));

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
        state->releaseCapture = true;
    }
    state->releaseCondition.notify_all();
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState == vision::app::TaskState::Idle;
    }));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    const auto unsolicited = newClient.takeAvailableObjects();
    EXPECT_TRUE(unsolicited.empty());
    ASSERT_EQ(capturedModes(state).size(), 1U);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
    EXPECT_TRUE(state->cameraDestroyed.load());
}

TEST(App_ApplicationControllerTaskDeliveryTests,
     ResultDeliveryRetriesTheSameResultWithoutRunningLocateAgain)
{
    ensureApplication();
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    config.resultDelivery.acknowledgementInterval = 25ms;
    config.resultDelivery.maximumRetries = 1;
    const auto state = std::make_shared<RuntimeState>();
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
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"3001\",\"mode\":\"EDGE\"}\n");

    int resultCount = 0;
    ASSERT_TRUE(processEventsUntil([&] {
        for (const auto& object : client.takeAvailableObjects()) {
            if (object.value(QStringLiteral("type")).toString() ==
                QStringLiteral("locateResult")) {
                ++resultCount;
            }
        }
        return resultCount == 2;
    }));
    ASSERT_TRUE(processEventsUntil([&] {
        return client.socket.state() == QAbstractSocket::UnconnectedState;
    }));
    EXPECT_EQ(capturedModes(state).size(), 1U);
    EXPECT_EQ(controller.snapshot().taskState, vision::app::TaskState::Idle);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerTaskDeliveryTests,
     WrongResultAckIsIgnoredAndCorrectAckClearsExactlyOneTask)
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
    client.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"6101\",\"mode\":\"EDGE\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState ==
            vision::app::TaskState::AwaitingResultAck;
    }));

    client.send("{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"6102\"}\n");
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    EXPECT_EQ(controller.snapshot().taskState,
              vision::app::TaskState::AwaitingResultAck);
    client.send("{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"6101\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState == vision::app::TaskState::Idle;
    }));
    EXPECT_EQ(capturedModes(state).size(), 1U);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerTaskDeliveryTests,
     DisconnectAndShutdownStopPendingRetriesWithoutRepeatingLocate)
{
    ensureApplication();

    {
        auto config = controllerConfig();
        config.resultDelivery.acknowledgementInterval = 25ms;
        config.resultDelivery.maximumRetries = 5;
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
        client.send(
            "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"6501\",\"mode\":\"EDGE\"}\n");
        bool receivedResult = false;
        ASSERT_TRUE(processEventsUntil([&] {
            for (const auto& object : client.takeAvailableObjects()) {
                receivedResult |= object.value(QStringLiteral("type")).toString() ==
                    QStringLiteral("locateResult");
            }
            return receivedResult;
        }));
        client.socket.disconnectFromHost();
        ASSERT_TRUE(processEventsUntil([&] {
            return controller.snapshot().taskState == vision::app::TaskState::Idle;
        }));
        EXPECT_FALSE(processEventsUntil(
            [&] { return capturedModes(state).size() > 1U; }, 80ms));
        EXPECT_EQ(capturedModes(state).size(), 1U);
        controller.shutdown();
        ASSERT_TRUE(shutdownFinished.wait(5000));
    }

    {
        auto config = controllerConfig();
        config.resultDelivery.acknowledgementInterval = 25ms;
        config.resultDelivery.maximumRetries = 5;
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
        client.send(
            "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"6502\",\"mode\":\"EDGE\"}\n");
        ASSERT_TRUE(processEventsUntil([&] {
            return controller.snapshot().taskState ==
                vision::app::TaskState::AwaitingResultAck;
        }));
        controller.shutdown();
        ASSERT_TRUE(shutdownFinished.wait(5000));
        EXPECT_EQ(capturedModes(state).size(), 1U);
        EXPECT_EQ(controller.snapshot().taskState, vision::app::TaskState::Idle);
    }
}

} }
