#include "AppConfig.h"

#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>

namespace vision::app {
namespace {

class ConfigFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] QString pathText(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

[[nodiscard]] vision::core::Error invalidConfig(
    const std::filesystem::path& path,
    const std::string& detail)
{
    return vision::core::Error{
        vision::core::ErrorCode::InvalidConfiguration,
        "The VisionApp configuration is invalid.",
        "configPath=" + path.string() + "; " + detail};
}

[[nodiscard]] QJsonObject requireObject(
    const QJsonObject& parent,
    const QString& key)
{
    const QJsonValue value = parent.value(key);
    if (!value.isObject()) {
        throw ConfigFailure(
            QStringLiteral("%1 must be an object.").arg(key).toStdString());
    }
    return value.toObject();
}

[[nodiscard]] QString requireString(
    const QJsonObject& object,
    const QString& key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        throw ConfigFailure(
            QStringLiteral("%1 must be a non-empty string.")
                .arg(key).toStdString());
    }
    return value.toString().trimmed();
}

[[nodiscard]] double requireNumber(
    const QJsonObject& object,
    const QString& key)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        throw ConfigFailure(
            QStringLiteral("%1 must be a finite number.")
                .arg(key).toStdString());
    }
    return value.toDouble();
}

[[nodiscard]] qint64 requireInteger(
    const QJsonObject& object,
    const QString& key,
    qint64 minimum,
    qint64 maximum)
{
    const double number = requireNumber(object, key);
    if (std::floor(number) != number ||
        number < static_cast<double>(minimum) ||
        number > static_cast<double>(maximum)) {
        throw ConfigFailure(
            QStringLiteral("%1 must be an integer in [%2, %3].")
                .arg(key).arg(minimum).arg(maximum).toStdString());
    }
    return static_cast<qint64>(number);
}

[[nodiscard]] bool requireBoolean(
    const QJsonObject& object,
    const QString& key)
{
    const QJsonValue value = object.value(key);
    if (!value.isBool()) {
        throw ConfigFailure(
            QStringLiteral("%1 must be a boolean.")
                .arg(key).toStdString());
    }
    return value.toBool();
}

[[nodiscard]] std::optional<double> optionalFiniteNumber(
    const QJsonObject& object,
    const QString& key)
{
    if (!object.contains(key) || object.value(key).isNull()) {
        return std::nullopt;
    }
    return requireNumber(object, key);
}

[[nodiscard]] vision::core::CameraRole parseRole(const QString& text)
{
    if (text == QStringLiteral("CENTER")) {
        return vision::core::CameraRole::Center;
    }
    if (text == QStringLiteral("TOP_LEFT")) {
        return vision::core::CameraRole::TopLeft;
    }
    if (text == QStringLiteral("TOP_RIGHT")) {
        return vision::core::CameraRole::TopRight;
    }
    if (text == QStringLiteral("BOTTOM_RIGHT")) {
        return vision::core::CameraRole::BottomRight;
    }
    if (text == QStringLiteral("BOTTOM_LEFT")) {
        return vision::core::CameraRole::BottomLeft;
    }
    throw ConfigFailure("camera role is unknown: " + text.toStdString());
}

[[nodiscard]] vision::hardware::HikCameraModel parseModel(
    const QString& text)
{
    if (text == QStringLiteral("MV_CH250_90GM")) {
        return vision::hardware::HikCameraModel::MvCh25090Gm;
    }
    if (text == QStringLiteral("MV_CS120_10GM")) {
        return vision::hardware::HikCameraModel::MvCs12010Gm;
    }
    throw ConfigFailure("camera model is unknown: " + text.toStdString());
}

[[nodiscard]] vision::hardware::HikInputLine parseInputLine(
    const QString& text)
{
    if (text == QStringLiteral("LINE0")) {
        return vision::hardware::HikInputLine::Line0;
    }
    if (text == QStringLiteral("LINE2")) {
        return vision::hardware::HikInputLine::Line2;
    }
    throw ConfigFailure("trigger inputLine must be LINE0 or LINE2.");
}

[[nodiscard]] vision::hardware::HikTriggerActivation parseActivation(
    const QString& text)
{
    if (text == QStringLiteral("RISING_EDGE")) {
        return vision::hardware::HikTriggerActivation::RisingEdge;
    }
    if (text == QStringLiteral("FALLING_EDGE")) {
        return vision::hardware::HikTriggerActivation::FallingEdge;
    }
    throw ConfigFailure(
        "trigger activation must be RISING_EDGE or FALLING_EDGE.");
}

