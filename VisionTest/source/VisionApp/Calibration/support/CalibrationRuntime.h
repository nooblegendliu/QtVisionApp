#pragma once
#include "ApplicationController.h"
#include "TestSupport/QtTcpTestSupport.h"
#include <VisionCore/Ports.h>
#include <opencv2/imgproc.hpp>
#include <QFile>
#include <QThread>
#include <atomic>
#include <array>
#include <mutex>
#include <vector>
#include <stdexcept>
namespace vision::test::app {
struct CalibrationUiRuntimeState {
    std::atomic_int captures{0};
    std::atomic_bool failCapture{false};
    std::atomic_bool failFactory{false};
    std::atomic_int factoryCalls{0};
    std::atomic_int locateCalls{0};
    std::atomic_bool missingCircle{false};
    std::atomic_bool lightOn{false};
    std::atomic_bool cameraDestroyed{false};
    std::atomic_bool threadContract{true};
    std::mutex mutex;
    std::vector<bool> lightChanges;
};

namespace calibration_support { namespace {
using namespace std::chrono_literals;
using namespace vision::core;
using namespace vision::app;

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

std::filesystem::path nativePath(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

void writeText(const QString& path, const QByteArray& content)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot create test resource.");
    require(file.write(content) == content.size(), "Cannot write complete test resource.");
}

cv::Mat gridImage(bool missing)
{
    cv::Mat image(480, 640, CV_8UC1, cv::Scalar(255));
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (missing && row == 2 && col == 2) continue;
            cv::circle(image, {160 + 80 * col, 80 + 80 * row}, 18,
                       cv::Scalar(0), cv::FILLED, cv::LINE_AA);
        }
    }
    return image;
}

class GridCamera final : public ICamera {
public:
    explicit GridCamera(std::shared_ptr<CalibrationUiRuntimeState> state)
        : state_(std::move(state)), thread_(QThread::currentThread()) {}
    ~GridCamera() override
    {
        if (QThread::currentThread() != thread_) state_->threadContract = false;
        state_->cameraDestroyed = true;
    }
    CaptureOutcome capture(LocateMode mode) override
    {
        if (QThread::currentThread() != thread_) state_->threadContract = false;
        const int number = ++state_->captures;
        if (state_->failCapture.exchange(false))
            return Error{ErrorCode::HardwareFailure, "Injected capture failure", "Calibration capture"};
        const std::vector<CameraRole> roles = mode == LocateMode::Edge
            ? std::vector<CameraRole>{CameraRole::Center}
            : std::vector<CameraRole>{CameraRole::BottomLeft, CameraRole::TopRight,
                                     CameraRole::TopLeft, CameraRole::BottomRight};
        ImageFrames frames;
        for (const auto role : roles) {
            const bool missing = state_->missingCircle &&
                (mode == LocateMode::Edge || role == CameraRole::BottomLeft);
            frames.frames.push_back(ImageFrame{gridImage(missing),
                std::chrono::steady_clock::now(), static_cast<std::uint64_t>(number),
                "TEST-GRID-" + std::to_string(static_cast<int>(role)), role});
        }
        return frames;
    }
    Status recover() override { return std::monostate{}; }
    Status shutdown() override
    {
        if (QThread::currentThread() != thread_) state_->threadContract = false;
        return std::monostate{};
    }
private:
    std::shared_ptr<CalibrationUiRuntimeState> state_;
    QThread* thread_;
};

class GridLight final : public ILight {
public:
    explicit GridLight(std::shared_ptr<CalibrationUiRuntimeState> state) : state_(std::move(state)) {}
    Status turnOn(LocateMode) override { change(true); return std::monostate{}; }
    Status turnOff() override { change(false); return std::monostate{}; }
    Status recover() override { return std::monostate{}; }
    Status shutdown() override { change(false); return std::monostate{}; }
private:
    void change(bool on)
    {
        state_->lightOn = on;
        std::lock_guard lock(state_->mutex);
        state_->lightChanges.push_back(on);
    }
    std::shared_ptr<CalibrationUiRuntimeState> state_;
};

class UnusedLocateAlgorithm final : public ILocateAlgorithm {
public:
    explicit UnusedLocateAlgorithm(std::shared_ptr<CalibrationUiRuntimeState> state) : state_(std::move(state)) {}
    AlgorithmOutcome locate(const AlgorithmInput&) override
    {
        ++state_->locateCalls;
        return Error{ErrorCode::InternalFailure, "Locate must not run in calibration UI test.", {}};
    }
private:
    std::shared_ptr<CalibrationUiRuntimeState> state_;
};

void generateResources(const QString& dir)
{
    writeText(dir + "/main.yaml", R"(%YAML:1.0
---
timeout_ms: 500
enabled_algorithms:
  edge: 1
  mark: 0
calibration:
  parameters: "calibration_parameters.yaml"
  center: "center.yaml"
  top_left: "top_left.yaml"
  top_right: "top_right.yaml"
  bottom_right: "bottom_right.yaml"
  bottom_left: "bottom_left.yaml"
edge:
  parameters: "unused_edge_parameters.yaml"
)");
    QByteArray parameters = "%YAML:1.0\n---\ngrid_side_points: 5\ndetection:\n";
    for (const char* role : {"center", "top_left", "top_right", "bottom_right", "bottom_left"}) {
        parameters += "  " + QByteArray(role) + ":\n"
            "    blob_color: \"dark\"\n"
            "    minimum_threshold: 20.0\n    maximum_threshold: 240.0\n"
            "    threshold_step: 10.0\n    minimum_repeatability: 2\n"
            "    minimum_distance_px: 20.0\n    minimum_area_px2: 100.0\n"
            "    maximum_area_px2: 10000.0\n    minimum_circularity: 0.75\n";
        cv::FileStorage storage("geometry.yaml", cv::FileStorage::WRITE | cv::FileStorage::MEMORY);
        const cv::Mat matrix = (cv::Mat_<double>(3, 3) << 500, 0, 320, 0, 500, 240, 0, 0, 1);
        storage << "image_width" << 640 << "image_height" << 480;
        storage << "camera_matrix" << matrix;
        storage << "distortion_coefficients" << cv::Mat::zeros(1, 5, CV_64F);
        storage << "new_camera_matrix" << matrix;
        storage << "image_to_physical" << cv::Mat(cv::Matx33d::eye());
        writeText(dir + "/" + role + ".yaml", QByteArray::fromStdString(storage.releaseAndGetString()));
    }
    parameters += "solver:\n  ransac_threshold_mm: 0.05\nquality:\n"
                  "  minimum_used_points: 24\n  maximum_rms_error_mm: 0.03\n"
                  "  maximum_residual_mm: 0.08\n";
    writeText(dir + "/calibration_parameters.yaml", parameters);
    QByteArray points;
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 5; ++col)
            points += QByteArray::number(10 * col) + ',' + QByteArray::number(10 * row) + '\n';
    writeText(dir + "/points.csv", points);
}

