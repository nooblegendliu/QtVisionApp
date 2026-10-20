#include "FileRuntimeFactory.h"

#include <VisionAlgorithm/VisionAlgorithm.h>
#include <VisionCore/VisionCore.h>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace vision::app {
namespace {

using namespace vision::core;

void diagnose(IDiagnosticSink* sink, const char* operation,
              const std::string& message) noexcept
{
    try {
        if (sink) sink->submit({DiagnosticSeverity::Info, "FileRuntime", operation, message, {}});
    }
    catch (...) { }
}

class FileCamera final : public ICamera {
public:
    FileCamera(cv::Mat image, vision::hardware::ICaptureObserver* observer,
               IDiagnosticSink* diagnostics)
        : image_(std::move(image)), observer_(observer), diagnostics_(diagnostics) {}

    CaptureOutcome capture(LocateMode mode) override
    {
        if (stopped_) return Error{ErrorCode::NotReady, "The file camera is stopped.", {}};
        if (mode != LocateMode::Edge) {
            return Error{ErrorCode::InvalidConfiguration,
                         "The file backend supports Edge mode only.", "A single Center image was configured."};
        }
        try {
            // Each capture owns its buffer; the cached source remains immutable.
            ImageFrame frame;
            frame.image = image_.clone();
            frame.cameraRole = CameraRole::Center;
            frame.cameraId = "FILE-CENTER";
            frame.capturedAt = std::chrono::steady_clock::now();
            frame.frameNumber = nextFrame_++;
            if (observer_) observer_->onPrepared(mode);
            diagnose(diagnostics_, "capture", "Center image loaded from file; real VisionAlgorithm will process this frame.");
            ImageFrames result;
            result.frames.push_back(std::move(frame));
            return result;
        }
        catch (const std::exception& e) {
            return Error{ErrorCode::InternalFailure, "The file frame could not be prepared.", e.what()};
        }
        catch (...) {
            return Error{ErrorCode::InternalFailure, "The file frame could not be prepared.", "Unknown exception."};
        }
    }

    Status recover() override
    {
        if (stopped_) return Error{ErrorCode::NotReady, "The file camera is stopped.", {}};
        return std::monostate{};
    }
    Status shutdown() override { stopped_ = true; image_.release(); return std::monostate{}; }

private:
    cv::Mat image_;
    vision::hardware::ICaptureObserver* observer_;
    IDiagnosticSink* diagnostics_;
    std::uint64_t nextFrame_{1};
    bool stopped_{false};
};

class FileLight final : public ILight {
public:
    Status turnOn(LocateMode) override { return recover(); }
    Status turnOff() override { return std::monostate{}; }
    Status recover() override
    {
        if (stopped_) return Error{ErrorCode::NotReady, "The file light is stopped.", {}};
        return std::monostate{};
    }
    Status shutdown() override { stopped_ = true; return std::monostate{}; }
private:
    bool stopped_{false};
};

} // namespace

CoreFactory makeFileCoreFactory(std::filesystem::path imagePath)
{
    return [imagePath = std::move(imagePath)](
        const VisionRuntimeConfig& config,
        vision::hardware::ICaptureObserver* observer,
        IDiagnosticSink* diagnostics,
        ILocateFrameObserver* frameObserver) -> CoreFactoryOutcome {
        try {
            std::error_code fileError;
            if (!std::filesystem::is_regular_file(imagePath, fileError)) {
                return Error{ErrorCode::InvalidConfiguration, "The offline image file is unavailable.", imagePath.string()};
            }
            // Filesystem path supports native Windows Unicode paths. Decode only in the vision thread.
            std::ifstream file(imagePath, std::ios::binary);
            if (!file) return Error{ErrorCode::InvalidConfiguration, "The offline image file could not be opened.", imagePath.string()};
            const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), {}};
            if (file.bad() || bytes.empty()) {
                return Error{ErrorCode::InvalidConfiguration, "The offline image file could not be read.", imagePath.string()};
            }
            cv::Mat image;
            try {
                image = cv::imdecode(bytes, cv::IMREAD_GRAYSCALE | cv::IMREAD_IGNORE_ORIENTATION);
            }
            catch (const cv::Exception& e) {
                return Error{ErrorCode::InvalidConfiguration, "The offline image could not be decoded.", e.what()};
            }
            if (image.empty() || image.type() != CV_8UC1 || image.dims != 2) {
                return Error{ErrorCode::InvalidConfiguration, "The offline image could not be decoded as grayscale.", imagePath.string()};
            }
            auto algorithm = vision::algorithm::VisionAlgorithm::create(config.algorithmConfigPath);
            if (const auto* error = std::get_if<Error>(&algorithm)) return *error;
            diagnose(diagnostics, "create", "Offline image: " + imagePath.filename().string() +
                " (" + std::to_string(image.cols) + "x" + std::to_string(image.rows) +
                "); file camera + real Edge algorithm; physical hardware is not used.");
            auto core = std::make_unique<VisionCore>(
                std::make_unique<FileCamera>(std::move(image), observer, diagnostics),
                std::make_unique<FileLight>(),
                std::move(std::get<std::unique_ptr<ILocateAlgorithm>>(algorithm)), diagnostics, frameObserver);
            const auto status = core->status();
            if (status.state != CoreState::Ready) {
                return status.lastError.value_or(Error{ErrorCode::InternalFailure, "The file runtime is not ready.", {}});
            }
            return core;
        }
        catch (const std::exception& e) {
            return Error{ErrorCode::InternalFailure, "The file runtime could not be created.", e.what()};
        }
        catch (...) {
            return Error{ErrorCode::InternalFailure, "The file runtime could not be created.", "Unknown exception."};
        }
    };
}

} // namespace vision::app
