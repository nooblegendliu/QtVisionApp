#include "VisionApp/Coordination/support/ControllerScenario.h"

namespace vision::test::app { namespace {

TEST(App_ApplicationControllerConfigurationTests,
     IdleResultPolicyApplyPersistsAndIncrementsRevision)
{
    ensureApplication();
    support::ScopedTestDirectory directory(__func__);
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const auto path = temporaryConfigPath(directory);
    const auto state = std::make_shared<RuntimeState>();
    vision::app::ApplicationController controller(
        config, successfulFactory(state), nullptr, nullptr, path);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy applied(
        &controller, &vision::app::ApplicationController::configurationApplied);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));
    auto candidate = controller.configuration();
    candidate.resultDelivery.maximumRetries = 6;
    controller.requestApplyConfiguration(candidate);

    ASSERT_TRUE(processEventsUntil([&] { return applied.count() == 1; }));
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 2U);
    EXPECT_EQ(controller.snapshot().configurationApplyState,
              vision::app::ConfigurationApplyState::Idle);
    EXPECT_EQ(controller.configuration().resultDelivery.maximumRetries, 6);
    EXPECT_FALSE(state->cameraDestroyed.load());

    const auto persisted = vision::app::loadApplicationConfig(path);
    const auto* saved =
        std::get_if<vision::app::ApplicationConfig>(&persisted);
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->resultDelivery.maximumRetries, 6);
    EXPECT_EQ(saved->schemaVersion, 1);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     StartupFailureCanApplyCorrectedConfigurationAndInitializeRuntime)
{
    ensureApplication();
    support::ScopedTestDirectory directory(__func__);
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const auto path = temporaryConfigPath(directory);
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
                "Initial hardware configuration failed.",
                "deterministic startup failure"};
        }
        return success(runtime, captureObserver, diagnostics, frameObserver);
    };
    vision::app::ApplicationController controller(
        config, std::move(factory), nullptr, nullptr, path);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy startupFailure(
        &controller, &vision::app::ApplicationController::startupFailed);
    QSignalSpy applied(
        &controller, &vision::app::ApplicationController::configurationApplied);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(startupFailure.wait(5000));
    EXPECT_FALSE(controller.snapshot().runtimeInitialized);
    EXPECT_EQ(controller.snapshot().visionState,
              vision::app::VisionServiceState::Faulted);

    auto corrected = controller.configuration();
    corrected.vision.cameras[0].serialNumber = "corrected-center";
    controller.requestApplyConfiguration(corrected);

    ASSERT_TRUE(processEventsUntil([&] { return applied.count() == 1; }));
    EXPECT_EQ(calls->load(), 2);
    EXPECT_TRUE(controller.snapshot().runtimeInitialized);
    EXPECT_TRUE(controller.isTcpListening());
    EXPECT_EQ(controller.snapshot().applicationState,
              vision::app::ApplicationState::Ready);
    EXPECT_EQ(controller.snapshot().visionState,
              vision::app::VisionServiceState::Ready);
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 2U);
    EXPECT_EQ(controller.configuration().vision.cameras[0].serialNumber,
              "corrected-center");

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     InitialApplyTcpFailureDiscardsCandidateAndRetryUsesPreviousConfiguration)
{
    ensureApplication();
    support::ScopedTestDirectory directory(__func__);
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const auto path = temporaryConfigPath(directory);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        vision::app::saveApplicationConfig(path, config)));

    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, 0));
    const auto state = std::make_shared<RuntimeState>();
    const auto success = successfulFactory(state);
    const auto calls = std::make_shared<std::atomic_int>(0);
    const auto serialMutex = std::make_shared<std::mutex>();
    const auto serials = std::make_shared<std::vector<std::string>>();
    vision::app::CoreFactory factory =
        [success, calls, serialMutex, serials](
            const vision::app::VisionRuntimeConfig& runtime,
            vision::hardware::ICaptureObserver* captureObserver,
            vision::core::IDiagnosticSink* diagnostics,
            vision::core::ILocateFrameObserver* frameObserver)
            -> vision::app::CoreFactoryOutcome {
        {
            std::lock_guard lock(*serialMutex);
            serials->push_back(runtime.cameras[0].serialNumber);
        }
        const int call = ++(*calls);
        if (call == 1 || call == 3) {
            return vision::core::Error{
                vision::core::ErrorCode::HardwareUnavailable,
                "Configured hardware is unavailable.",
                "deterministic initial and retry failure"};
        }
        return success(runtime, captureObserver, diagnostics, frameObserver);
    };
    vision::app::ApplicationController controller(
        config, std::move(factory), nullptr, nullptr, path);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy startupFailure(
        &controller, &vision::app::ApplicationController::startupFailed);
    QSignalSpy rejected(
        &controller, &vision::app::ApplicationController::actionRejected);
    QSignalSpy applied(
        &controller, &vision::app::ApplicationController::configurationApplied);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(startupFailure.wait(5000));
    auto candidate = controller.configuration();
    candidate.vision.cameras[0].serialNumber = "candidate-center";
    candidate.tcp.port = blocker.serverPort();
    controller.requestApplyConfiguration(candidate);

    ASSERT_TRUE(processEventsUntil([&] { return rejected.count() == 1; }));
    EXPECT_EQ(applied.count(), 0);
    EXPECT_EQ(calls->load(), 2);
    EXPECT_TRUE(state->cameraDestroyed.load());
    EXPECT_EQ(state->cameraDestructionThread, state->creationThread);
    EXPECT_FALSE(controller.isTcpListening());
    EXPECT_FALSE(controller.snapshot().runtimeInitialized);
    EXPECT_EQ(controller.snapshot().applicationState,
              vision::app::ApplicationState::StartFailed);
    EXPECT_EQ(controller.snapshot().visionState,
              vision::app::VisionServiceState::Faulted);
    EXPECT_EQ(controller.snapshot().configurationApplyState,
              vision::app::ConfigurationApplyState::Failed);
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 1U);
    EXPECT_EQ(controller.configuration().vision.cameras[0].serialNumber,
              config.vision.cameras[0].serialNumber);

    const auto persisted = vision::app::loadApplicationConfig(path);
    const auto* saved =
        std::get_if<vision::app::ApplicationConfig>(&persisted);
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->vision.cameras[0].serialNumber,
              config.vision.cameras[0].serialNumber);

    blocker.close();
    controller.retryInitialization();
    ASSERT_TRUE(processEventsUntil([&] {
        return startupFailure.count() == 2;
    }));
    EXPECT_EQ(calls->load(), 3);
    {
        std::lock_guard lock(*serialMutex);
        ASSERT_EQ(serials->size(), 3U);
        EXPECT_EQ((*serials)[0], config.vision.cameras[0].serialNumber);
        EXPECT_EQ((*serials)[1], "candidate-center");
        EXPECT_EQ((*serials)[2], config.vision.cameras[0].serialNumber);
    }
    EXPECT_FALSE(controller.snapshot().runtimeInitialized);
    EXPECT_FALSE(controller.isTcpListening());

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     InitialApplyPersistenceFailureDiscardsCandidateAndTcpConfiguration)
{
    ensureApplication();
    support::ScopedTestDirectory directory(__func__);
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
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
                "Initial hardware configuration failed.",
                "deterministic startup failure"};
        }
        return success(runtime, captureObserver, diagnostics, frameObserver);
    };
    vision::app::ApplicationController controller(
        config, std::move(factory), nullptr, nullptr, directory.path());
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy startupFailure(
        &controller, &vision::app::ApplicationController::startupFailed);
    QSignalSpy rejected(
        &controller, &vision::app::ApplicationController::actionRejected);
    QSignalSpy applied(
        &controller, &vision::app::ApplicationController::configurationApplied);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(startupFailure.wait(5000));
    auto candidate = controller.configuration();
    candidate.vision.cameras[0].serialNumber = "candidate-center";
    controller.requestApplyConfiguration(candidate);

    ASSERT_TRUE(processEventsUntil([&] { return rejected.count() == 1; }));
    EXPECT_EQ(applied.count(), 0);
    EXPECT_EQ(calls->load(), 2);
    EXPECT_TRUE(state->cameraDestroyed.load());
    EXPECT_EQ(state->cameraDestructionThread, state->creationThread);
    EXPECT_FALSE(controller.isTcpListening());
    EXPECT_FALSE(controller.snapshot().runtimeInitialized);
    EXPECT_EQ(controller.snapshot().applicationState,
              vision::app::ApplicationState::StartFailed);
    EXPECT_EQ(controller.snapshot().visionState,
              vision::app::VisionServiceState::Faulted);
    EXPECT_EQ(controller.snapshot().configurationApplyState,
              vision::app::ConfigurationApplyState::Failed);
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 1U);
    EXPECT_EQ(controller.configuration().vision.cameras[0].serialNumber,
              config.vision.cameras[0].serialNumber);
    EXPECT_TRUE(std::filesystem::is_directory(directory.path()));

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     InitialApplyRollbackFailurePrioritizesCleanupError)
{
    ensureApplication();
    support::ScopedTestDirectory directory(__func__);
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const auto path = temporaryConfigPath(directory);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        vision::app::saveApplicationConfig(path, config)));

    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, 0));
    const auto state = std::make_shared<RuntimeState>();
    state->failNextShutdown.store(true);
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
                "Initial hardware configuration failed.",
                "deterministic startup failure"};
        }
        return success(runtime, captureObserver, diagnostics, frameObserver);
    };
    vision::app::ApplicationController controller(
        config, std::move(factory), nullptr, nullptr, path);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy startupFailure(
        &controller, &vision::app::ApplicationController::startupFailed);
    QSignalSpy rejected(
        &controller, &vision::app::ApplicationController::actionRejected);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(startupFailure.wait(5000));
    auto candidate = controller.configuration();
    candidate.vision.cameras[0].serialNumber = "candidate-center";
    candidate.tcp.port = blocker.serverPort();
    controller.requestApplyConfiguration(candidate);

    ASSERT_TRUE(processEventsUntil([&] { return rejected.count() == 1; }));
    ASSERT_TRUE(controller.snapshot().lastError.has_value());
    EXPECT_EQ(controller.snapshot().lastError->code,
              vision::core::ErrorCode::HardwareFailure);
    EXPECT_NE(controller.snapshot().lastError->diagnostic.find(
                  "original configuration apply failure"),
              std::string::npos);
    EXPECT_TRUE(state->cameraDestroyed.load());
    EXPECT_FALSE(controller.snapshot().runtimeInitialized);
    EXPECT_FALSE(controller.isTcpListening());
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 1U);

    blocker.close();
    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     VisionApplyFailureRestoresPreviousRuntimeAndKeepsRevision)
{
    ensureApplication();
    support::ScopedTestDirectory directory(__func__);
    auto config = controllerConfig();
    ASSERT_NE(config.tcp.port, 0);
    const auto path = temporaryConfigPath(directory);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        vision::app::saveApplicationConfig(path, config)));
    const auto state = std::make_shared<RuntimeState>();
    const auto success = successfulFactory(state);
    const auto calls = std::make_shared<std::atomic_int>(0);
    vision::app::CoreFactory factory =
        [success, calls](const vision::app::VisionRuntimeConfig& runtime,
                         vision::hardware::ICaptureObserver* captureObserver,
                         vision::core::IDiagnosticSink* diagnostics,
                         vision::core::ILocateFrameObserver* frameObserver)
            -> vision::app::CoreFactoryOutcome {
        if (++(*calls) == 2) {
            return vision::core::Error{
                vision::core::ErrorCode::InvalidConfiguration,
                "Candidate runtime rejected.",
                "deterministic candidate creation failure"};
        }
        return success(runtime, captureObserver, diagnostics, frameObserver);
    };
    vision::app::ApplicationController controller(
        config, std::move(factory), nullptr, nullptr, path);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy rejected(
        &controller, &vision::app::ApplicationController::actionRejected);
    QSignalSpy applied(
        &controller, &vision::app::ApplicationController::configurationApplied);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);

    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));
    auto candidate = controller.configuration();
    candidate.vision.cameras[0].serialNumber = "candidate-center";
    controller.requestApplyConfiguration(candidate);

    ASSERT_TRUE(processEventsUntil([&] { return rejected.count() == 1; }));
    EXPECT_EQ(applied.count(), 0);
    EXPECT_EQ(calls->load(), 3);
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 1U);
    EXPECT_EQ(controller.snapshot().visionState,
              vision::app::VisionServiceState::Ready);
    EXPECT_EQ(controller.snapshot().configurationApplyState,
              vision::app::ConfigurationApplyState::Failed);
    EXPECT_EQ(controller.configuration().vision.cameras[0].serialNumber,
              config.vision.cameras[0].serialNumber);

    const auto persisted = vision::app::loadApplicationConfig(path);
    const auto* saved =
        std::get_if<vision::app::ApplicationConfig>(&persisted);
    ASSERT_NE(saved, nullptr);
    EXPECT_EQ(saved->vision.cameras[0].serialNumber,
              config.vision.cameras[0].serialNumber);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     AlgorithmValidationReturnsRequestIdsAndRejectsBusyAndClosingRequests)
{
    ensureApplication();
    auto config = controllerConfig();
    const auto state = std::make_shared<RuntimeState>();
    const quint16 port = config.tcp.port;
    vision::app::ApplicationController controller(
        std::move(config), successfulFactory(state), nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy validations(
        &controller,
        &vision::app::ApplicationController::algorithmValidationFinished);
    QSignalSpy rejected(
        &controller, &vision::app::ApplicationController::actionRejected);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);
    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));

    controller.requestAlgorithmValidation(QString{});
    ASSERT_TRUE(processEventsUntil([&] { return validations.count() == 1; }));
    const auto emptyCompletion = qvariant_cast<
        vision::app::AlgorithmValidationCompletion>(validations.at(0).at(0));
    EXPECT_EQ(emptyCompletion.requestId, 1U);
    ASSERT_NE(std::get_if<vision::core::Error>(&emptyCompletion.status), nullptr);

    state->blockCapture = true;
    ClientPeer client;
    client.connectTo(port);
    ASSERT_TRUE(processEventsUntil([&] {
        return client.socket.state() == QAbstractSocket::ConnectedState;
    }));
    client.send(
        "{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"6301\",\"mode\":\"EDGE\"}\n");
    ASSERT_TRUE(processEventsUntil([&] { return state->captureEntered.load(); }));
    controller.requestAlgorithmValidation(QStringLiteral("unused.yaml"));
    EXPECT_EQ(rejected.count(), 1);
    {
        std::lock_guard lock(state->mutex);
        state->releaseCapture = true;
    }
    state->releaseCondition.notify_all();
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState ==
            vision::app::TaskState::AwaitingResultAck;
    }));
    client.send("{\"version\":1,\"type\":\"resultAck\",\"taskId\":\"6301\"}\n");
    ASSERT_TRUE(processEventsUntil([&] {
        return controller.snapshot().taskState == vision::app::TaskState::Idle;
    }));

    controller.shutdown();
    controller.requestAlgorithmValidation(QStringLiteral("unused.yaml"));
    EXPECT_EQ(rejected.count(), 2);
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     NoChangeTcpLoggingAndCombinedChangesApplyDeterministically)
{
    ensureApplication();
    support::ScopedTestDirectory directory(__func__);
    auto config = controllerConfig();
    const auto path = temporaryConfigPath(directory);
    const auto state = std::make_shared<RuntimeState>();
    const auto calls = std::make_shared<std::atomic_int>(0);
    const auto success = successfulFactory(state);
    vision::app::CoreFactory factory =
        [success, calls](const vision::app::VisionRuntimeConfig& runtime,
                         vision::hardware::ICaptureObserver* observer,
                         vision::core::IDiagnosticSink* diagnostics,
                         vision::core::ILocateFrameObserver* frameObserver)
            -> vision::app::CoreFactoryOutcome {
        ++(*calls);
        return success(runtime, observer, diagnostics, frameObserver);
    };
    vision::app::ApplicationController controller(
        config, std::move(factory), nullptr, nullptr, path);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy applied(
        &controller, &vision::app::ApplicationController::configurationApplied);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);
    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));

    controller.requestApplyConfiguration(controller.configuration());
    ASSERT_TRUE(processEventsUntil([&] { return applied.count() == 1; }));
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 2U);

    auto tcpOnly = controller.configuration();
    tcpOnly.tcp.port = reserveLocalPort();
    ASSERT_NE(tcpOnly.tcp.port, 0);
    controller.requestApplyConfiguration(tcpOnly);
    ASSERT_TRUE(processEventsUntil([&] { return applied.count() == 2; }));
    EXPECT_EQ(controller.tcpPort(), tcpOnly.tcp.port);
    EXPECT_EQ(calls->load(), 1);

    auto loggingOnly = controller.configuration();
    loggingOnly.logging.directory = directory.path() / "new-logs";
    loggingOnly.logging.level = "debug";
    controller.requestApplyConfiguration(loggingOnly);
    ASSERT_TRUE(processEventsUntil([&] { return applied.count() == 3; }));
    EXPECT_EQ(controller.configuration().logging.level, "debug");
    EXPECT_EQ(calls->load(), 1);

    auto combined = controller.configuration();
    combined.tcp.port = reserveLocalPort();
    ASSERT_NE(combined.tcp.port, 0);
    combined.vision.cameras[0].serialNumber = "combined-center";
    combined.logging.level = "warn";
    controller.requestApplyConfiguration(combined);
    ASSERT_TRUE(processEventsUntil([&] { return applied.count() == 4; }));
    EXPECT_EQ(controller.tcpPort(), combined.tcp.port);
    EXPECT_EQ(controller.configuration().vision.cameras[0].serialNumber,
              "combined-center");
    EXPECT_EQ(controller.configuration().logging.level, "warn");
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 5U);
    EXPECT_EQ(calls->load(), 2);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     ValidationAndPersistenceFailuresKeepConfigurationAndRevision)
{
    ensureApplication();
    support::ScopedTestDirectory directory(__func__);
    auto config = controllerConfig();
    const auto path = directory.path() / "missing-parent" / "application.json";
    const auto state = std::make_shared<RuntimeState>();
    vision::app::ApplicationController controller(
        config, successfulFactory(state), nullptr, nullptr, path);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy rejected(
        &controller, &vision::app::ApplicationController::actionRejected);
    QSignalSpy applied(
        &controller, &vision::app::ApplicationController::configurationApplied);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);
    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));

    auto invalid = controller.configuration();
    invalid.tcp.port = 0;
    controller.requestApplyConfiguration(invalid);
    ASSERT_TRUE(processEventsUntil([&] { return rejected.count() == 1; }));
    EXPECT_EQ(applied.count(), 0);
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 1U);
    EXPECT_EQ(controller.configuration().tcp.port, config.tcp.port);

    auto cannotPersist = controller.configuration();
    cannotPersist.resultDelivery.maximumRetries += 1;
    controller.requestApplyConfiguration(cannotPersist);
    ASSERT_TRUE(processEventsUntil([&] { return rejected.count() == 2; }));
    EXPECT_EQ(applied.count(), 0);
    EXPECT_EQ(controller.snapshot().activeConfigurationRevision, 1U);
    EXPECT_EQ(controller.configuration().resultDelivery.maximumRetries,
              config.resultDelivery.maximumRetries);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

