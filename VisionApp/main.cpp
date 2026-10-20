#include "AppConfig.h"
#include "ApplicationController.h"
#include "Logging.h"
#include "FileRuntimeFactory.h"
#include "SimulationRuntimeFactory.h"
#include "VisionRuntimeFactory.h"
#include "WindowCoordinator.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QApplication>
#include <QTextStream>
#include <QTimer>
#include <QMessageBox>

#include <spdlog/logger.h>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <memory>
#include <variant>

namespace {

enum class RuntimeBackend {
    Hik,
    Simulation,
    File
};

std::atomic_bool shutdownSignalReceived{false};

extern "C" void handleShutdownSignal(int) noexcept
{
    shutdownSignalReceived.store(true, std::memory_order_relaxed);
}

[[nodiscard]] std::filesystem::path nativePath(const QString& value)
{
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toStdString());
#endif
}

void writeStartupError(const vision::core::Error& error)
{
    QTextStream stream(stderr);
    stream << "VisionApp startup failed: "
           << QString::fromStdString(error.message)
           << "\nDiagnostic: "
           << QString::fromStdString(error.diagnostic)
           << Qt::endl;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("QtVisionApp"));
    QCoreApplication::setApplicationName(QStringLiteral("VisionApp"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Industrial vision service and operator UI."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption configOption(
        {QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("Application JSON configuration file."),
        QStringLiteral("path"));
    const QCommandLineOption backendOption(
        QStringLiteral("backend"),
        QStringLiteral("Runtime backend: hik, simulation or file."),
        QStringLiteral("backend"),
        QStringLiteral("hik"));
    parser.addOption(configOption);
    parser.addOption(backendOption);
    const QCommandLineOption imageOption(
        QStringLiteral("image"),
        QStringLiteral("Local Center image for --backend file (real Edge algorithm)."),
        QStringLiteral("path"));
    parser.addOption(imageOption);
    parser.process(application);

    if (!parser.isSet(configOption)) {
        QTextStream(stderr)
            << "VisionApp requires --config <path>." << Qt::endl;
        return 2;
    }

    const QString backendText =
        parser.value(backendOption).trimmed().toLower();
    RuntimeBackend backend;
    if (backendText == QStringLiteral("hik")) {
        backend = RuntimeBackend::Hik;
    }
    else if (backendText == QStringLiteral("simulation")) {
        backend = RuntimeBackend::Simulation;
    }
    else if (backendText == QStringLiteral("file")) {
        backend = RuntimeBackend::File;
    }
    else {
        QTextStream(stderr)
            << "--backend must be hik, simulation or file." << Qt::endl;
        return 2;
    }
    if ((backend == RuntimeBackend::File && parser.value(imageOption).trimmed().isEmpty()) ||
        (backend != RuntimeBackend::File && parser.isSet(imageOption))) {
        QTextStream(stderr) << "--image <path> is required and only supported with --backend file." << Qt::endl;
        return 2;
    }

    const std::filesystem::path configPath =
        nativePath(parser.value(configOption));
    auto configOutcome = vision::app::loadApplicationConfig(configPath);
    if (const auto* error =
            std::get_if<vision::core::Error>(&configOutcome)) {
        writeStartupError(*error);
        QMessageBox::critical(
            nullptr,
            QStringLiteral("配置加载失败"),
            QString::fromStdString(error->message) + QStringLiteral("\n") +
                QString::fromStdString(error->diagnostic));
        return 2;
    }
    auto config = std::move(
        std::get<vision::app::ApplicationConfig>(configOutcome));

    if (backend == RuntimeBackend::Simulation &&
        !config.simulation.enabled) {
        writeStartupError(vision::core::Error{
            vision::core::ErrorCode::InvalidConfiguration,
            "The simulation backend is not enabled by configuration.",
            "--backend simulation requires simulation.enabled=true"});
        return 2;
    }
    auto loggerOutcome = vision::app::createApplicationLogger(config.logging);
    if (const auto* error =
            std::get_if<vision::core::Error>(&loggerOutcome)) {
        writeStartupError(*error);
        return 3;
    }
    auto logger = std::move(
        std::get<std::shared_ptr<spdlog::logger>>(loggerOutcome));

    vision::app::CoreFactory coreFactory;
    if (backend == RuntimeBackend::Simulation) {
        logger->warn(
            "DEVELOPMENT SIMULATION BACKEND ACTIVE; no physical hardware "
            "or production algorithm is being used");
        coreFactory = vision::app::makeSimulationCoreFactory(
            config.simulation);
    }
    else if (backend == RuntimeBackend::File) {
        logger->warn("OFFLINE FILE BACKEND ACTIVE; real algorithm, no physical camera or light");
        coreFactory = vision::app::makeFileCoreFactory(
            std::filesystem::absolute(nativePath(parser.value(imageOption))));
    }
    else {
        coreFactory = vision::app::makeProductionCoreFactory();
    }
    vision::app::ApplicationController controller(
        std::move(config),
        std::move(coreFactory),
        logger,
        nullptr,
        configPath);
    vision::app::WindowCoordinator windowCoordinator(
        &controller,
        backend == RuntimeBackend::Hik
            ? QStringLiteral("Hik")
            : backend == RuntimeBackend::File
                ? QStringLiteral("File / Real algorithm") : QStringLiteral("Simulation"));
    windowCoordinator.showInitialWindow();

    int processExitCode = 0;
    QObject::connect(
        &controller,
        &vision::app::ApplicationController::shutdownFinished,
        &application,
        [&] { application.exit(processExitCode); });

    std::signal(SIGINT, handleShutdownSignal);
    std::signal(SIGTERM, handleShutdownSignal);
    QTimer signalTimer;
    signalTimer.setInterval(100);
    QObject::connect(&signalTimer, &QTimer::timeout, &controller, [&] {
        if (shutdownSignalReceived.exchange(false, std::memory_order_relaxed)) {
            controller.shutdown();
        }
    });
    signalTimer.start();

    QTimer::singleShot(0, &controller, &vision::app::ApplicationController::start);
    const int result = application.exec();
    try {
        logger->flush();
    }
    catch (...) {
    }
    return result;
}