[[nodiscard]] vision::hardware::HikLightChannelRole parseLightRole(
    const QString& text)
{
    using Role = vision::hardware::HikLightChannelRole;
    if (text == QStringLiteral("WHITE_BACKLIGHT")) {
        return Role::WhiteBacklight;
    }
    if (text == QStringLiteral("COAXIAL_RED")) {
        return Role::CoaxialRed;
    }
    if (text == QStringLiteral("COAXIAL_GREEN")) {
        return Role::CoaxialGreen;
    }
    if (text == QStringLiteral("COAXIAL_BLUE")) {
        return Role::CoaxialBlue;
    }
    throw ConfigFailure("light channel role is unknown: " + text.toStdString());
}

[[nodiscard]] std::filesystem::path resolvePath(
    const std::filesystem::path& base,
    const QString& configured)
{
#ifdef _WIN32
    std::filesystem::path value(configured.toStdWString());
#else
    std::filesystem::path value(configured.toStdString());
#endif
    if (value.is_relative()) {
        value = base / value;
    }
    return value.lexically_normal();
}

void validateCameraSet(
    const std::array<vision::hardware::HikCameraDeviceConfig, 5>& cameras)
{
    std::set<vision::core::CameraRole> roles;
    std::set<std::string> serialNumbers;
    for (const auto& camera : cameras) {
        if (!roles.insert(camera.role).second) {
            throw ConfigFailure("camera roles must be unique.");
        }
        if (!serialNumbers.insert(camera.serialNumber).second) {
            throw ConfigFailure("camera serial numbers must be unique.");
        }
        const bool center = camera.role == vision::core::CameraRole::Center;
        if (center &&
            camera.model != vision::hardware::HikCameraModel::MvCh25090Gm) {
            throw ConfigFailure("CENTER must use MV_CH250_90GM.");
        }
        if (!center &&
            camera.model != vision::hardware::HikCameraModel::MvCs12010Gm) {
            throw ConfigFailure("corner cameras must use MV_CS120_10GM.");
        }
    }
    if (roles.size() != cameras.size() ||
        !roles.contains(vision::core::CameraRole::Center) ||
        !roles.contains(vision::core::CameraRole::TopLeft) ||
        !roles.contains(vision::core::CameraRole::TopRight) ||
        !roles.contains(vision::core::CameraRole::BottomRight) ||
        !roles.contains(vision::core::CameraRole::BottomLeft)) {
        throw ConfigFailure("all five stable camera roles are required.");
    }
}

[[nodiscard]] SimulationResultConfig parseSimulationResult(
    const QJsonObject& simulation,
    const QString& key)
{
    const QJsonObject object = requireObject(simulation, key);
    const QString outcome = requireString(
        object, QStringLiteral("outcome")).toUpper();

    SimulationResultConfig result;
    if (outcome == QStringLiteral("NG")) {
        return result;
    }
    if (outcome != QStringLiteral("OK")) {
        throw ConfigFailure(
            QStringLiteral("simulation.%1.outcome must be OK or NG.")
                .arg(key).toStdString());
    }

    result.decision = SimulationDecision::Ok;
    result.x = requireNumber(object, QStringLiteral("x"));
    result.y = requireNumber(object, QStringLiteral("y"));
    result.r = requireNumber(object, QStringLiteral("r"));
    return result;
}

[[nodiscard]] QString roleText(vision::core::CameraRole role)
{
    switch (role) {
    case vision::core::CameraRole::Center: return QStringLiteral("CENTER");
    case vision::core::CameraRole::TopLeft: return QStringLiteral("TOP_LEFT");
    case vision::core::CameraRole::TopRight: return QStringLiteral("TOP_RIGHT");
    case vision::core::CameraRole::BottomRight: return QStringLiteral("BOTTOM_RIGHT");
    case vision::core::CameraRole::BottomLeft: return QStringLiteral("BOTTOM_LEFT");
    default: throw ConfigFailure("camera role is unknown.");
    }
}

[[nodiscard]] QString modelText(vision::hardware::HikCameraModel model)
{
    return model == vision::hardware::HikCameraModel::MvCh25090Gm
        ? QStringLiteral("MV_CH250_90GM")
        : QStringLiteral("MV_CS120_10GM");
}

