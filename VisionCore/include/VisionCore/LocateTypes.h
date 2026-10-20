#pragma once

#include <functional>
#include <optional>
#include <variant>

#include "VisionCore/Error.h"
#include "VisionCore/ImageFrame.h"

namespace vision::core {

enum class LocateMode {
    Edge,
    MarkPoint
};

struct LocateOptions {
    explicit LocateOptions(LocateMode value) noexcept
        : mode(value) {}

    LocateMode mode;
};

struct EdgeAlgorithmInput {
    std::reference_wrapper<const cv::Mat> center;
};

struct MarkPointAlgorithmInput {
    std::reference_wrapper<const cv::Mat> topLeft;
    std::reference_wrapper<const cv::Mat> topRight;
    std::reference_wrapper<const cv::Mat> bottomRight;
    std::reference_wrapper<const cv::Mat> bottomLeft;
};

using AlgorithmInput = std::variant<EdgeAlgorithmInput, MarkPointAlgorithmInput>;

struct AlgorithmOutput {
    double x{ 0.0 };       
    double y{ 0.0 };        
    double r{ 0.0 };        
    bool valid{ false };    
};

enum class LocateDecision {
    Ok,
    Ng
};

struct LocateResult {
    double x;
    double y;
    double r;
    LocateDecision decision;
};

enum class CoreState {
    Ready,
    Locating,
    Faulted,
    ShuttingDown,
    Stopped
};

struct CoreStatus {
    CoreState state;
    std::optional<Error> lastError;
};

using CaptureOutcome = Outcome<ImageFrames>;
using AlgorithmOutcome = Outcome<AlgorithmOutput>;
using LocateOutcome = Outcome<LocateResult>;

} // namespace vision::core
