#include "AppConfig.h"
#include "ConfigurationService.h"

#include <QFile>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <utility>
#include <variant>
#include <vector>

#include "TestSupport/QtEventLoopTestSupport.h"

namespace vision::test::app {
namespace {

QByteArray validConfigJson()
{
    return R"JSON({
  "tcp": {
    "listenAddress": "127.0.0.1",
    "port": 19000,
    "heartbeatTimeoutMs": 2500,
    "maximumFrameBytes": 4096
  },
  "vision": {
    "captureTimeoutMs": 800,
    "algorithmConfigPath": "algorithm/main.yaml",
    "productionTrigger": {
      "inputLine": "LINE0",
      "activation": "RISING_EDGE",
      "delayUs": 12.5
    },
    "cameras": [
      {"role":"CENTER","model":"MV_CH250_90GM","serialNumber":"C","exposureUs":1000.0,"gain":0.0},
      {"role":"TOP_LEFT","model":"MV_CS120_10GM","serialNumber":"TL","exposureUs":1000.0,"gain":0.0},
      {"role":"TOP_RIGHT","model":"MV_CS120_10GM","serialNumber":"TR","exposureUs":1000.0,"gain":0.0},
      {"role":"BOTTOM_RIGHT","model":"MV_CS120_10GM","serialNumber":"BR","exposureUs":1000.0,"gain":0.0},
      {"role":"BOTTOM_LEFT","model":"MV_CS120_10GM","serialNumber":"BL","exposureUs":1000.0,"gain":0.0}
    ],
    "lightController": {
      "serialNumber": "LIGHT",
      "channels": [
        {"role":"WHITE_BACKLIGHT","outputSelector":"OUT0","normalizedIntensity":0.7},
        {"role":"COAXIAL_RED","outputSelector":"OUT1","normalizedIntensity":0.2},
        {"role":"COAXIAL_GREEN","outputSelector":"OUT2","normalizedIntensity":0.3},
        {"role":"COAXIAL_BLUE","outputSelector":"OUT3","normalizedIntensity":0.4}
      ]
    }
  },
  "resultDelivery": {
    "acknowledgementIntervalMs": 125,
    "maximumRetries": 4
  },
  "logging": {
    "directory": "logs",
    "level": "warn",
    "maximumFileMiB": 8,
    "maximumFiles": 6
  }
})JSON";
}

std::filesystem::path writeConfig(
    const support::ScopedTestDirectory& directory,
    const QByteArray& bytes)
{
    const auto filePath = directory.path() / "app.json";
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    EXPECT_TRUE(output.is_open()) << filePath.string();
    output.write(bytes.constData(), bytes.size());
    EXPECT_TRUE(output.good()) << filePath.string();
    return filePath;
}

TEST(App_AppConfigTests, ValidConfigurationMapsAllModuleSettingsAndRelativePaths)
{
    support::ScopedTestDirectory directory(__func__);
    const auto path = writeConfig(directory, validConfigJson());

    const auto outcome = vision::app::loadApplicationConfig(path);
    const auto* config = std::get_if<vision::app::ApplicationConfig>(&outcome);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->tcp.listenAddress, QHostAddress::LocalHost);
    EXPECT_EQ(config->tcp.port, 19000);
    EXPECT_EQ(config->tcp.heartbeatTimeout, std::chrono::milliseconds(2500));
    EXPECT_EQ(config->tcp.maximumFrameBytes, 4096);
    EXPECT_EQ(config->vision.captureTimeout, std::chrono::milliseconds(800));
    EXPECT_EQ(config->vision.lightController.serialNumber, "LIGHT");
    EXPECT_EQ(
        config->vision.lightController.channels[3].role,
        vision::hardware::HikLightChannelRole::CoaxialBlue);
    EXPECT_DOUBLE_EQ(
        config->vision.lightController.channels[3].normalizedIntensity,
        0.4);
    EXPECT_EQ(
        config->resultDelivery.acknowledgementInterval,
        std::chrono::milliseconds(125));
    EXPECT_EQ(config->resultDelivery.maximumRetries, 4);
    EXPECT_EQ(config->logging.maximumFileBytes, 8U * 1024U * 1024U);
    EXPECT_EQ(config->logging.maximumFiles, 6U);
    EXPECT_EQ(
        config->vision.algorithmConfigPath,
        path.parent_path() / "algorithm" / "main.yaml");
    EXPECT_EQ(config->logging.directory, path.parent_path() / "logs");
}