[[nodiscard]] QString lightRoleText(
    vision::hardware::HikLightChannelRole role)
{
    using Role = vision::hardware::HikLightChannelRole;
    switch (role) {
    case Role::WhiteBacklight: return QStringLiteral("WHITE_BACKLIGHT");
    case Role::CoaxialRed: return QStringLiteral("COAXIAL_RED");
    case Role::CoaxialGreen: return QStringLiteral("COAXIAL_GREEN");
    case Role::CoaxialBlue: return QStringLiteral("COAXIAL_BLUE");
    }
    throw ConfigFailure("light channel role is unknown.");
}

[[nodiscard]] QString storedPath(
    const std::filesystem::path& configPath,
    const std::filesystem::path& value)
{
    const QDir base(pathText(std::filesystem::absolute(configPath).parent_path()));
    return QDir::cleanPath(base.relativeFilePath(pathText(value)));
}

[[nodiscard]] QJsonObject simulationResultObject(
    const SimulationResultConfig& result)
{
    QJsonObject object;
    object.insert(QStringLiteral("outcome"),
        result.decision == SimulationDecision::Ok
            ? QStringLiteral("OK") : QStringLiteral("NG"));
    if (result.decision == SimulationDecision::Ok) {
        object.insert(QStringLiteral("x"), result.x);
        object.insert(QStringLiteral("y"), result.y);
        object.insert(QStringLiteral("r"), result.r);
    }
    return object;
}

[[nodiscard]] QJsonObject toJson(
    const std::filesystem::path& path,
    const ApplicationConfig& config)
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), config.schemaVersion);

    QJsonObject tcp;
    tcp.insert(QStringLiteral("listenAddress"), config.tcp.listenAddress.toString());
    tcp.insert(QStringLiteral("port"), static_cast<int>(config.tcp.port));
    tcp.insert(QStringLiteral("heartbeatTimeoutMs"),
        static_cast<qint64>(config.tcp.heartbeatTimeout.count()));
    tcp.insert(QStringLiteral("maximumFrameBytes"),
        static_cast<qint64>(config.tcp.maximumFrameBytes));
    root.insert(QStringLiteral("tcp"), tcp);

    QJsonObject vision;
    vision.insert(QStringLiteral("captureTimeoutMs"),
        static_cast<qint64>(config.vision.captureTimeout.count()));
    vision.insert(QStringLiteral("algorithmConfigPath"),
        storedPath(path, config.vision.algorithmConfigPath));

    QJsonObject trigger;
    trigger.insert(QStringLiteral("inputLine"),
        config.vision.productionTrigger.inputLine ==
                vision::hardware::HikInputLine::Line0
            ? QStringLiteral("LINE0") : QStringLiteral("LINE2"));
    trigger.insert(QStringLiteral("activation"),
        config.vision.productionTrigger.activation ==
                vision::hardware::HikTriggerActivation::RisingEdge
            ? QStringLiteral("RISING_EDGE") : QStringLiteral("FALLING_EDGE"));
    trigger.insert(QStringLiteral("delayUs"),
        config.vision.productionTrigger.delay.count());
    vision.insert(QStringLiteral("productionTrigger"), trigger);

    QJsonArray cameras;
    for (const auto& camera : config.vision.cameras) {
        QJsonObject object;
        object.insert(QStringLiteral("role"), roleText(camera.role));
        object.insert(QStringLiteral("model"), modelText(camera.model));
        object.insert(QStringLiteral("serialNumber"),
            QString::fromStdString(camera.serialNumber));
        object.insert(QStringLiteral("exposureUs"), camera.exposureTime
            ? QJsonValue(camera.exposureTime->count()) : QJsonValue(QJsonValue::Null));
        object.insert(QStringLiteral("gain"), camera.gain
            ? QJsonValue(*camera.gain) : QJsonValue(QJsonValue::Null));
        cameras.append(object);
    }
    vision.insert(QStringLiteral("cameras"), cameras);

    QJsonObject light;
    light.insert(QStringLiteral("serialNumber"),
        QString::fromStdString(config.vision.lightController.serialNumber));
    QJsonArray channels;
    for (const auto& channel : config.vision.lightController.channels) {
        QJsonObject object;
        object.insert(QStringLiteral("role"), lightRoleText(channel.role));
        object.insert(QStringLiteral("outputSelector"),
            QString::fromStdString(channel.outputSelector));
        object.insert(QStringLiteral("normalizedIntensity"),
            channel.normalizedIntensity);
        channels.append(object);
    }
    light.insert(QStringLiteral("channels"), channels);
    vision.insert(QStringLiteral("lightController"), light);
    root.insert(QStringLiteral("vision"), vision);

    QJsonObject delivery;
    delivery.insert(QStringLiteral("acknowledgementIntervalMs"),
        static_cast<qint64>(config.resultDelivery.acknowledgementInterval.count()));
    delivery.insert(QStringLiteral("maximumRetries"),
        config.resultDelivery.maximumRetries);
    root.insert(QStringLiteral("resultDelivery"), delivery);

    QJsonObject logging;
    logging.insert(QStringLiteral("directory"), storedPath(path, config.logging.directory));
    logging.insert(QStringLiteral("level"), QString::fromStdString(config.logging.level));
    logging.insert(QStringLiteral("maximumFileMiB"),
        static_cast<qint64>(config.logging.maximumFileBytes / (1024U * 1024U)));
    logging.insert(QStringLiteral("maximumFiles"),
        static_cast<qint64>(config.logging.maximumFiles));
    root.insert(QStringLiteral("logging"), logging);

    if (config.simulation.enabled) {
        QJsonObject simulation;
        simulation.insert(QStringLiteral("enabled"), true);
        simulation.insert(QStringLiteral("locateDelayMs"),
            static_cast<qint64>(config.simulation.locateDelay.count()));
        simulation.insert(QStringLiteral("failFirstCapture"),
            config.simulation.failFirstCapture);
        simulation.insert(QStringLiteral("edgeResult"),
            simulationResultObject(config.simulation.edgeResult));
        simulation.insert(QStringLiteral("markPointResult"),
            simulationResultObject(config.simulation.markPointResult));
        root.insert(QStringLiteral("simulation"), simulation);
    }
    return root;
}

} // namespace

