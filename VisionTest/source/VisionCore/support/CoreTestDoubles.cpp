#include "VisionCore/support/CoreTestDoubles.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <opencv2/core.hpp>

namespace vision::test::core {
namespace {

vision::core::Status SuccessStatus() {
    return std::monostate{};
}

vision::core::CaptureOutcome DefaultCapture() {
    cv::Mat image(1, 1, CV_8UC1);
    image.setTo(cv::Scalar(0));
    vision::core::ImageFrame frame{
        std::move(image),
        std::chrono::steady_clock::time_point{std::chrono::milliseconds{1}},
        1U,
        "default-camera",
        vision::core::CameraRole::Center};
    return vision::core::ImageFrames{std::vector<vision::core::ImageFrame>{std::move(frame)}};
}

template <typename T>
T TakeNext(std::deque<T>& values, const T& fallback) {
    if (values.empty()) {
        return fallback;
    }
    T value = std::move(values.front());
    values.pop_front();
    return value;
}

void ThrowIfRequested(std::deque<ExceptionKind>& exceptions, const char* operation) {
    if (exceptions.empty()) {
        return;
    }

    const ExceptionKind kind = exceptions.front();
    exceptions.pop_front();
    if (kind == ExceptionKind::Standard) {
        throw std::runtime_error(std::string("fake exception at ") + operation);
    }
    if (kind == ExceptionKind::NonStandard) {
        throw 7;
    }
}

ImageObservation ObserveImage(const cv::Mat& image) {
    ImageObservation observation;
    observation.data = image.data;
    observation.rows = image.rows;
    observation.columns = image.cols;
    observation.type = image.type();
    observation.step = image.step;
    const std::size_t bytes = image.total() * image.elemSize();
    observation.pixels.assign(image.data, image.data + bytes);
    return observation;
}

AlgorithmObservation ObserveInput(const vision::core::AlgorithmInput& input) {
    AlgorithmObservation observation;
    if (const auto* edge = std::get_if<vision::core::EdgeAlgorithmInput>(&input)) {
        observation.kind = AlgorithmInputKind::Edge;
        observation.images.push_back(ObserveImage(edge->center.get()));
        return observation;
    }

    const auto& markPoint = std::get<vision::core::MarkPointAlgorithmInput>(input);
    observation.kind = AlgorithmInputKind::MarkPoint;
    observation.images.push_back(ObserveImage(markPoint.topLeft.get()));
    observation.images.push_back(ObserveImage(markPoint.topRight.get()));
    observation.images.push_back(ObserveImage(markPoint.bottomRight.get()));
    observation.images.push_back(ObserveImage(markPoint.bottomLeft.get()));
    return observation;
}

} // namespace

void CallLog::record(CallOperation operation) {
    operations_.push_back(operation);
}

const std::vector<CallOperation>& CallLog::operations() const noexcept {
    return operations_;
}

void CallLog::clear() noexcept {
    operations_.clear();
}

FakeCamera::FakeCamera(CallLog& callLog, LifetimeLog* lifetimeLog)
    : callLog_(callLog), lifetimeLog_(lifetimeLog), defaultCapture_(DefaultCapture()) {}

FakeCamera::~FakeCamera() {
    if (lifetimeLog_ != nullptr) {
        ++lifetimeLog_->cameraDestructions;
    }
}

vision::core::CaptureOutcome FakeCamera::capture(vision::core::LocateMode mode) {
    callLog_.record(CallOperation::CameraCapture);
    captureModes_.push_back(mode);
    if (onCapture) {
        onCapture();
    }
    ThrowIfRequested(captureExceptions_, "camera.capture");
    return TakeNext(captureResults_, defaultCapture_);
}

vision::core::Status FakeCamera::recover() {
    callLog_.record(CallOperation::CameraRecover);
    ++recoverCalls_;
    if (onRecover) {
        onRecover();
    }
    ThrowIfRequested(recoverExceptions_, "camera.recover");
    return TakeNext(recoverResults_, SuccessStatus());
}

vision::core::Status FakeCamera::shutdown() {
    callLog_.record(CallOperation::CameraShutdown);
    ++shutdownCalls_;
    if (onShutdown) {
        onShutdown();
    }
    ThrowIfRequested(shutdownExceptions_, "camera.shutdown");
    return TakeNext(shutdownResults_, SuccessStatus());
}

void FakeCamera::setDefaultCapture(vision::core::CaptureOutcome outcome) {
    defaultCapture_ = std::move(outcome);
}

void FakeCamera::enqueueCapture(vision::core::CaptureOutcome outcome) {
    captureResults_.push_back(std::move(outcome));
}

void FakeCamera::enqueueCaptureException(ExceptionKind exception) {
    captureExceptions_.push_back(exception);
}

void FakeCamera::enqueueRecover(vision::core::Status status) {
    recoverResults_.push_back(std::move(status));
}

void FakeCamera::enqueueRecoverException(ExceptionKind exception) {
    recoverExceptions_.push_back(exception);
}

void FakeCamera::enqueueShutdown(vision::core::Status status) {
    shutdownResults_.push_back(std::move(status));
}

void FakeCamera::enqueueShutdownException(ExceptionKind exception) {
    shutdownExceptions_.push_back(exception);
}

std::size_t FakeCamera::captureCalls() const noexcept {
    return captureModes_.size();
}

std::size_t FakeCamera::recoverCalls() const noexcept {
    return recoverCalls_;
}

std::size_t FakeCamera::shutdownCalls() const noexcept {
    return shutdownCalls_;
}

const std::vector<vision::core::LocateMode>& FakeCamera::captureModes() const noexcept {
    return captureModes_;
}

FakeLight::FakeLight(CallLog& callLog, LifetimeLog* lifetimeLog)
    : callLog_(callLog), lifetimeLog_(lifetimeLog) {}

FakeLight::~FakeLight() {
    if (lifetimeLog_ != nullptr) {
        ++lifetimeLog_->lightDestructions;
    }
}

vision::core::Status FakeLight::turnOn(vision::core::LocateMode mode) {
    callLog_.record(CallOperation::LightTurnOn);
    ++turnOnCalls_;
    turnOnModes_.push_back(mode);
    if (onTurnOn) {
        onTurnOn();
    }
    ThrowIfRequested(turnOnExceptions_, "light.turnOn");
    return TakeNext(turnOnResults_, SuccessStatus());
}

vision::core::Status FakeLight::turnOff() {
    callLog_.record(CallOperation::LightTurnOff);
    ++turnOffCalls_;
    if (onTurnOff) {
        onTurnOff();
    }
    ThrowIfRequested(turnOffExceptions_, "light.turnOff");
    return TakeNext(turnOffResults_, SuccessStatus());
}

vision::core::Status FakeLight::recover() {
    callLog_.record(CallOperation::LightRecover);
    ++recoverCalls_;
    if (onRecover) {
        onRecover();
    }
    ThrowIfRequested(recoverExceptions_, "light.recover");
    return TakeNext(recoverResults_, SuccessStatus());
}

vision::core::Status FakeLight::shutdown() {
    callLog_.record(CallOperation::LightShutdown);
    ++shutdownCalls_;
    if (onShutdown) {
        onShutdown();
    }
    ThrowIfRequested(shutdownExceptions_, "light.shutdown");
    return TakeNext(shutdownResults_, SuccessStatus());
}

void FakeLight::enqueueTurnOn(vision::core::Status status) {
    turnOnResults_.push_back(std::move(status));
}

void FakeLight::enqueueTurnOnException(ExceptionKind exception) {
    turnOnExceptions_.push_back(exception);
}

void FakeLight::enqueueTurnOff(vision::core::Status status) {
    turnOffResults_.push_back(std::move(status));
}

void FakeLight::enqueueTurnOffException(ExceptionKind exception) {
    turnOffExceptions_.push_back(exception);
}

void FakeLight::enqueueRecover(vision::core::Status status) {
    recoverResults_.push_back(std::move(status));
}

void FakeLight::enqueueRecoverException(ExceptionKind exception) {
    recoverExceptions_.push_back(exception);
}

void FakeLight::enqueueShutdown(vision::core::Status status) {
    shutdownResults_.push_back(std::move(status));
}

void FakeLight::enqueueShutdownException(ExceptionKind exception) {
    shutdownExceptions_.push_back(exception);
}

std::size_t FakeLight::turnOnCalls() const noexcept {
    return turnOnCalls_;
}

std::size_t FakeLight::turnOffCalls() const noexcept {
    return turnOffCalls_;
}

std::size_t FakeLight::recoverCalls() const noexcept {
    return recoverCalls_;
}

std::size_t FakeLight::shutdownCalls() const noexcept {
    return shutdownCalls_;
}

const std::vector<vision::core::LocateMode>& FakeLight::turnOnModes() const noexcept {
    return turnOnModes_;
}

FakeLocateAlgorithm::FakeLocateAlgorithm(CallLog& callLog, LifetimeLog* lifetimeLog)
    : callLog_(callLog),
      lifetimeLog_(lifetimeLog),
      defaultOutcome_(vision::core::AlgorithmOutput{1.0, 2.0, 3.0, true}) {}

FakeLocateAlgorithm::~FakeLocateAlgorithm() {
    if (lifetimeLog_ != nullptr) {
        ++lifetimeLog_->algorithmDestructions;
    }
}

vision::core::AlgorithmOutcome FakeLocateAlgorithm::locate(const vision::core::AlgorithmInput& input) {
    callLog_.record(CallOperation::AlgorithmLocate);
    observations_.push_back(ObserveInput(input));
    if (onLocate) {
        onLocate();
    }
    ThrowIfRequested(exceptions_, "algorithm.locate");
    return TakeNext(outcomes_, defaultOutcome_);
}

void FakeLocateAlgorithm::setDefaultOutcome(vision::core::AlgorithmOutcome outcome) {
    defaultOutcome_ = std::move(outcome);
}

void FakeLocateAlgorithm::enqueueOutcome(vision::core::AlgorithmOutcome outcome) {
    outcomes_.push_back(std::move(outcome));
}

void FakeLocateAlgorithm::enqueueException(ExceptionKind exception) {
    exceptions_.push_back(exception);
}

std::size_t FakeLocateAlgorithm::locateCalls() const noexcept {
    return observations_.size();
}

const std::vector<AlgorithmObservation>& FakeLocateAlgorithm::observations() const noexcept {
    return observations_;
}

void FakeLocateAlgorithm::clearObservations() noexcept {
    observations_.clear();
}

FakeFrameObserver::FakeFrameObserver(CallLog& callLog) noexcept
    : callLog_(callLog) {}

void FakeFrameObserver::onFrames(const vision::core::ImageFrames& frames) noexcept {
    ++calls_;
    try {
        callLog_.record(CallOperation::FrameObserved);
        observations_.clear();
        observations_.reserve(frames.frames.size());
        for (const auto& frame : frames.frames) {
            observations_.push_back(ObserveImage(frame.image));
        }
    } catch (...) {
        discardedObservation_ = true;
        observations_.clear();
    }
}

std::size_t FakeFrameObserver::calls() const noexcept {
    return calls_;
}

const std::vector<ImageObservation>& FakeFrameObserver::observations() const noexcept {
    return observations_;
}

bool FakeFrameObserver::discardedObservation() const noexcept {
    return discardedObservation_;
}

void FakeDiagnosticSink::submit(const vision::core::DiagnosticEvent& event) noexcept {
    ++submitAttempts_;
    if (discardWrites_) {
        return;
    }

    try {
        events_.push_back(event);
    } catch (...) {
        discardWrites_ = true;
    }
}

void FakeDiagnosticSink::setDiscardWrites(bool discard) noexcept {
    discardWrites_ = discard;
}

bool FakeDiagnosticSink::discardedWrites() const noexcept {
    return discardWrites_;
}

std::size_t FakeDiagnosticSink::submitAttempts() const noexcept {
    return submitAttempts_;
}

const std::vector<vision::core::DiagnosticEvent>& FakeDiagnosticSink::events() const noexcept {
    return events_;
}

void FakeDiagnosticSink::clear() noexcept {
    events_.clear();
    submitAttempts_ = 0U;
}

} // namespace vision::test::core