TEST(App_ApplicationControllerConfigurationTests,
     ValidAlgorithmRequestsKeepDistinctMonotonicRequestIds)
{
    ensureApplication();
    const auto workspaceRoot = findWorkspaceRoot();
    ASSERT_FALSE(workspaceRoot.empty());
    const auto validPath = workspaceRoot /
        "VisionAlgorithm" / "config" / "vision_algorithm.example.yaml";
    ASSERT_TRUE(std::filesystem::exists(validPath));

    auto config = controllerConfig();
    const auto state = std::make_shared<RuntimeState>();
    vision::app::ApplicationController controller(
        std::move(config), successfulFactory(state), nullptr);
    ControllerShutdownGuard shutdownGuard(controller, state);
    QSignalSpy validations(
        &controller,
        &vision::app::ApplicationController::algorithmValidationFinished);
    QSignalSpy shutdownFinished(
        &controller, &vision::app::ApplicationController::shutdownFinished);
    controller.start();
    ASSERT_TRUE(processEventsUntil([&] { return controller.isTcpListening(); }));

    controller.requestAlgorithmValidation(
        QString::fromStdWString(validPath.wstring()));
    controller.requestAlgorithmValidation(
        QString::fromStdWString((validPath.parent_path() / "missing.yaml").wstring()));
    ASSERT_TRUE(processEventsUntil([&] { return validations.count() == 2; }, 2500ms));

    const auto first = qvariant_cast<vision::app::AlgorithmValidationCompletion>(
        validations.at(0).at(0));
    const auto second = qvariant_cast<vision::app::AlgorithmValidationCompletion>(
        validations.at(1).at(0));
    EXPECT_EQ(first.requestId, 1U);
    EXPECT_EQ(second.requestId, 2U);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(first.status));
    const auto* error = std::get_if<vision::core::Error>(&second.status);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);

    controller.shutdown();
    ASSERT_TRUE(shutdownFinished.wait(5000));
}

} }
