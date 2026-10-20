#include "SimulationRuntimeFactory.h"
#include "VisionWorker.h"
#include "TestSupport/QtEventLoopTestSupport.h"
#include <VisionCore/Ports.h>
#include <QSignalSpy>
#include <gtest/gtest.h>

namespace vision::test::app { namespace {
using namespace vision::app;
using namespace vision::core;
TEST(App_Simulation, OneShotCaptureFailureRecoveryAndConfiguredModeResultsRemainDistinct) {
    SimulationConfig config;
    config.enabled=true;config.failFirstCapture=true;
    config.edgeResult={SimulationDecision::Ok,12.5,-7.25,0.125};
    config.markPointResult={SimulationDecision::Ng,999,999,999};
    auto outcome=makeSimulationCoreFactory(config)({},nullptr,nullptr,nullptr);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<VisionCore>>(outcome));
    auto core=std::move(std::get<std::unique_ptr<VisionCore>>(outcome));
    const auto failed=core->locate(LocateOptions{LocateMode::Edge});
    ASSERT_TRUE(std::holds_alternative<Error>(failed));
    EXPECT_EQ(std::get<Error>(failed).code,ErrorCode::HardwareFailure);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(core->recover()));
    for(int repeat=0;repeat<2;++repeat) {
        const auto edge=core->locate(LocateOptions{LocateMode::Edge});
        ASSERT_TRUE(std::holds_alternative<LocateResult>(edge));
        const auto& ok=std::get<LocateResult>(edge);
        EXPECT_EQ(ok.decision,LocateDecision::Ok);
        EXPECT_EQ(ok.x,12.5);EXPECT_EQ(ok.y,-7.25);EXPECT_EQ(ok.r,0.125);
        const auto mark=core->locate(LocateOptions{LocateMode::MarkPoint});
        ASSERT_TRUE(std::holds_alternative<LocateResult>(mark));
        const auto& ng=std::get<LocateResult>(mark);
        EXPECT_EQ(ng.decision,LocateDecision::Ng);
        EXPECT_EQ(ng.x,0);EXPECT_EQ(ng.y,0);EXPECT_EQ(ng.r,0);
    }
    EXPECT_TRUE(std::holds_alternative<std::monostate>(core->shutdown()));
}

TEST(App_WorkerSnapshots, PublishedImagesOwnPixelsAfterFrameBufferIsOverwritten) {
    // Public synchronous Worker observation hook. No UI or hardware is constructed.
    support::ensureQCoreApplication();
    qRegisterMetaType<VisionTaskCompletion>();
    class Camera final : public ICamera {
    public:
        explicit Camera(std::shared_ptr<cv::Mat> pixels) : pixels_(std::move(pixels)) {}
        CaptureOutcome capture(LocateMode) override {
            return ImageFrames{{ImageFrame{*pixels_,std::chrono::steady_clock::now(),1,"snapshot",CameraRole::Center}}};
        }
        Status recover() override {return std::monostate{};}
        Status shutdown() override {return std::monostate{};}
    private: std::shared_ptr<cv::Mat> pixels_;
    };
    class Light final : public ILight {
    public:
        Status turnOn(LocateMode) override {return std::monostate{};}
        Status turnOff() override {return std::monostate{};}
        Status recover() override {return std::monostate{};}
        Status shutdown() override {return std::monostate{};}
    };
    class Algorithm final : public ILocateAlgorithm {
    public: AlgorithmOutcome locate(const AlgorithmInput&) override {return AlgorithmOutput{1,2,3,true};}
    };
    auto pixels=std::make_shared<cv::Mat>(4,4,CV_8UC1,cv::Scalar(73));
    CoreFactory factory=[pixels](const VisionRuntimeConfig&,vision::hardware::ICaptureObserver*,
        IDiagnosticSink* sink,ILocateFrameObserver* observer)->CoreFactoryOutcome {
        return std::make_unique<VisionCore>(std::make_unique<Camera>(pixels),std::make_unique<Light>(),
            std::make_unique<Algorithm>(),sink,observer);
    };
    VisionWorker worker({},factory);
    QSignalSpy completed(&worker,&VisionWorker::taskCompleted);
    worker.initialize();
    TcpTaskContext task;
    task.taskId="snapshot";task.options=LocateOptions{LocateMode::Edge};
    worker.locate(VisionTaskCommand{task});
    ASSERT_EQ(completed.count(),1);
    const auto first=qvariant_cast<VisionTaskCompletion>(completed.takeFirst()[0]);
    ASSERT_EQ(first.images.size(),1);
    const QImage original=first.images[0].image.copy();
    ASSERT_FALSE(original.isNull());
    pixels->setTo(cv::Scalar(19));
    EXPECT_EQ(first.images[0].image,original);
    worker.locate(VisionTaskCommand{task});
    ASSERT_EQ(completed.count(),1);
    auto second=qvariant_cast<VisionTaskCompletion>(completed.takeFirst()[0]);
    ASSERT_EQ(second.images.size(),1);
    second.images[0].image.fill(Qt::black);
    EXPECT_EQ(first.images[0].image,original);
    worker.shutdown();
}
} }