TEST(App_AppConfigTests, LegacyConfigurationIsSavedAtomicallyWithExplicitSchemaVersion)
{
    support::ScopedTestDirectory directory(__func__);
    const auto sourcePath = writeConfig(directory, validConfigJson());
    const auto loaded = vision::app::loadApplicationConfig(sourcePath);
    const auto* config = std::get_if<vision::app::ApplicationConfig>(&loaded);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->schemaVersion, 1);

    const auto savedPath = sourcePath.parent_path() / "saved.json";
    const auto saved = vision::app::saveApplicationConfig(savedPath, *config);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(saved));

#ifdef _WIN32
    QFile file(QString::fromStdWString(savedPath.wstring()));
#else
    QFile file(QString::fromStdString(savedPath.string()));
#endif
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray serialized = file.readAll();
    EXPECT_TRUE(serialized.contains("\"schemaVersion\": 1"));

    const auto roundTrip = vision::app::loadApplicationConfig(savedPath);
    const auto* reloaded =
        std::get_if<vision::app::ApplicationConfig>(&roundTrip);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->schemaVersion, 1);
    EXPECT_EQ(reloaded->tcp.port, config->tcp.port);
    EXPECT_EQ(reloaded->vision.cameras[0].serialNumber,
              config->vision.cameras[0].serialNumber);
    EXPECT_EQ(reloaded->vision.algorithmConfigPath,
              config->vision.algorithmConfigPath);
    EXPECT_EQ(reloaded->logging.directory, config->logging.directory);
}

TEST(App_AppConfigTests, FailedSaveDoesNotOverwriteExistingConfiguration)
{
    support::ScopedTestDirectory directory(__func__);
    const QByteArray original = validConfigJson();
    const auto path = writeConfig(directory, original);
    const auto loaded = vision::app::loadApplicationConfig(path);
    const auto* valid = std::get_if<vision::app::ApplicationConfig>(&loaded);
    ASSERT_NE(valid, nullptr);

    auto invalid = *valid;
    invalid.tcp.port = 0;
    const auto status = vision::app::saveApplicationConfig(path, invalid);
    ASSERT_NE(std::get_if<vision::core::Error>(&status), nullptr);

#ifdef _WIN32
    QFile file(QString::fromStdWString(path.wstring()));
#else
    QFile file(QString::fromStdString(path.string()));
#endif
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), original);
}

TEST(App_AppConfigTests, MissingResultDeliveryIsRejectedBeforeRuntimeStartup)
{
    support::ScopedTestDirectory directory(__func__);
    QByteArray json = validConfigJson();
    const qsizetype begin = json.indexOf("  \"resultDelivery\"");
    const qsizetype end = json.indexOf("  \"logging\"");
    ASSERT_GE(begin, 0);
    ASSERT_GT(end, begin);
    json.remove(begin, end - begin);
    const auto path = writeConfig(directory, json);

    const auto outcome = vision::app::loadApplicationConfig(path);
    const auto* error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
}

TEST(App_AppConfigTests, TcpAndDeliveryBoundsUseUnitBearingValidation)
{
    support::ScopedTestDirectory directory(__func__);
    QByteArray json = validConfigJson();
    json.replace("\"heartbeatTimeoutMs\": 2500", "\"heartbeatTimeoutMs\": 0");
    const auto path = writeConfig(directory, json);

    const auto outcome = vision::app::loadApplicationConfig(path);
    const auto* error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
}

TEST(App_AppConfigTests, MalformedJsonAndUnsupportedSchemaReturnStableConfigurationError)
{
    support::ScopedTestDirectory directory(__func__);
    auto outcome = vision::app::loadApplicationConfig(
        writeConfig(directory, "{not-json"));
    auto* error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);

    QByteArray unsupported = validConfigJson();
    unsupported.prepend("{\"schemaVersion\":2,\"legacy\":");
    unsupported.append("}");
    outcome = vision::app::loadApplicationConfig(
        writeConfig(directory, unsupported));
    error = std::get_if<vision::core::Error>(&outcome);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
}

