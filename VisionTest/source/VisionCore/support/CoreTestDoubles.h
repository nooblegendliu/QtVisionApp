#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "VisionCore/Diagnostics.h"
#include "VisionCore/Ports.h"

namespace vision::test::core {

enum class CallOperation {
    LightTurnOn,
    LightTurnOff,
    LightRecover,
    LightShutdown,
    CameraCapture,
    CameraRecover,
    CameraShutdown,
    FrameObserved,
    AlgorithmLocate
};

class CallLog {
public:
    void record(CallOperation operation);
    const std::vector<CallOperation>& operations() const noexcept;
    void clear() noexcept;

private:
    std::vector<CallOperation> operations_;
};

enum class ExceptionKind {
    None,
    Standard,
    NonStandard
};

struct LifetimeLog {
    std::size_t cameraDestructions{0U};
    std::size_t lightDestructions{0U};
    std::size_t algorithmDestructions{0U};
};

struct ImageObservation {
    const unsigned char* data{nullptr};
    int rows{0};
    int columns{0};
    int type{0};
    std::size_t step{0U};
    std::vector<unsigned char> pixels;
};

enum class AlgorithmInputKind {
    Edge,
    MarkPoint
};

struct AlgorithmObservation {
    AlgorithmInputKind kind{AlgorithmInputKind::Edge};
    std::vector<ImageObservation> images;
};

class FakeCamera final : public vision::core::ICamera {
public:
    explicit FakeCamera(CallLog& callLog, LifetimeLog* lifetimeLog = nullptr);
    ~FakeCamera() override;

    vision::core::CaptureOutcome capture(vision::core::LocateMode mode) override;
    vision::core::Status recover() override;
    vision::core::Status shutdown() override;

    void setDefaultCapture(vision::core::CaptureOutcome outcome);
    void enqueueCapture(vision::core::CaptureOutcome outcome);
    void enqueueCaptureException(ExceptionKind exception);
    void enqueueRecover(vision::core::Status status);
    void enqueueRecoverException(ExceptionKind exception);
    void enqueueShutdown(vision::core::Status status);
    void enqueueShutdownException(ExceptionKind exception);

    std::function<void()> onCapture;
    std::function<void()> onRecover;
    std::function<void()> onShutdown;

    std::size_t captureCalls() const noexcept;
    std::size_t recoverCalls() const noexcept;
    std::size_t shutdownCalls() const noexcept;
    const std::vector<vision::core::LocateMode>& captureModes() const noexcept;

private:
    CallLog& callLog_;
    LifetimeLog* lifetimeLog_;
    vision::core::CaptureOutcome defaultCapture_;
    std::deque<vision::core::CaptureOutcome> captureResults_;
    std::deque<vision::core::Status> recoverResults_;
    std::deque<vision::core::Status> shutdownResults_;
    std::deque<ExceptionKind> captureExceptions_;
    std::deque<ExceptionKind> recoverExceptions_;
    std::deque<ExceptionKind> shutdownExceptions_;
    std::vector<vision::core::LocateMode> captureModes_;
    std::size_t recoverCalls_{0U};
    std::size_t shutdownCalls_{0U};
};

class FakeLight final : public vision::core::ILight {
public:
    explicit FakeLight(CallLog& callLog, LifetimeLog* lifetimeLog = nullptr);
    ~FakeLight() override;

    vision::core::Status turnOn(vision::core::LocateMode mode) override;
    vision::core::Status turnOff() override;
    vision::core::Status recover() override;
    vision::core::Status shutdown() override;

    void enqueueTurnOn(vision::core::Status status);
    void enqueueTurnOnException(ExceptionKind exception);
    void enqueueTurnOff(vision::core::Status status);
    void enqueueTurnOffException(ExceptionKind exception);
    void enqueueRecover(vision::core::Status status);
    void enqueueRecoverException(ExceptionKind exception);
    void enqueueShutdown(vision::core::Status status);
    void enqueueShutdownException(ExceptionKind exception);

    std::function<void()> onTurnOn;
    std::function<void()> onTurnOff;
    std::function<void()> onRecover;
    std::function<void()> onShutdown;

    std::size_t turnOnCalls() const noexcept;
    std::size_t turnOffCalls() const noexcept;
    std::size_t recoverCalls() const noexcept;
    std::size_t shutdownCalls() const noexcept;
    const std::vector<vision::core::LocateMode>& turnOnModes() const noexcept;

private:
    CallLog& callLog_;
    LifetimeLog* lifetimeLog_;
    std::deque<vision::core::Status> turnOnResults_;
    std::deque<vision::core::Status> turnOffResults_;
    std::deque<vision::core::Status> recoverResults_;
    std::deque<vision::core::Status> shutdownResults_;
    std::deque<ExceptionKind> turnOnExceptions_;
    std::deque<ExceptionKind> turnOffExceptions_;
    std::deque<ExceptionKind> recoverExceptions_;
    std::deque<ExceptionKind> shutdownExceptions_;
    std::vector<vision::core::LocateMode> turnOnModes_;
    std::size_t turnOnCalls_{0U};
    std::size_t turnOffCalls_{0U};
    std::size_t recoverCalls_{0U};
    std::size_t shutdownCalls_{0U};
};

class FakeLocateAlgorithm final : public vision::core::ILocateAlgorithm {
public:
    explicit FakeLocateAlgorithm(CallLog& callLog, LifetimeLog* lifetimeLog = nullptr);
    ~FakeLocateAlgorithm() override;

    vision::core::AlgorithmOutcome locate(const vision::core::AlgorithmInput& input) override;

    void setDefaultOutcome(vision::core::AlgorithmOutcome outcome);
    void enqueueOutcome(vision::core::AlgorithmOutcome outcome);
    void enqueueException(ExceptionKind exception);

    std::function<void()> onLocate;

    std::size_t locateCalls() const noexcept;
    const std::vector<AlgorithmObservation>& observations() const noexcept;
    void clearObservations() noexcept;

private:
    CallLog& callLog_;
    LifetimeLog* lifetimeLog_;
    vision::core::AlgorithmOutcome defaultOutcome_;
    std::deque<vision::core::AlgorithmOutcome> outcomes_;
    std::deque<ExceptionKind> exceptions_;
    std::vector<AlgorithmObservation> observations_;
};

class FakeFrameObserver final : public vision::core::ILocateFrameObserver {
public:
    explicit FakeFrameObserver(CallLog& callLog) noexcept;

    void onFrames(const vision::core::ImageFrames& frames) noexcept override;

    std::size_t calls() const noexcept;
    const std::vector<ImageObservation>& observations() const noexcept;
    bool discardedObservation() const noexcept;

private:
    CallLog& callLog_;
    std::size_t calls_{0U};
    bool discardedObservation_{false};
    std::vector<ImageObservation> observations_;
};

class FakeDiagnosticSink final : public vision::core::IDiagnosticSink {
public:
    void submit(const vision::core::DiagnosticEvent& event) noexcept override;

    void setDiscardWrites(bool discard) noexcept;
    bool discardedWrites() const noexcept;
    std::size_t submitAttempts() const noexcept;
    const std::vector<vision::core::DiagnosticEvent>& events() const noexcept;
    void clear() noexcept;

private:
    bool discardWrites_{false};
    std::size_t submitAttempts_{0U};
    std::vector<vision::core::DiagnosticEvent> events_;
};

} // namespace vision::test::core
