#pragma once

#include <QHostAddress>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <optional>

namespace vision::tcp {

inline constexpr int kProtocolVersion = 1;

struct TcpServerConfig {
    QHostAddress listenAddress{QHostAddress::AnyIPv4};
    quint16 port{9000};
    std::chrono::milliseconds heartbeatTimeout{std::chrono::seconds(3)};
    qsizetype maximumFrameBytes{16 * 1024};
};

struct ConnectionContext {
    quint64 generation{0};

    [[nodiscard]] bool isValid() const noexcept
    {
        return generation != 0;
    }

    friend bool operator==(
        const ConnectionContext&,
        const ConnectionContext&) = default;
};

enum class TcpLocateMode {
    Edge,
    MarkPoint
};

enum class TcpErrorCode {
    ProtocolError,
    UnsupportedVersion,
    MessageTooLarge,
    SingleClientOnly,
    NotReady,
    Busy,
    InvalidConfiguration,
    HardwareUnavailable,
    HardwareFailure,
    HardwareTimeout,
    InvalidImage,
    AlgorithmFailure,
    AlgorithmTimeout,
    InvalidResult,
    InternalFailure
};

enum class LocateReplyOutcome {
    Ok,
    Ng,
    Error
};

enum class TcpApplicationState {
    Starting,
    Ready,
    ShuttingDown,
    Stopped
};

enum class TcpVisionState {
    Starting,
    Ready,
    Faulted,
    Recovering,
    Stopped
};

enum class TcpTaskState {
    Idle,
    Preparing,
    Running,
    AwaitingResultAck
};

struct PrepareLocateCommand {
    QString taskId;
    TcpLocateMode mode{TcpLocateMode::Edge};
    ConnectionContext connection;
};

struct QueryStatusCommand {
    quint32 sequence{0};
    ConnectionContext connection;
};

struct RecoverCommand {
    quint32 sequence{0};
    ConnectionContext connection;
};

struct ResultAck {
    QString taskId;
    ConnectionContext connection;
};

struct LocatePreparedReply {
    QString taskId;
    TcpLocateMode mode{TcpLocateMode::Edge};
    ConnectionContext connection;
};

struct PrepareRejectedReply {
    QString taskId;
    TcpErrorCode errorCode{TcpErrorCode::InternalFailure};
    QString message;
    std::optional<QString> activeTaskId;
    ConnectionContext connection;
};

struct LocateResultReply {
    QString taskId;
    TcpLocateMode mode{TcpLocateMode::Edge};
    LocateReplyOutcome outcome{LocateReplyOutcome::Error};
    double x{0.0};
    double y{0.0};
    double r{0.0};
    std::optional<TcpErrorCode> errorCode;
    QString message;
    ConnectionContext connection;
};

struct StatusReply {
    quint32 sequence{0};
    TcpApplicationState applicationState{TcpApplicationState::Starting};
    TcpVisionState visionState{TcpVisionState::Starting};
    TcpTaskState taskState{TcpTaskState::Idle};
    std::optional<QString> activeTaskId;
    std::optional<TcpErrorCode> lastErrorCode;
    ConnectionContext connection;
};

struct RecoverReply {
    quint32 sequence{0};
    bool success{false};
    std::optional<TcpErrorCode> errorCode;
    QString message;
    ConnectionContext connection;
};

[[nodiscard]] QString protocolName(TcpLocateMode mode);
[[nodiscard]] QString protocolName(TcpErrorCode code);
[[nodiscard]] QString protocolName(LocateReplyOutcome outcome);
[[nodiscard]] QString protocolName(TcpApplicationState state);
[[nodiscard]] QString protocolName(TcpVisionState state);
[[nodiscard]] QString protocolName(TcpTaskState state);

} // namespace vision::tcp

Q_DECLARE_METATYPE(vision::tcp::ConnectionContext)
Q_DECLARE_METATYPE(vision::tcp::PrepareLocateCommand)
Q_DECLARE_METATYPE(vision::tcp::QueryStatusCommand)
Q_DECLARE_METATYPE(vision::tcp::RecoverCommand)
Q_DECLARE_METATYPE(vision::tcp::ResultAck)
Q_DECLARE_METATYPE(vision::tcp::LocatePreparedReply)
Q_DECLARE_METATYPE(vision::tcp::PrepareRejectedReply)
Q_DECLARE_METATYPE(vision::tcp::LocateResultReply)
Q_DECLARE_METATYPE(vision::tcp::StatusReply)
Q_DECLARE_METATYPE(vision::tcp::RecoverReply)
