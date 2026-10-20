#include "VisionApp/Calibration/support/CalibrationRuntime.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>

namespace vision::test::app { namespace {
using namespace calibration_support;
class App_CalibrationWorkflow : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(resources.isValid());
        generateResources(resources.path());
        runtime = std::make_shared<CalibrationUiRuntimeState>();
        auto state = runtime;
        CoreFactory factory = [state](const VisionRuntimeConfig&,
            vision::hardware::ICaptureObserver*, IDiagnosticSink* sink,
            ILocateFrameObserver* observer) -> CoreFactoryOutcome {
            ++state->factoryCalls;
            if (state->failFactory.exchange(false))
                return Error{ErrorCode::InternalFailure, "Injected factory failure", "candidate"};
            return std::make_unique<VisionCore>(std::make_unique<GridCamera>(state),
                std::make_unique<GridLight>(state), std::make_unique<UnusedLocateAlgorithm>(state), sink, observer);
        };
        controller = std::make_unique<ApplicationController>(testConfig(resources.path()),
            factory, nullptr, nullptr, nativePath(resources.path()+"/app.json"));
        QObject::connect(controller.get(), &ApplicationController::calibrationSnapshotChanged,
            controller.get(), [this](const CalibrationSnapshot& value) { snapshot=value; ++revision; });
        QObject::connect(controller.get(), &ApplicationController::shutdownFinished,
            controller.get(), [this] { stopped=true; });
        controller->start();
        await([&] { return controller->isTcpListening() && snapshot.backendAvailable; });
    }
    void TearDown() override {
        if (controller && !stopped) {
            controller->shutdown();
            if (!support::processEventsUntil([&] { return stopped; }, 5s)) {
                ADD_FAILURE() << "Calibration Controller shutdown timeout";
                std::fflush(nullptr); std::_Exit(3);
            }
        }
        if (runtime) { EXPECT_EQ(runtime->locateCalls, 0); EXPECT_TRUE(runtime->threadContract); }
        controller.reset();
    }
    void await(const std::function<bool()>& predicate) {
        require(support::processEventsUntil(predicate,5s), "Calibration public workflow timed out");
    }
    const CalibrationRoleSnapshot& role(CameraRole role) const {
        for (const auto& item:snapshot.roles) if(item.role==role)return item;
        throw std::runtime_error("Missing role snapshot");
    }
    std::vector<CameraRole> roles(CalibrationMode mode) const {
        if(mode==CalibrationMode::Center)return {CameraRole::Center};
        return {CameraRole::TopLeft,CameraRole::TopRight,CameraRole::BottomRight,CameraRole::BottomLeft};
    }
    void prepare(CalibrationMode mode) {
        for (const auto r:roles(mode)) {
            const int previous=revision;
            controller->requestCalibrationPointFile(mode,r,resources.path()+"/points.csv");
            await([&]{return revision>previous && role(r).pointFileValid;});
        }
        require(snapshot.state==CalibrationProcessState::ReadyToCapture,"Incomplete point inputs");
    }
    void captureAndDetect(CalibrationMode mode) {
        controller->requestCalibrationCapture(mode);
        await([&]{return snapshot.state==CalibrationProcessState::ImageReady;});
        EXPECT_FALSE(runtime->lightOn);
        controller->requestCalibrationDetection();
        await([&]{return snapshot.state==CalibrationProcessState::SelectingAnchors;});
    }
    void orientAndSolve(CalibrationMode mode) {
        for(const auto r:roles(mode)) {
            const auto points=role(r).detectionPoints;
            for(const auto target:{QPointF(160,80),QPointF(240,80)}) {
                int found=-1;
                for(int i=0;i<points.size();++i) {
                    const auto delta=points[i]-target;
                    if(QPointF::dotProduct(delta,delta)<=1.0)found=i;
                }
                require(found>=0,"Synthetic circle missing");
                const int previous=revision;
                controller->requestCalibrationPointSelection(r,found);
                await([&]{return revision>previous;});
            }
        }
        require(snapshot.state==CalibrationProcessState::ReadyToSolve,"Missing orientations");
        controller->requestCalibrationSolve();
        await([&]{return snapshot.state==CalibrationProcessState::CandidateReady;});
        controller->requestCalibrationEvaluation();
        await([&]{return snapshot.state==CalibrationProcessState::ReadyToApply;});
    }
    QTemporaryDir resources;
    std::shared_ptr<CalibrationUiRuntimeState> runtime;
    std::unique_ptr<ApplicationController> controller;
    CalibrationSnapshot snapshot;
    int revision{0}; bool stopped{false};
};