ApplicationConfigOutcome loadApplicationConfig(
    const std::filesystem::path& path)
{
    try {
        QFile file(pathText(path));
        if (!file.open(QIODevice::ReadOnly)) {
            return invalidConfig(
                path,
                "file cannot be opened: " + file.errorString().toStdString());
        }

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
            return invalidConfig(
                path,
                "JSON parse failed: " + parseError.errorString().toStdString());
        }

        ApplicationConfig config;
        const QJsonObject root = document.object();
        if (root.contains(QStringLiteral("schemaVersion"))) {
            config.schemaVersion = static_cast<int>(requireInteger(
                root, QStringLiteral("schemaVersion"), 1, 1));
        }
        const QJsonObject tcp = requireObject(root, QStringLiteral("tcp"));
        const QString listenAddress =
            requireString(tcp, QStringLiteral("listenAddress"));
        if (!config.tcp.listenAddress.setAddress(listenAddress)) {
            throw ConfigFailure("tcp.listenAddress is not a valid IP address.");
        }
        config.tcp.port = static_cast<quint16>(requireInteger(
            tcp, QStringLiteral("port"), 1, 65535));
        config.tcp.heartbeatTimeout = std::chrono::milliseconds(requireInteger(
            tcp,
            QStringLiteral("heartbeatTimeoutMs"),
            1,
            std::numeric_limits<int>::max()));
        config.tcp.maximumFrameBytes = static_cast<qsizetype>(requireInteger(
            tcp,
            QStringLiteral("maximumFrameBytes"),
            256,
            std::numeric_limits<int>::max()));

        const QJsonObject vision =
            requireObject(root, QStringLiteral("vision"));
        config.vision.captureTimeout = std::chrono::milliseconds(
            requireInteger(
                vision,
                QStringLiteral("captureTimeoutMs"),
                1,
                std::numeric_limits<quint32>::max()));

        const QJsonValue camerasValue =
            vision.value(QStringLiteral("cameras"));
        if (!camerasValue.isArray() ||
            camerasValue.toArray().size() !=
                static_cast<qsizetype>(config.vision.cameras.size())) {
            throw ConfigFailure("vision.cameras must contain exactly five entries.");
        }
        const QJsonArray cameras = camerasValue.toArray();
        for (qsizetype index = 0; index < cameras.size(); ++index) {
            if (!cameras.at(index).isObject()) {
                throw ConfigFailure("every camera entry must be an object.");
            }
            const QJsonObject cameraObject = cameras.at(index).toObject();
            auto& camera = config.vision.cameras[static_cast<std::size_t>(index)];
            camera.role = parseRole(
                requireString(cameraObject, QStringLiteral("role")));
            camera.model = parseModel(
                requireString(cameraObject, QStringLiteral("model")));
            camera.serialNumber = requireString(
                cameraObject, QStringLiteral("serialNumber")).toStdString();
            if (const auto exposure = optionalFiniteNumber(
                    cameraObject, QStringLiteral("exposureUs"))) {
                if (*exposure <= 0.0) {
                    throw ConfigFailure("camera exposureUs must be positive.");
                }
                camera.exposureTime = vision::hardware::HikMicroseconds(*exposure);
            }
            camera.gain = optionalFiniteNumber(
                cameraObject, QStringLiteral("gain"));
        }
        validateCameraSet(config.vision.cameras);

        const QJsonObject trigger =
            requireObject(vision, QStringLiteral("productionTrigger"));
        config.vision.productionTrigger.mode =
            vision::hardware::HikTriggerMode::Hardware;
        config.vision.productionTrigger.inputLine = parseInputLine(
            requireString(trigger, QStringLiteral("inputLine")));
        config.vision.productionTrigger.activation = parseActivation(
            requireString(trigger, QStringLiteral("activation")));
        const double delayUs = requireNumber(
            trigger, QStringLiteral("delayUs"));
        if (delayUs < 0.0) {
            throw ConfigFailure("productionTrigger.delayUs cannot be negative.");
        }
        config.vision.productionTrigger.delay =
            vision::hardware::HikMicroseconds(delayUs);

        const QJsonObject lightController =
            requireObject(vision, QStringLiteral("lightController"));
        config.vision.lightController.serialNumber = requireString(
            lightController, QStringLiteral("serialNumber")).toStdString();
        const QJsonValue channelsValue =
            lightController.value(QStringLiteral("channels"));
        if (!channelsValue.isArray() ||
            channelsValue.toArray().size() !=
                static_cast<qsizetype>(
                    config.vision.lightController.channels.size())) {
            throw ConfigFailure(
                "vision.lightController.channels must contain exactly four entries.");
        }
        const QJsonArray channels = channelsValue.toArray();
        for (qsizetype index = 0; index < channels.size(); ++index) {
            if (!channels.at(index).isObject()) {
                throw ConfigFailure("every light channel entry must be an object.");
            }
            const QJsonObject channelObject = channels.at(index).toObject();
            auto& channel = config.vision.lightController.channels[
                static_cast<std::size_t>(index)];
            channel.role = parseLightRole(
                requireString(channelObject, QStringLiteral("role")));
            channel.outputSelector = requireString(
                channelObject, QStringLiteral("outputSelector")).toStdString();
            channel.normalizedIntensity = requireNumber(
                channelObject, QStringLiteral("normalizedIntensity"));
        }

        const std::filesystem::path absoluteConfigPath =
            std::filesystem::absolute(path).lexically_normal();
        const std::filesystem::path base = absoluteConfigPath.parent_path();
        config.vision.algorithmConfigPath = resolvePath(
            base,
            requireString(vision, QStringLiteral("algorithmConfigPath")));

        const QJsonObject logging =
            requireObject(root, QStringLiteral("logging"));
        config.logging.directory = resolvePath(
            base,
            requireString(logging, QStringLiteral("directory")));
        config.logging.level = requireString(
            logging, QStringLiteral("level")).toLower().toStdString();
        static const std::set<std::string> levels{
            "trace", "debug", "info", "warn", "error", "critical", "off"};
        if (!levels.contains(config.logging.level)) {
            throw ConfigFailure("logging.level is unknown.");
        }
        const qint64 maximumFileMiB = requireInteger(
            logging, QStringLiteral("maximumFileMiB"), 1, 1024);
        config.logging.maximumFileBytes =
            static_cast<std::size_t>(maximumFileMiB) * 1024U * 1024U;
        config.logging.maximumFiles = static_cast<std::size_t>(requireInteger(
            logging, QStringLiteral("maximumFiles"), 1, 1000));

        const QJsonObject resultDelivery =
            requireObject(root, QStringLiteral("resultDelivery"));
        config.resultDelivery.acknowledgementInterval =
            std::chrono::milliseconds(requireInteger(
                resultDelivery,
                QStringLiteral("acknowledgementIntervalMs"),
                1,
                std::numeric_limits<int>::max()));
        config.resultDelivery.maximumRetries = static_cast<int>(requireInteger(
            resultDelivery,
            QStringLiteral("maximumRetries"),
            0,
            100));

        if (root.contains(QStringLiteral("simulation"))) {
            const QJsonObject simulation =
                requireObject(root, QStringLiteral("simulation"));
            config.simulation.enabled = requireBoolean(
                simulation, QStringLiteral("enabled"));
            config.simulation.locateDelay = std::chrono::milliseconds(
                requireInteger(
                    simulation,
                    QStringLiteral("locateDelayMs"),
                    0,
                    10000));
            config.simulation.failFirstCapture = requireBoolean(
                simulation, QStringLiteral("failFirstCapture"));
            config.simulation.edgeResult = parseSimulationResult(
                simulation, QStringLiteral("edgeResult"));
            config.simulation.markPointResult = parseSimulationResult(
                simulation, QStringLiteral("markPointResult"));
        }
        return config;
    }
    catch (const ConfigFailure& failure) {
        return invalidConfig(path, failure.what());
    }
    catch (const std::exception& exception) {
        return vision::core::Error{
            vision::core::ErrorCode::InternalFailure,
            "VisionApp configuration loading failed internally.",
            "configPath=" + path.string() + "; " + exception.what()};
    }
    catch (...) {
        return vision::core::Error{
            vision::core::ErrorCode::InternalFailure,
            "VisionApp configuration loading failed internally.",
            "configPath=" + path.string() + "; unknown exception"};
    }
}

