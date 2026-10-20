#include <VisionHardware/HikHardware.h>
#include "VisionHardware/support/HikHardwareTestConfig.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>

namespace vision::test::hardware { namespace {
using namespace vision::hardware;
using namespace vision::core;
void expectLight(const HikLightControllerConfig& config,bool valid) {
    auto result=validateHikHardwareConfiguration(validHikCameraConfiguration(),config);
    if(valid)EXPECT_TRUE(std::holds_alternative<std::monostate>(result));
    else {ASSERT_TRUE(std::holds_alternative<Error>(result));EXPECT_EQ(std::get<Error>(result).code,ErrorCode::InvalidConfiguration);}
}
TEST(Hardware_LightRules, EveryChannelRejectsMissingRoleSelectorAndInvalidIntensity) {
    for(std::size_t slot=0;slot<4;++slot) {
        SCOPED_TRACE(slot);
        for(int rule=0;rule<4;++rule) {
            SCOPED_TRACE(rule);auto c=validHikLightConfiguration();
            if(rule==0)c.channels[slot].role=static_cast<HikLightChannelRole>(-1);
            if(rule==1)c.channels[slot].role=c.channels[(slot+1)%4].role;
            if(rule==2)c.channels[slot].outputSelector.clear();
            if(rule==3)c.channels[slot].outputSelector=c.channels[(slot+1)%4].outputSelector;
            expectLight(c,false);
        }
        for(double value:{-0.01,1.01,std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}) {
            SCOPED_TRACE(value);auto c=validHikLightConfiguration();c.channels[slot].normalizedIntensity=value;expectLight(c,false);
        }
    }
    auto c=validHikLightConfiguration();c.serialNumber.clear();expectLight(c,false);
}
TEST(Hardware_LightRules, BrightnessEndpointsRequireWhiteAndAtLeastOneRgbOutput) {
    // All 16 on/off combinations exercise the mode requirements independently.
    for(unsigned mask=0;mask<16;++mask) {
        SCOPED_TRACE(mask);auto c=validHikLightConfiguration();
        for(unsigned i=0;i<4;++i)c.channels[i].normalizedIntensity=(mask&(1U<<i))?1.0:0.0;
        expectLight(c,(mask&1U)!=0 && (mask&14U)!=0);
    }
    auto c=validHikLightConfiguration();std::reverse(c.channels.begin(),c.channels.end());expectLight(c,true);
}
} }