TEST_F(App_CalibrationWorkflow, CenterAndCornersApplyThroughRealWorkerThenResetWithoutStaleResults) {
    for(const auto mode:{CalibrationMode::Center,CalibrationMode::FourCorners}) {
        SCOPED_TRACE(static_cast<int>(mode));
        prepare(mode);captureAndDetect(mode);orientAndSolve(mode);
        for(const auto r:roles(mode)) {
            ASSERT_TRUE(role(r).passed); EXPECT_EQ(role(r).points.size(),25);
            EXPECT_EQ(role(r).detectedPointCount,25);
        }
        const int factories=runtime->factoryCalls;
        controller->requestCalibrationApply();
        await([&]{return snapshot.state==CalibrationProcessState::Completed;});
        EXPECT_EQ(runtime->factoryCalls,factories+1);
        EXPECT_FALSE(controller->snapshot().calibrationMaintenanceLocked);
        controller->requestCalibrationReset();
        await([&]{return snapshot.state==CalibrationProcessState::NoInput;});
        for(const auto& r:snapshot.roles) {EXPECT_FALSE(r.hasSolution);EXPECT_TRUE(r.detectionPoints.empty());}
    }
}

TEST_F(App_CalibrationWorkflow, RealMaintenanceSessionRejectsNetworkLocateRecoveryAndConfiguration) {
    prepare(CalibrationMode::Center);captureAndDetect(CalibrationMode::Center);
    ASSERT_TRUE(controller->snapshot().calibrationMaintenanceLocked);
    QSignalSpy rejected(controller.get(),&ApplicationController::actionRejected);
    controller->requestLocalRecovery();
    controller->requestApplyConfiguration(controller->configuration());
    EXPECT_EQ(rejected.count(),2);
    support::JsonLineClient client;
    client.connectToLocalHost(controller->tcpPort());
    await([&]{return client.socket().state()==QAbstractSocket::ConnectedState;});
    client.sendBytes("{\"version\":1,\"type\":\"prepareLocate\",\"taskId\":\"7701\",\"mode\":\"EDGE\"}\n");
    auto reply=client.takeObject();ASSERT_TRUE(reply);
    EXPECT_EQ(reply->value("errorCode").toString(),"BUSY");
    EXPECT_EQ(runtime->captures,1);
    controller->requestCalibrationReset();
    await([&]{return !controller->snapshot().calibrationMaintenanceLocked;});
}

TEST_F(App_CalibrationWorkflow, CandidateFactoryFailureRestoresFilesAndPreviousRuntime) {
    prepare(CalibrationMode::FourCorners);
    QFile source(resources.path()+"/top_left.yaml");ASSERT_TRUE(source.open(QIODevice::ReadOnly));
    const auto original=source.readAll();source.close();
    captureAndDetect(CalibrationMode::FourCorners);orientAndSolve(CalibrationMode::FourCorners);
    runtime->failFactory=true;
    controller->requestCalibrationApply();
    await([&]{return snapshot.state==CalibrationProcessState::Failed;});
    ASSERT_TRUE(snapshot.error);
    EXPECT_EQ(snapshot.error->code,ErrorCode::InternalFailure);
    EXPECT_TRUE(controller->snapshot().runtimeInitialized);
    ASSERT_TRUE(source.open(QIODevice::ReadOnly));EXPECT_EQ(source.readAll(),original);source.close();
    controller->requestCalibrationReset();await([&]{return snapshot.state==CalibrationProcessState::NoInput;});
    prepare(CalibrationMode::Center);captureAndDetect(CalibrationMode::Center);
}

TEST_F(App_CalibrationWorkflow, MissingCornerGridDoesNotProducePartialCandidateAndCanBeRetried) {
    prepare(CalibrationMode::FourCorners);runtime->missingCircle=true;
    controller->requestCalibrationCapture(CalibrationMode::FourCorners);
    await([&]{return snapshot.state==CalibrationProcessState::ImageReady;});
    const int previous=revision;controller->requestCalibrationDetection();
    await([&]{return revision>previous && snapshot.state==CalibrationProcessState::ImageReady;});
    EXPECT_TRUE(role(CameraRole::BottomLeft).detectionPoints.empty());
    EXPECT_FALSE(role(CameraRole::BottomLeft).hasSolution);
    runtime->missingCircle=false;
    captureAndDetect(CalibrationMode::FourCorners);orientAndSolve(CalibrationMode::FourCorners);
}
} }
