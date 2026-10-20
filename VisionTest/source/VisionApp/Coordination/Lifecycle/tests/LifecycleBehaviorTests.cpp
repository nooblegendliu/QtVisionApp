#include "VisionApp/Coordination/support/ControllerScenario.h"

namespace vision::test::app { namespace {

TEST(App_ApplicationControllerLifecycleTests,
     InitializationFailureNeverStartsTcpAndShutsDownAsynchronously)
{
    ensureApplication();
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const vision::core::Error expected{
        vision::core::ErrorCode::HardwareUnavailable,
        "Hardware is unavailable.",
        "fake startup failure"};
    vision::app::CoreFactory factory =
        [expected](const vision::app::VisionRuntimeConfig&,
                   vision::hardware::ICaptureObserver*,
                   vision::core::IDiagnosticSink*,
                   vision::core::ILocateFrameObserver*)
                   -> vision::app::CoreFactoryOutcome {
        return expected;
    };

    vision::app::ApplicationController controller(
        std::move(config),
        std::move(factory),
        nullptr);
    ControllerShutdownGuard shutdownGuard(controller);
    QSignalSpy startupFailure(
        &controller, &vision::app::ApplicationController::startupFailed);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(startupFailure.wait(5000));
    const auto failed = controller.snapshot();
    EXPECT_EQ(failed.applicationState, vision::app::ApplicationState::StartFailed);
    EXPECT_EQ(failed.visionState, vision::app::VisionServiceState::Faulted);
    EXPECT_FALSE(failed.tcpListening);
    ASSERT_TRUE(failed.lastError.has_value());
    EXPECT_EQ(failed.lastError->code, expected.code);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
    EXPECT_EQ(
        controller.snapshot().applicationState,
        vision::app::ApplicationState::Stopped);
}

TEST(App_ApplicationControllerLifecycleTests,
     NullAndNonReadyFactoryResultsDoNotStartTcpOrRetainRuntime)
{
    ensureApplication();
    const auto verifyRejectedFactory = [](vision::app::CoreFactory factory) {
        auto config = controllerConfig();
        ASSERT_NE(config.tcp.port, 0);
        vision::app::ApplicationController controller(
            std::move(config),
            std::move(factory),
            nullptr);
    ControllerShutdownGuard shutdownGuard(controller);
        QSignalSpy startupFailure(
            &controller, &vision::app::ApplicationController::startupFailed);
        QSignalSpy shutdownFinished(
            &controller, &vision::app::ApplicationController::shutdownFinished);
        controller.start();
        ASSERT_TRUE(startupFailure.wait(5000));
        EXPECT_FALSE(controller.isTcpListening());
        EXPECT_FALSE(controller.snapshot().runtimeInitialized);
        controller.shutdown();
        ASSERT_TRUE(shutdownFinished.wait(5000));
    };

    verifyRejectedFactory([](
            const vision::app::VisionRuntimeConfig&,
            vision::hardware::ICaptureObserver*,
            vision::core::IDiagnosticSink*,
            vision::core::ILocateFrameObserver*)
            -> vision::app::CoreFactoryOutcome {
        return std::unique_ptr<vision::core::VisionCore>{};
    });
    verifyRejectedFactory([](
            const vision::app::VisionRuntimeConfig&,
            vision::hardware::ICaptureObserver*,
            vision::core::IDiagnosticSink* diagnostics,
            vision::core::ILocateFrameObserver*)
            -> vision::app::CoreFactoryOutcome {
        return std::make_unique<vision::core::VisionCore>(
            std::unique_ptr<vision::core::ICamera>{},
            std::make_unique<AppFakeLight>(),
            std::make_unique<AppFakeAlgorithm>(),
            diagnostics);
    });
}

TEST(App_ApplicationControllerLifecycleTests,
     ShutdownDuringLocateWaitsForNaturalCompletionBeforeDestroyingRuntime)
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

    ClientPeer client;
    client.connectTo(port);
    ASSERT_TRUE(processEventsUntil([&] {
        return client.socket.state() == QAbstractSocket::ConnectedState;
    }));
    client.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"5001\",\"mode\":\"EDGE\"}\n");
    ASSERT_TRUE(processEventsUntil([&] { return state->captureEntered.load(); }));

    controller.shutdown();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_EQ(shutdownFinished.count(), 0);
    EXPECT_FALSE(state->cameraDestroyed.load());
    EXPECT_EQ(
        controller.snapshot().applicationState,
        vision::app::ApplicationState::ShuttingDown);

