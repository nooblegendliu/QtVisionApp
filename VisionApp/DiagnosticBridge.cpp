#include "DiagnosticBridge.h"

namespace vision::app {

DiagnosticBridge::DiagnosticBridge(QObject* parent)
    : QObject(parent)
{
}

void DiagnosticBridge::submit(
    const vision::core::DiagnosticEvent& event) noexcept
{
    try {
        AppDiagnostic diagnostic;
        diagnostic.occurredAt = QDateTime::currentDateTime();
        diagnostic.severity = event.severity;
        diagnostic.component = QString::fromStdString(event.component);
        diagnostic.operation = QString::fromStdString(event.operation);
        diagnostic.message = QString::fromStdString(event.message);
        for (const auto& frame : event.frames) {
            diagnostic.frames.push_back(
                QStringLiteral("camera=%1 role=%2 frame=%3")
                    .arg(QString::fromStdString(frame.cameraId))
                    .arg(static_cast<int>(frame.cameraRole))
                    .arg(frame.frameNumber));
        }
        emit diagnosticReady(diagnostic);
    }
    catch (...) {
        // Diagnostics are best effort and cannot alter vision behavior.
    }
}

} // namespace vision::app
