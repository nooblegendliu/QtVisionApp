#include "Logging.h"
#include "TestSupport/TestDirectory.h"
#include <gtest/gtest.h>
#include <spdlog/logger.h>
#include <fstream>
#include <iterator>

namespace vision::test::app { namespace {
TEST(App_Logging, FiltersMessagesRotatesAndKeepsOwnedFilesWithinConfiguredCount) {
    support::ScopedTestDirectory directory("logging");
    vision::app::LoggingConfig config{directory.path(),"warn",512,2};
    auto result=vision::app::createApplicationLogger(config);
    ASSERT_TRUE(std::holds_alternative<std::shared_ptr<spdlog::logger>>(result));
    auto logger=std::get<std::shared_ptr<spdlog::logger>>(result);
    logger->info("FILTERED_SENTINEL");
    logger->warn("VISIBLE_SENTINEL");logger->flush();
    std::ifstream stream(directory.path()/"vision_app.log");
    const std::string contents{std::istreambuf_iterator<char>(stream),{}};stream.close();
    EXPECT_EQ(contents.find("FILTERED_SENTINEL"),std::string::npos);
    EXPECT_NE(contents.find("VISIBLE_SENTINEL"),std::string::npos);
    for(int i=0;i<80;++i)logger->warn("rotation {} {}",i,std::string(128,'x'));
    logger->flush();
    std::size_t count=0;
    for(const auto& file:std::filesystem::directory_iterator(directory.path())) {
        if(!file.is_regular_file())continue;
        ++count;EXPECT_LE(file.file_size(),512U);
    }
    EXPECT_EQ(count,3U);
}

TEST(App_Logging, InvalidConfigurationAndFileAsDirectoryReturnStableErrors) {
    support::ScopedTestDirectory directory("logging-errors");
    for(int field=0;field<4;++field) {
        SCOPED_TRACE(field);
        vision::app::LoggingConfig config{directory.path(),"info",1024,2};
        if(field==0)config.directory.clear();
        if(field==1)config.level="invalid";
        if(field==2)config.maximumFileBytes=0;
        if(field==3)config.maximumFiles=0;
        auto result=vision::app::createApplicationLogger(config);
        ASSERT_TRUE(std::holds_alternative<vision::core::Error>(result));
        EXPECT_EQ(std::get<vision::core::Error>(result).code,vision::core::ErrorCode::InvalidConfiguration);
    }
    const auto file=directory.path()/"file";std::ofstream(file)<<"not a directory";
    auto result=vision::app::createApplicationLogger({file,"info",1024,2});
    ASSERT_TRUE(std::holds_alternative<vision::core::Error>(result));
    EXPECT_EQ(std::get<vision::core::Error>(result).code,vision::core::ErrorCode::InternalFailure);
}
} }
