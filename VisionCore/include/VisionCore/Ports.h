#pragma once

#include "VisionCore/LocateTypes.h"

namespace vision::core {

class ICamera {
public:
    virtual ~ICamera() = default;

    virtual CaptureOutcome capture(LocateMode mode) = 0;
    virtual Status recover() = 0;
    virtual Status shutdown() = 0;
};

class ILight {
public:
    virtual ~ILight() = default;

    virtual Status turnOn(LocateMode mode) = 0;
    virtual Status turnOff() = 0;
    virtual Status recover() = 0;
    virtual Status shutdown() = 0;
};

class ILocateAlgorithm {
public:
    virtual ~ILocateAlgorithm() = default;

    virtual AlgorithmOutcome locate(const AlgorithmInput& input) = 0;
};

class ILocateFrameObserver {
public:
    virtual ~ILocateFrameObserver() = default;

    // The observer may only read the frames during this synchronous call.
    // Implementations must create independent storage before returning.
    virtual void onFrames(const ImageFrames& frames) noexcept = 0;
};

} // namespace vision::core
