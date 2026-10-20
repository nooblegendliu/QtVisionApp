#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "VisionCore/ImageFrame.h"

namespace vision::core {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct FrameDiagnosticContext {
    std::chrono::steady_clock::time_point capturedAt;
    std::uint64_t frameNumber;
    std::string cameraId;
    CameraRole cameraRole;
};

struct DiagnosticEvent {
    DiagnosticSeverity severity;
    std::string component;
    std::string operation;
    std::string message;
    std::vector<FrameDiagnosticContext> frames;
};

class IDiagnosticSink {
public:
    virtual ~IDiagnosticSink() = default;

    virtual void submit(const DiagnosticEvent& event) noexcept = 0;
};

} // namespace vision::core
