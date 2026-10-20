#include <cstdio>
#include <cstdlib>
#pragma once
#include "ApplicationController.h"

#include <VisionCore/Ports.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "TestSupport/QtEventLoopTestSupport.h"
#include "TestSupport/QtTcpTestSupport.h"

namespace vision::test::app {
namespace {

using namespace std::chrono_literals;

void ensureApplication()
{
    support::ensureQCoreApplication();
}

bool processEventsUntil(
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout = 5s)
{
    return support::processEventsUntil(predicate, timeout);
}

quint16 reserveLocalPort()
{
    return support::reserveLocalTcpPort();
}

vision::app::ApplicationConfig controllerConfig()
{
    vision::app::ApplicationConfig config;
    config.tcp.listenAddress = QHostAddress::LocalHost;
    config.tcp.port = reserveLocalPort();
    config.tcp.heartbeatTimeout = 5s;
    config.tcp.maximumFrameBytes = 4096;
    config.resultDelivery.acknowledgementInterval = 2s;
    config.resultDelivery.maximumRetries = 1;
    config.vision.captureTimeout = 100ms;
    config.vision.algorithmConfigPath = "algorithm/main.yaml";
    config.vision.productionTrigger.mode =
        vision::hardware::HikTriggerMode::Hardware;
    const std::array<vision::core::CameraRole, 5> cameraRoles{
        vision::core::CameraRole::Center,
        vision::core::CameraRole::TopLeft,
        vision::core::CameraRole::TopRight,
        vision::core::CameraRole::BottomRight,
        vision::core::CameraRole::BottomLeft};
    for (std::size_t index = 0; index < cameraRoles.size(); ++index) {
        auto& camera = config.vision.cameras[index];
        camera.role = cameraRoles[index];
        camera.model = index == 0
            ? vision::hardware::HikCameraModel::MvCh25090Gm
            : vision::hardware::HikCameraModel::MvCs12010Gm;
        camera.serialNumber = "camera-" + std::to_string(index);
        camera.exposureTime = vision::hardware::HikMicroseconds(1000.0);
        camera.gain = 0.0;
    }
    config.vision.lightController.serialNumber = "light-controller";
    const std::array<vision::hardware::HikLightChannelRole, 4> lightRoles{
        vision::hardware::HikLightChannelRole::WhiteBacklight,
        vision::hardware::HikLightChannelRole::CoaxialRed,
        vision::hardware::HikLightChannelRole::CoaxialGreen,
        vision::hardware::HikLightChannelRole::CoaxialBlue};
    for (std::size_t index = 0; index < lightRoles.size(); ++index) {
        auto& channel = config.vision.lightController.channels[index];
        channel.role = lightRoles[index];
        channel.outputSelector = "OUT" + std::to_string(index);
        channel.normalizedIntensity = index == 0 ? 0.7 : 0.2;
    }
    config.logging.directory = support::uniqueTestDirectory("controller-logs");
    config.logging.level = "info";
    config.logging.maximumFileBytes = 1024U * 1024U;
    config.logging.maximumFiles = 3;
    return config;
}

struct RuntimeState {
    std::mutex mutex;
    std::condition_variable releaseCondition;
    bool blockCapture{false};
    bool releaseCapture{false};
    bool blockRecovery{false};
    bool releaseRecovery{false};
    std::atomic_bool failNextCapture{false};
    std::atomic_bool failNextRecovery{false};
    std::atomic_bool failNextShutdown{false};
    std::atomic_bool captureEntered{false};
    std::atomic_bool recoveryEntered{false};
    std::vector<vision::core::LocateMode> captureModes;
    QThread* creationThread{nullptr};
    QThread* cameraDestructionThread{nullptr};
    std::atomic_bool cameraDestroyed{false};
};

vision::core::ImageFrame makeFrame(
    vision::core::CameraRole role,
    std::string cameraId,
    std::uint64_t number)
{
    return vision::core::ImageFrame{
        cv::Mat(4, 4, CV_8UC1, cv::Scalar(10)).clone(),
        std::chrono::steady_clock::now(),
        number,
        std::move(cameraId),
        role};
}

vision::core::ImageFrames makeFrames(vision::core::LocateMode mode)
{
    if (mode == vision::core::LocateMode::Edge) {
        return vision::core::ImageFrames{{makeFrame(
            vision::core::CameraRole::Center, "center", 1)}};
    }
    return vision::core::ImageFrames{{
        makeFrame(vision::core::CameraRole::TopLeft, "top-left", 2),
        makeFrame(vision::core::CameraRole::TopRight, "top-right", 3),
        makeFrame(vision::core::CameraRole::BottomRight, "bottom-right", 4),
        makeFrame(vision::core::CameraRole::BottomLeft, "bottom-left", 5)}};
}

class AppFakeCamera final : public vision::core::ICamera {
public:
    AppFakeCamera(
        std::shared_ptr<RuntimeState> state,
        vision::hardware::ICaptureObserver* observer)
        : state_(std::move(state)), observer_(observer)
    {
    }

    ~AppFakeCamera() override
    {
        state_->cameraDestructionThread = QThread::currentThread();
        state_->cameraDestroyed.store(true);
    }