    {
        std::lock_guard lock(state->mutex);
        state->releaseCapture = true;
    }
    state->releaseCondition.notify_all();
    ASSERT_TRUE(shutdownFinished.wait(5000));
    EXPECT_TRUE(state->cameraDestroyed.load());
    EXPECT_EQ(state->cameraDestructionThread, state->creationThread);
}

TEST(App_ApplicationControllerLifecycleTests,
     RetryInitializationSucceedsAfterDeterministicFirstFailure)
{
    ensureApplication();
    auto config = controllerConfig();
    const auto state = std::make_shared<RuntimeState>();
    const auto success = successfulFactory(state);
    const auto calls = std::make_shared<std::atomic_int>(0);
    vision::app::CoreFactory factory =
        [success, calls](const vision::app::VisionRuntimeConfig& runtime,
                         vision::hardware::ICaptureObserver* captureObserver,
                         vision::core::IDiagnosticSink* diagnostics,
                         vision::core::ILocateFrameObserver* frameObserver)
            -> vision::app::CoreFactoryOutcome {
        if (++(*calls) == 1) {
            return vision::core::Error{
                vision::core::ErrorCode::HardwareUnavailable,
                "First initialization failed.",
                "deterministic retry test"};
        }
        return success(runtime, captureObserver, diagnostics, frameObserver);
    };
    vision::app::ApplicationController controller(
        std::move(config), std::move(factory), nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy failures(
        &controller, &vision::app::ApplicationController::startupFailed);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(failures.wait(5000));
    EXPECT_FALSE(controller.snapshot().runtimeInitialized);
    controller.retryInitialization();
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().runtimeInitialized &&
            controller.isTcpListening();
    }));
    EXPECT_EQ(calls->load(), 2);
    EXPECT_EQ(controller.snapshot().applicationState,
              vision::app::ApplicationState::Ready);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerLifecycleTests,
     RetryTcpSucceedsAfterOccupiedPortIsReleased)
{
    ensureApplication();
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, config.tcp.port));
    const auto state = std::make_shared<RuntimeState>();
    vision::app::ApplicationController controller(
        config, successfulFactory(state), nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy startupFailure(
        &controller, &vision::app::ApplicationController::startupFailed);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(startupFailure.wait(5000));
    EXPECT_TRUE(controller.snapshot().runtimeInitialized);
    EXPECT_FALSE(controller.isTcpListening());
    blocker.close();
    controller.retryTcp();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));
    EXPECT_EQ(controller.snapshot().applicationState,
              vision::app::ApplicationState::Ready);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerLifecycleTests,
     ShutdownCompletesFromStartingReadyAndAwaitingResultAck)
{
    ensureApplication();

    {
        auto config = controllerConfig();
        const auto state = std::make_shared<RuntimeState>();
        vision::app::ApplicationController controller(
            std::move(config), successfulFactory(state), nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
        QSignalSpy shutdownFinished(
            &controller, &vision::app::ApplicationController::shutdownFinished);
        controller.start();
        controller.shutdown();
        EXPECT_NE(controller.snapshot().applicationState,
                  vision::app::ApplicationState::Ready);
        ASSERT_TRUE(shutdownFinished.wait(5000));
        EXPECT_EQ(controller.snapshot().applicationState,
                  vision::app::ApplicationState::Stopped);
    }

    {
        auto config = controllerConfig();
        const auto state = std::make_shared<RuntimeState>();
        vision::app::ApplicationController controller(
            std::move(config), successfulFactory(state), nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
        QSignalSpy shutdownFinished(
            &controller, &vision::app::ApplicationController::shutdownFinished);
        controller.start();
        ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));
        controller.shutdown();
        EXPECT_EQ(controller.snapshot().applicationState,
                  vision::app::ApplicationState::ShuttingDown);
        ASSERT_TRUE(shutdownFinished.wait(5000));
        EXPECT_EQ(controller.snapshot().applicationState,
                  vision::app::ApplicationState::Stopped);
    }

    {
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
            "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"6401\",\"mode\":\"EDGE\"}\n");
        ASSERT_TRUE(processEventsUntil([&] {
            return controller.snapshot().taskState ==
                vision::app::TaskState::AwaitingResultAck;
        }));
        controller.shutdown();
        EXPECT_EQ(controller.snapshot().taskState, vision::app::TaskState::Idle);
        ASSERT_TRUE(shutdownFinished.wait(5000));
        EXPECT_EQ(capturedModes(state).size(), 1U);
    }
}

} }