vision::core::Status validateApplicationConfig(
    const ApplicationConfig& config)
{
    try {
        if (config.schemaVersion != 1) {
            throw ConfigFailure("schemaVersion must be 1.");
        }
        if (config.tcp.port == 0 ||
            config.tcp.heartbeatTimeout <= std::chrono::milliseconds::zero() ||
            config.tcp.maximumFrameBytes < 256) {
            throw ConfigFailure("TCP configuration is out of range.");
        }
        if (config.vision.captureTimeout <= std::chrono::milliseconds::zero() ||
            config.vision.algorithmConfigPath.empty()) {
            throw ConfigFailure("vision timeout and algorithm path are required.");
        }
        validateCameraSet(config.vision.cameras);
        for (const auto& camera : config.vision.cameras) {
            if (camera.serialNumber.empty() ||
                (camera.exposureTime && camera.exposureTime->count() <= 0.0) ||
                (camera.gain && !std::isfinite(*camera.gain))) {
                throw ConfigFailure("camera values are invalid.");
            }
        }
        if (config.vision.productionTrigger.delay.count() < 0.0 ||
            config.vision.lightController.serialNumber.empty()) {
            throw ConfigFailure("trigger or light controller configuration is invalid.");
        }
        std::set<vision::hardware::HikLightChannelRole> lightRoles;
        for (const auto& channel : config.vision.lightController.channels) {
            if (!lightRoles.insert(channel.role).second ||
                channel.outputSelector.empty() ||
                !std::isfinite(channel.normalizedIntensity) ||
                channel.normalizedIntensity < 0.0 ||
                channel.normalizedIntensity > 1.0) {
                throw ConfigFailure("light channel configuration is invalid.");
            }
        }
        if (config.resultDelivery.acknowledgementInterval <=
                std::chrono::milliseconds::zero() ||
            config.resultDelivery.maximumRetries < 0 ||
            config.logging.directory.empty() ||
            config.logging.maximumFileBytes == 0 ||
            config.logging.maximumFiles == 0) {
            throw ConfigFailure("application policy or logging configuration is invalid.");
        }
        return std::monostate{};
    }
    catch (const ConfigFailure& failure) {
        return invalidConfig({}, failure.what());
    }
    catch (const std::exception& exception) {
        return vision::core::Error{
            vision::core::ErrorCode::InternalFailure,
            "VisionApp configuration validation failed internally.",
            exception.what()};
    }
}

vision::core::Status saveApplicationConfig(
    const std::filesystem::path& path,
    const ApplicationConfig& config)
{
    if (auto status = validateApplicationConfig(config);
        std::holds_alternative<vision::core::Error>(status)) {
        return status;
    }
    try {
        QSaveFile file(pathText(path));
        if (!file.open(QIODevice::WriteOnly)) {
            return vision::core::Error{
                vision::core::ErrorCode::InvalidConfiguration,
                "The application configuration could not be saved.",
                file.errorString().toStdString()};
        }
        const QByteArray bytes = QJsonDocument(toJson(path, config))
            .toJson(QJsonDocument::Indented);
        if (file.write(bytes) != bytes.size() || !file.commit()) {
            return vision::core::Error{
                vision::core::ErrorCode::InternalFailure,
                "The application configuration could not be saved.",
                file.errorString().toStdString()};
        }
        return std::monostate{};
    }
    catch (const std::exception& exception) {
        return vision::core::Error{
            vision::core::ErrorCode::InternalFailure,
            "The application configuration could not be saved.",
            exception.what()};
    }
}

} // namespace vision::app