ApplicationConfig testConfig(const QString& dir)
{
    ApplicationConfig config;
    config.tcp.listenAddress = QHostAddress::LocalHost;
    config.tcp.port = support::reserveLocalTcpPort();
    require(config.tcp.port != 0, "Cannot reserve loopback TCP port.");
    config.tcp.heartbeatTimeout = 5s;
    config.tcp.maximumFrameBytes = 4096;
    config.vision.algorithmConfigPath = nativePath(dir + "/main.yaml");
    config.vision.captureTimeout = 100ms;
    config.vision.productionTrigger.mode = vision::hardware::HikTriggerMode::Hardware;
    const std::array roles{CameraRole::Center, CameraRole::TopLeft, CameraRole::TopRight,
                           CameraRole::BottomRight, CameraRole::BottomLeft};
    for (std::size_t i = 0; i < roles.size(); ++i) {
        auto& camera = config.vision.cameras[i];
        camera.role = roles[i];
        camera.model = i == 0 ? vision::hardware::HikCameraModel::MvCh25090Gm
                              : vision::hardware::HikCameraModel::MvCs12010Gm;
        camera.serialNumber = "UI-TEST-" + std::to_string(i);
        camera.exposureTime = vision::hardware::HikMicroseconds(1000.0);
    }
    const std::array lightRoles{vision::hardware::HikLightChannelRole::WhiteBacklight,
        vision::hardware::HikLightChannelRole::CoaxialRed,
        vision::hardware::HikLightChannelRole::CoaxialGreen,
        vision::hardware::HikLightChannelRole::CoaxialBlue};
    config.vision.lightController.serialNumber = "UI-TEST-LIGHT";
    for (std::size_t i = 0; i < lightRoles.size(); ++i) {
        auto& channel = config.vision.lightController.channels[i];
        channel.role = lightRoles[i];
        channel.outputSelector = "OUT" + std::to_string(i);
        channel.normalizedIntensity = 0.5;
    }
    config.logging.directory = nativePath(dir + "/logs");
    config.logging.maximumFileBytes = 1024 * 1024;
    config.logging.maximumFiles = 3;
    return config;
}
} } // namespace calibration_support

} // namespace vision::test::app