TEST(App_AppConfigTests, MissingSchemaRemainsExplicitVersionOneLegacyInput)
{
    support::ScopedTestDirectory directory(__func__);
    const auto outcome = vision::app::loadApplicationConfig(
        writeConfig(directory, validConfigJson()));
    const auto* config = std::get_if<vision::app::ApplicationConfig>(&outcome);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->schemaVersion, 1);
}

TEST(App_AppConfigTests, UnknownPublicEnumStringsAreRejected)
{
    const std::vector<std::pair<QByteArray, QByteArray>> cases{
        {"\"CENTER\"", "\"CENTER_UNKNOWN\""},
        {"\"MV_CH250_90GM\"", "\"CAMERA_UNKNOWN\""},
        {"\"LINE0\"", "\"LINE_UNKNOWN\""},
        {"\"RISING_EDGE\"", "\"EDGE_UNKNOWN\""},
        {"\"WHITE_BACKLIGHT\"", "\"LIGHT_UNKNOWN\""},
        {"\"warn\"", "\"verbose\""}};
    for (std::size_t index = 0; index < cases.size(); ++index) {
        support::ScopedTestDirectory directory(__func__);
        QByteArray json = validConfigJson();
        json.replace(cases[index].first, cases[index].second);
        const auto outcome = vision::app::loadApplicationConfig(
            writeConfig(directory, json));
        const auto* error = std::get_if<vision::core::Error>(&outcome);
        ASSERT_NE(error, nullptr) << index;
        EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration) << index;
    }
}

TEST(App_AppConfigTests, MinimumTimeSizeAndRetryBoundariesAreAccepted)
{
    support::ScopedTestDirectory directory(__func__);
    QByteArray json = validConfigJson();
    json.replace("\"port\": 19000", "\"port\": 1");
    json.replace("\"heartbeatTimeoutMs\": 2500", "\"heartbeatTimeoutMs\": 1");
    json.replace("\"maximumFrameBytes\": 4096", "\"maximumFrameBytes\": 256");
    json.replace("\"captureTimeoutMs\": 800", "\"captureTimeoutMs\": 1");
    json.replace("\"acknowledgementIntervalMs\": 125", "\"acknowledgementIntervalMs\": 1");
    json.replace("\"maximumRetries\": 4", "\"maximumRetries\": 0");
    json.replace("\"maximumFileMiB\": 8", "\"maximumFileMiB\": 1");
    json.replace("\"maximumFiles\": 6", "\"maximumFiles\": 1");

    const auto outcome = vision::app::loadApplicationConfig(
        writeConfig(directory, json));
    const auto* config = std::get_if<vision::app::ApplicationConfig>(&outcome);
    ASSERT_NE(config, nullptr);
    EXPECT_EQ(config->tcp.port, 1);
    EXPECT_EQ(config->tcp.heartbeatTimeout, std::chrono::milliseconds{1});
    EXPECT_EQ(config->tcp.maximumFrameBytes, 256);
    EXPECT_EQ(config->resultDelivery.maximumRetries, 0);
}

TEST(App_AppConfigTests, ConfigurationServicePropagatesPureHardwareValidation)
{
    support::ScopedTestDirectory directory(__func__);
    const auto path = writeConfig(directory, validConfigJson());
    const auto loaded = vision::app::loadApplicationConfig(path);
    const auto* valid = std::get_if<vision::app::ApplicationConfig>(&loaded);
    ASSERT_NE(valid, nullptr);
    vision::app::ConfigurationService service(path, *valid);
    auto invalid = *valid;
    invalid.vision.lightController.channels[2].outputSelector =
        invalid.vision.lightController.channels[1].outputSelector;

    const auto status = service.validate(invalid);

    const auto* error = std::get_if<vision::core::Error>(&status);
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, vision::core::ErrorCode::InvalidConfiguration);
}

} // namespace
} // namespace vision::test::app
