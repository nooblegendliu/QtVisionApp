#include <VisionHardware/HikHardware.h>
#include "VisionHardware/support/HikHardwareTestConfig.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

namespace vision::test::hardware { namespace {
using namespace vision::hardware;
using namespace vision::core;
struct CameraRule {
    std::string name;
    std::function<void(HikCameraSystemConfig&)> change;
    bool valid{false};
};
std::vector<CameraRule> cameraRules() {
    std::vector<CameraRule> rows;
    rows.push_back({"valid",[](auto&){},true});
    rows.push_back({"unordered roles",[](auto& c){std::reverse(c.cameras.begin(),c.cameras.end());},true});
    for(std::size_t slot=0;slot<5;++slot) {
        const auto label="slot "+std::to_string(slot)+" ";
        rows.push_back({label+"unknown role",[slot](auto& c){c.cameras[slot].role=CameraRole::Unknown;}});
        rows.push_back({label+"duplicate role",[slot](auto& c){c.cameras[slot].role=c.cameras[(slot+1)%5].role;}});
        rows.push_back({label+"model",[slot](auto& c){c.cameras[slot].model=slot==0?HikCameraModel::MvCs12010Gm:HikCameraModel::MvCh25090Gm;}});
        rows.push_back({label+"empty serial",[slot](auto& c){c.cameras[slot].serialNumber.clear();}});
        rows.push_back({label+"duplicate serial",[slot](auto& c){c.cameras[slot].serialNumber=c.cameras[(slot+1)%5].serialNumber;}});
        rows.push_back({label+"optional exposure/gain",[slot](auto& c){c.cameras[slot].exposureTime.reset();c.cameras[slot].gain.reset();},true});
        for(double value:{0.0,-1.0,std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::max()})
            rows.push_back({label+"exposure "+std::to_string(value),[slot,value](auto& c){c.cameras[slot].exposureTime=HikMicroseconds{value};}});
        for(double value:{std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::max()})
            rows.push_back({label+"gain "+std::to_string(value),[slot,value](auto& c){c.cameras[slot].gain=value;}});
    }
    const auto max=static_cast<long long>(std::numeric_limits<unsigned int>::max());
    for(auto value:{-1LL,0LL,1LL,max,max+1})
        rows.push_back({"capture milliseconds "+std::to_string(value),[value](auto& c){c.captureTimeout=std::chrono::milliseconds{value};},value>=1 && value<=max});
    rows.push_back({"unknown trigger mode",[](auto& c){c.trigger.mode=static_cast<HikTriggerMode>(-1);}});
    rows.push_back({"unknown input line",[](auto& c){c.trigger.inputLine=static_cast<HikInputLine>(-1);}});
    rows.push_back({"unknown activation",[](auto& c){c.trigger.activation=static_cast<HikTriggerActivation>(-1);}});
    for(auto line:{HikInputLine::Line0,HikInputLine::Line2})for(auto edge:{HikTriggerActivation::RisingEdge,HikTriggerActivation::FallingEdge})
        rows.push_back({"hardware trigger combination",[line,edge](auto& c){c.trigger.mode=HikTriggerMode::Hardware;c.trigger.inputLine=line;c.trigger.activation=edge;},true});
    for(double value:{-1.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        rows.push_back({"trigger delay",[value](auto& c){c.trigger.delay=HikMicroseconds{value};}});
    return rows;
}
class Hardware_CameraRules : public testing::TestWithParam<CameraRule> {};
TEST_P(Hardware_CameraRules, PureValidationMatchesSingleRule) {
    const auto& rule=GetParam();SCOPED_TRACE(rule.name);
    auto config=validHikCameraConfiguration();rule.change(config);
    const auto result=validateHikHardwareConfiguration(config,validHikLightConfiguration());
    if(rule.valid)EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
    else {ASSERT_TRUE(std::holds_alternative<Error>(result));EXPECT_EQ(std::get<Error>(result).code,ErrorCode::InvalidConfiguration);}
}
INSTANTIATE_TEST_CASE_P(ConfigurationMatrix,Hardware_CameraRules,testing::ValuesIn(cameraRules()));
TEST(Hardware_CameraCombination, MissingHardwareObserverIsRejectedBeforeSdk) {
    auto camera=validHikCameraConfiguration();camera.trigger.mode=HikTriggerMode::Hardware;
    RecordingHardwareDiagnostics diagnostics;
    auto result=createHikHardware(camera,validHikLightConfiguration(),nullptr,&diagnostics);
    ASSERT_TRUE(std::holds_alternative<Error>(result));
    EXPECT_EQ(std::get<Error>(result).code,ErrorCode::InvalidConfiguration);
    EXPECT_TRUE(diagnostics.events.empty());
}
} }
