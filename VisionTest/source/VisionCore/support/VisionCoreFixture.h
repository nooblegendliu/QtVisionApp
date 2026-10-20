#pragma once

#include <memory>

#include <gtest/gtest.h>

#include "VisionCore/support/CoreTestData.h"
#include "VisionCore/support/CoreTestDoubles.h"
#include "VisionCore/VisionCore.h"

namespace vision::test::core {

class VisionCoreFixture : public ::testing::Test {
protected:
    void SetUp() override {
        composeDefaultCore(true);
    }

    void TearDown() override {
        if (core_) {
            static_cast<void>(core_->shutdown());
            core_.reset();
        }
    }

    void composeDefaultCore(bool withDiagnostics, bool withFrameObserver = false) {
        if (core_) {
            static_cast<void>(core_->shutdown());
            core_.reset();
        }
        callLog_.clear();
        sink_.reset();
        frameObserver_.reset();
        if (withDiagnostics) {
            sink_ = std::make_unique<FakeDiagnosticSink>();
        }
        if (withFrameObserver) {
            frameObserver_ = std::make_unique<FakeFrameObserver>(callLog_);
        }

        auto camera = std::make_unique<FakeCamera>(callLog_);
        auto light = std::make_unique<FakeLight>(callLog_);
        auto algorithm = std::make_unique<FakeLocateAlgorithm>(callLog_);
        camera_ = camera.get();
        light_ = light.get();
        algorithm_ = algorithm.get();
        camera_->setDefaultCapture(MakeEdgeFrames());
        algorithm_->setDefaultOutcome(MakeOkOutput());

        core_ = std::make_unique<vision::core::VisionCore>(
            std::move(camera), std::move(light), std::move(algorithm), sink_.get(), frameObserver_.get());
    }

    void clearCalls() {
        callLog_.clear();
    }

    CallLog callLog_;
    std::unique_ptr<FakeDiagnosticSink> sink_;
    std::unique_ptr<FakeFrameObserver> frameObserver_;
    std::unique_ptr<vision::core::VisionCore> core_;
    FakeCamera* camera_{nullptr};
    FakeLight* light_{nullptr};
    FakeLocateAlgorithm* algorithm_{nullptr};
};

} // namespace vision::test::core
