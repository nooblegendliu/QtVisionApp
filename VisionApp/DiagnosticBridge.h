#pragma once

#include "AppTypes.h"

#include <VisionCore/Diagnostics.h>

#include <QObject>

namespace vision::app {

class DiagnosticBridge final :
    public QObject,
    public vision::core::IDiagnosticSink {
    Q_OBJECT

public:
    explicit DiagnosticBridge(QObject* parent = nullptr);

    void submit(
        const vision::core::DiagnosticEvent& event) noexcept override;

signals:
    void diagnosticReady(const vision::app::AppDiagnostic& diagnostic);
};

} // namespace vision::app