    vision::core::CaptureOutcome capture(
        vision::core::LocateMode mode) override
    {
        if (observer_ != nullptr) {
            observer_->onPrepared(mode);
        }
        {
            std::unique_lock lock(state_->mutex);
            state_->captureModes.push_back(mode);
            state_->captureEntered.store(true);
            if (!state_->releaseCondition.wait_for(lock, 5s, [&] {
                return !state_->blockCapture || state_->releaseCapture;
            })) {
                ADD_FAILURE() << "Fake Capture gate timed out";
                return vision::core::Error{vision::core::ErrorCode::InternalFailure,
                    "Test gate timeout", "No release within five seconds"};
            }
        }
        if (state_->failNextCapture.exchange(false)) {
            return vision::core::Error{
                vision::core::ErrorCode::HardwareFailure,
                "Fake capture failed.",
                "deterministic App test failure"};
        }
        return makeFrames(mode);
    }

    vision::core::Status recover() override
    {
        {
            std::unique_lock lock(state_->mutex);
            state_->recoveryEntered.store(true);
            if (!state_->releaseCondition.wait_for(lock, 5s, [&] {
                return !state_->blockRecovery || state_->releaseRecovery;
            })) {
                ADD_FAILURE() << "Fake Recovery gate timed out";
                return vision::core::Error{vision::core::ErrorCode::InternalFailure,
                    "Test gate timeout", "No release within five seconds"};
            }
        }
        if (state_->failNextRecovery.exchange(false)) {
            return vision::core::Error{
                vision::core::ErrorCode::HardwareFailure,
                "Fake recovery failed.",
                "deterministic App test recovery failure"};
        }
        return std::monostate{};
    }

    vision::core::Status shutdown() override
    {
        if (state_->failNextShutdown.exchange(false)) {
            return vision::core::Error{
                vision::core::ErrorCode::HardwareFailure,
                "Fake camera shutdown failed.",
                "deterministic candidate shutdown failure"};
        }
        return std::monostate{};
    }

private:
    std::shared_ptr<RuntimeState> state_;
    vision::hardware::ICaptureObserver* observer_;
};

class AppFakeLight final : public vision::core::ILight {
public:
    vision::core::Status turnOn(vision::core::LocateMode) override
    {
        return std::monostate{};
    }

    vision::core::Status turnOff() override
    {
        return std::monostate{};
    }

    vision::core::Status recover() override
    {
        return std::monostate{};
    }

    vision::core::Status shutdown() override
    {
        return std::monostate{};
    }
};

class AppFakeAlgorithm final : public vision::core::ILocateAlgorithm {
public:
    vision::core::AlgorithmOutcome locate(
        const vision::core::AlgorithmInput&) override
    {
        return vision::core::AlgorithmOutput{1.25, -2.5, 0.125, true};
    }
};

vision::app::CoreFactory successfulFactory(
    const std::shared_ptr<RuntimeState>& state)
{
    return [state](
               const vision::app::VisionRuntimeConfig&,
               vision::hardware::ICaptureObserver* observer,
               vision::core::IDiagnosticSink* diagnostics,
               vision::core::ILocateFrameObserver* frameObserver)
               -> vision::app::CoreFactoryOutcome {
        state->creationThread = QThread::currentThread();
        return std::make_unique<vision::core::VisionCore>(
            std::make_unique<AppFakeCamera>(state, observer),
            std::make_unique<AppFakeLight>(),
            std::make_unique<AppFakeAlgorithm>(),
            diagnostics,
            frameObserver);
    };
}

std::vector<vision::core::LocateMode> capturedModes(
    const std::shared_ptr<RuntimeState>& state)
{
    std::lock_guard lock(state->mutex);
    return state->captureModes;
}

std::filesystem::path temporaryConfigPath(
    const support::ScopedTestDirectory& directory)
{
    return directory.path() / "application.json";
}

std::filesystem::path findWorkspaceRoot()
{
    const std::array<std::filesystem::path, 2> startingPoints{
        std::filesystem::current_path(),
        std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path()};

    for (auto current : startingPoints) {
        while (!current.empty()) {
            if (std::filesystem::exists(current / "QtVisionApp.slnx")) {
                return current;
            }

            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }
    }

    return {};
}

class ControllerShutdownGuard final {
public:
    ControllerShutdownGuard(vision::app::ApplicationController& controller,
                            std::shared_ptr<RuntimeState> state = {})
        : controller_(controller), state_(std::move(state)), finished_(
            &controller, &vision::app::ApplicationController::shutdownFinished) {}
    ~ControllerShutdownGuard() {
        if (state_) {
            { std::lock_guard lock(state_->mutex);
              state_->releaseCapture = true; state_->releaseRecovery = true; }
            state_->releaseCondition.notify_all();
        }
        if (!finished_.isEmpty()) return;
        controller_.shutdown();
        if (!support::processEventsUntil([&] { return !finished_.isEmpty(); }, 5s)) {
            ADD_FAILURE() << "Controller cleanup timed out; terminating test process";
            std::fflush(nullptr); std::_Exit(3);
        }
    }
private:
    vision::app::ApplicationController& controller_;
    std::shared_ptr<RuntimeState> state_;
    QSignalSpy finished_;
};

class ClientPeer final {
public:
    ClientPeer() : socket(client_.socket()) {}

    void connectTo(quint16 port)
    {
        client_.connectToLocalHost(port);
    }

    void send(const QByteArray& bytes)
    {
        client_.sendBytes(bytes);
    }

    std::vector<QJsonObject> takeAvailableObjects()
    {
        return client_.takeAvailableObjects();
    }

private:
    support::JsonLineClient client_;

public:
    QTcpSocket& socket;
};

} // namespace
} // namespace vision::test::app
