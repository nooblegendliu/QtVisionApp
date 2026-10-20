#include <VisionTcp/VisionTcpServer.h>

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cmath>
#include <limits>

namespace vision::tcp {
namespace {

constexpr auto kVersionKey = "version";
constexpr auto kTypeKey = "type";
constexpr auto kTaskIdKey = "taskId";
constexpr auto kSequenceKey = "sequence";

[[nodiscard]] QByteArray serialize(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

[[nodiscard]] std::optional<QString> validTaskId(
    const QJsonObject& object)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[1-9][0-9]{0,19}$"));
    const QJsonValue value = object.value(QLatin1String(kTaskIdKey));
    if (!value.isString()) {
        return std::nullopt;
    }
    const QString taskId = value.toString();
    if (!pattern.match(taskId).hasMatch()) {
        return std::nullopt;
    }
    return taskId;
}

[[nodiscard]] std::optional<quint32> validSequence(
    const QJsonObject& object)
{
    const QJsonValue value = object.value(QLatin1String(kSequenceKey));
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 ||
        number > static_cast<double>(std::numeric_limits<quint32>::max()) ||
        std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<quint32>(number);
}

[[nodiscard]] QJsonObject baseObject(QString type)
{
    return QJsonObject{
        {QLatin1String(kVersionKey), kProtocolVersion},
        {QLatin1String(kTypeKey), std::move(type)},
    };
}

} // namespace

QString protocolName(TcpLocateMode mode)
{
    switch (mode) {
    case TcpLocateMode::Edge: return QStringLiteral("EDGE");
    case TcpLocateMode::MarkPoint: return QStringLiteral("MARK_POINT");
    }
    return QStringLiteral("UNKNOWN");
}

QString protocolName(TcpErrorCode code)
{
    switch (code) {
    case TcpErrorCode::ProtocolError: return QStringLiteral("PROTOCOL_ERROR");
    case TcpErrorCode::UnsupportedVersion: return QStringLiteral("UNSUPPORTED_VERSION");
    case TcpErrorCode::MessageTooLarge: return QStringLiteral("MESSAGE_TOO_LARGE");
    case TcpErrorCode::SingleClientOnly: return QStringLiteral("SINGLE_CLIENT_ONLY");
    case TcpErrorCode::NotReady: return QStringLiteral("NOT_READY");
    case TcpErrorCode::Busy: return QStringLiteral("BUSY");
    case TcpErrorCode::InvalidConfiguration: return QStringLiteral("INVALID_CONFIGURATION");
    case TcpErrorCode::HardwareUnavailable: return QStringLiteral("HARDWARE_UNAVAILABLE");
    case TcpErrorCode::HardwareFailure: return QStringLiteral("HARDWARE_FAILURE");
    case TcpErrorCode::HardwareTimeout: return QStringLiteral("HARDWARE_TIMEOUT");
    case TcpErrorCode::InvalidImage: return QStringLiteral("INVALID_IMAGE");
    case TcpErrorCode::AlgorithmFailure: return QStringLiteral("ALGORITHM_FAILURE");
    case TcpErrorCode::AlgorithmTimeout: return QStringLiteral("ALGORITHM_TIMEOUT");
    case TcpErrorCode::InvalidResult: return QStringLiteral("INVALID_RESULT");
    case TcpErrorCode::InternalFailure: return QStringLiteral("INTERNAL_FAILURE");
    }
    return QStringLiteral("INTERNAL_FAILURE");
}

QString protocolName(LocateReplyOutcome outcome)
{
    switch (outcome) {
    case LocateReplyOutcome::Ok: return QStringLiteral("OK");
    case LocateReplyOutcome::Ng: return QStringLiteral("NG");
    case LocateReplyOutcome::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("ERROR");
}

QString protocolName(TcpApplicationState state)
{
    switch (state) {
    case TcpApplicationState::Starting: return QStringLiteral("STARTING");
    case TcpApplicationState::Ready: return QStringLiteral("READY");
    case TcpApplicationState::ShuttingDown: return QStringLiteral("SHUTTING_DOWN");
    case TcpApplicationState::Stopped: return QStringLiteral("STOPPED");
    }
    return QStringLiteral("STOPPED");
}

QString protocolName(TcpVisionState state)
{
    switch (state) {
    case TcpVisionState::Starting: return QStringLiteral("STARTING");
    case TcpVisionState::Ready: return QStringLiteral("READY");
    case TcpVisionState::Faulted: return QStringLiteral("FAULTED");
    case TcpVisionState::Recovering: return QStringLiteral("RECOVERING");
    case TcpVisionState::Stopped: return QStringLiteral("STOPPED");
    }
    return QStringLiteral("STOPPED");
}

QString protocolName(TcpTaskState state)
{
    switch (state) {
    case TcpTaskState::Idle: return QStringLiteral("IDLE");
    case TcpTaskState::Preparing: return QStringLiteral("PREPARING");
    case TcpTaskState::Running: return QStringLiteral("RUNNING");
    case TcpTaskState::AwaitingResultAck: return QStringLiteral("AWAITING_RESULT_ACK");
    }
    return QStringLiteral("IDLE");
}

VisionTcpServer::VisionTcpServer(TcpServerConfig config, QObject* parent)
    : QObject(parent),
      config_(std::move(config)),
      server_(new QTcpServer(this)),
      heartbeatTimer_(new QTimer(this))
{
    heartbeatTimer_->setSingleShot(true);
    connect(server_, &QTcpServer::newConnection,
            this, &VisionTcpServer::onNewConnection);
    connect(heartbeatTimer_, &QTimer::timeout,
            this, &VisionTcpServer::onHeartbeatTimeout);
}

VisionTcpServer::~VisionTcpServer()
{
    stop();
}

bool VisionTcpServer::start()
{
    if (server_->isListening()) {
        return true;
    }
    if (config_.listenAddress.isNull() || config_.port == 0 ||
        config_.heartbeatTimeout <= std::chrono::milliseconds::zero() ||
        config_.heartbeatTimeout > std::chrono::milliseconds{std::numeric_limits<int>::max()} ||
        config_.maximumFrameBytes < 256) {
        lastError_ = QStringLiteral("The TCP server configuration is invalid.");
        return false;
    }
    if (!server_->listen(config_.listenAddress, config_.port)) {
        lastError_ = server_->errorString();
        return false;
    }
    lastError_.clear();
    emit protocolMessage(
        QStringLiteral("[STATE] TCP server listening on %1:%2.")
            .arg(server_->serverAddress().toString())
            .arg(server_->serverPort()));
    return true;
}

void VisionTcpServer::stop()
{
    heartbeatTimer_->stop();
    receiveBuffer_.clear();
    if (server_->isListening()) {
        server_->close();
    }
    if (client_ != nullptr) {
        QTcpSocket* socket = client_;
        const ConnectionContext context{connectionGeneration_};
        client_ = nullptr;
        socket->disconnect(this);
        socket->disconnectFromHost();
        socket->deleteLater();
        emit clientDisconnected(context);
    }
}

bool VisionTcpServer::reconfigure(TcpServerConfig config)
{
    if (server_->isListening() || client_ != nullptr) {
        lastError_ = QStringLiteral(
            "The TCP server must be stopped before it is reconfigured.");
        return false;
    }
    if (config.listenAddress.isNull() || config.port == 0 ||
        config.heartbeatTimeout <= std::chrono::milliseconds::zero() ||
        config.heartbeatTimeout >
            std::chrono::milliseconds{std::numeric_limits<int>::max()} ||
        config.maximumFrameBytes < 256) {
        lastError_ = QStringLiteral("The TCP server configuration is invalid.");
        return false;
    }
    config_ = std::move(config);
    receiveBuffer_.clear();
    lastError_.clear();
    return true;
}

bool VisionTcpServer::isListening() const noexcept
{
    return server_->isListening();
}

quint16 VisionTcpServer::serverPort() const noexcept
{
    return server_->serverPort();
}

QString VisionTcpServer::lastError() const
{
    return lastError_;
}

std::optional<ConnectionContext> VisionTcpServer::activeConnection() const noexcept
{
    if (client_ == nullptr || client_->state() != QAbstractSocket::ConnectedState) {
        return std::nullopt;
    }
    return ConnectionContext{connectionGeneration_};
}

bool VisionTcpServer::sendLocatePrepared(const LocatePreparedReply& reply)
{
    QJsonObject object = baseObject(QStringLiteral("locatePrepared"));
    object.insert(QLatin1String(kTaskIdKey), reply.taskId);
    object.insert(QStringLiteral("mode"), protocolName(reply.mode));
    return writeObject(object, reply.connection);
}

bool VisionTcpServer::sendPrepareRejected(const PrepareRejectedReply& reply)
{
    QJsonObject object = baseObject(QStringLiteral("prepareRejected"));
    object.insert(QLatin1String(kTaskIdKey), reply.taskId);
    object.insert(QStringLiteral("errorCode"), protocolName(reply.errorCode));
    object.insert(QStringLiteral("message"), reply.message);
    if (reply.activeTaskId.has_value()) {
        object.insert(QStringLiteral("activeTaskId"), *reply.activeTaskId);
    }
    return writeObject(object, reply.connection);
}

bool VisionTcpServer::sendLocateResult(const LocateResultReply& reply)
{
    if (reply.outcome == LocateReplyOutcome::Error && !reply.errorCode.has_value()) {
        emit protocolMessage(QStringLiteral("[DROP] Invalid ERROR LocateResult without errorCode."));
        return false;
    }
    if (reply.outcome != LocateReplyOutcome::Error &&
        (!std::isfinite(reply.x) || !std::isfinite(reply.y) || !std::isfinite(reply.r))) {
        emit protocolMessage(QStringLiteral("[DROP] Invalid non-finite LocateResult coordinates."));
        return false;
    }

    QJsonObject object = baseObject(QStringLiteral("locateResult"));
    object.insert(QLatin1String(kTaskIdKey), reply.taskId);
    object.insert(QStringLiteral("mode"), protocolName(reply.mode));
    object.insert(QStringLiteral("outcome"), protocolName(reply.outcome));
    if (reply.outcome == LocateReplyOutcome::Error) {
        object.insert(QStringLiteral("errorCode"), protocolName(*reply.errorCode));
        object.insert(QStringLiteral("message"), reply.message);
    }
    else if (reply.outcome == LocateReplyOutcome::Ng) {
        object.insert(QStringLiteral("x"), 0.0);
        object.insert(QStringLiteral("y"), 0.0);
        object.insert(QStringLiteral("r"), 0.0);
    }
    else {
        object.insert(QStringLiteral("x"), reply.x);
        object.insert(QStringLiteral("y"), reply.y);
        object.insert(QStringLiteral("r"), reply.r);
    }
    return writeObject(object, reply.connection);
}

bool VisionTcpServer::sendStatus(const StatusReply& reply)
{
    QJsonObject object = baseObject(QStringLiteral("statusReply"));
    object.insert(QLatin1String(kSequenceKey), static_cast<qint64>(reply.sequence));
    object.insert(QStringLiteral("applicationState"), protocolName(reply.applicationState));
    object.insert(QStringLiteral("visionState"), protocolName(reply.visionState));
    object.insert(QStringLiteral("taskState"), protocolName(reply.taskState));
    object.insert(
        QStringLiteral("activeTaskId"),
        reply.activeTaskId.has_value() ? QJsonValue(*reply.activeTaskId) : QJsonValue::Null);
    object.insert(
        QStringLiteral("lastErrorCode"),
        reply.lastErrorCode.has_value()
            ? QJsonValue(protocolName(*reply.lastErrorCode))
            : QJsonValue::Null);
    return writeObject(object, reply.connection);
}

bool VisionTcpServer::sendRecoverResult(const RecoverReply& reply)
{
    QJsonObject object = baseObject(QStringLiteral("recoverResult"));
    object.insert(QLatin1String(kSequenceKey), static_cast<qint64>(reply.sequence));
    object.insert(QStringLiteral("outcome"), reply.success ? QStringLiteral("OK") : QStringLiteral("ERROR"));
    if (!reply.success) {
        object.insert(
            QStringLiteral("errorCode"),
            protocolName(reply.errorCode.value_or(TcpErrorCode::InternalFailure)));
        object.insert(QStringLiteral("message"), reply.message);
    }
    return writeObject(object, reply.connection);
}

void VisionTcpServer::disconnectClient(ConnectionContext connection)
{
    if (isActive(connection)) {
        client_->disconnectFromHost();
    }
}

void VisionTcpServer::onNewConnection()
{
    while (server_->hasPendingConnections()) {
        QTcpSocket* incoming = server_->nextPendingConnection();
        if (incoming == nullptr) {
            continue;
        }
        if (client_ != nullptr &&
            client_->state() != QAbstractSocket::UnconnectedState) {
            QJsonObject rejection = baseObject(QStringLiteral("protocolError"));
            rejection.insert(QStringLiteral("errorCode"), protocolName(TcpErrorCode::SingleClientOnly));
            rejection.insert(QStringLiteral("message"), QStringLiteral("The vision server already has an active client."));
            incoming->write(serialize(rejection));
            incoming->write("\n");
            connect(incoming, &QTcpSocket::disconnected,
                    incoming, &QObject::deleteLater);
            incoming->disconnectFromHost();
            if (incoming->state() == QAbstractSocket::UnconnectedState) {
                incoming->deleteLater();
            }
            continue;
        }

        client_ = incoming;
        receiveBuffer_.clear();
        ++connectionGeneration_;
        if (connectionGeneration_ == 0) {
            ++connectionGeneration_;
        }
        connect(client_, &QTcpSocket::readyRead,
                this, &VisionTcpServer::onReadyRead);
        connect(client_, &QTcpSocket::disconnected,
                this, &VisionTcpServer::onDisconnected);
        restartHeartbeatTimer();
        const ConnectionContext context{connectionGeneration_};
        emit clientConnected(context);
        emit protocolMessage(
            QStringLiteral("[STATE] TCP client connected (generation %1).")
                .arg(context.generation));
    }
}

void VisionTcpServer::onReadyRead()
{
    if (client_ == nullptr) {
        return;
    }
    receiveBuffer_.append(client_->readAll());

    qsizetype newline = receiveBuffer_.indexOf('\n');
    while (newline >= 0) {
        if (newline > config_.maximumFrameBytes) {
            receiveBuffer_.clear();
            writeProtocolError(
                TcpErrorCode::MessageTooLarge,
                QStringLiteral("The JSON message exceeds the configured frame limit."),
                true);
            return;
        }
        QByteArray line = receiveBuffer_.left(newline);
        receiveBuffer_.remove(0, newline + 1);
        if (!line.isEmpty() && line.back() == '\r') {
            line.chop(1);
        }
        if (!line.trimmed().isEmpty()) {
            emit protocolMessage(QStringLiteral("[RX] %1").arg(QString::fromUtf8(line)));
            processLine(line);
        }
        if (client_ == nullptr) {
            return;
        }
        newline = receiveBuffer_.indexOf('\n');
    }

    if (receiveBuffer_.size() > config_.maximumFrameBytes) {
        receiveBuffer_.clear();
        writeProtocolError(
            TcpErrorCode::MessageTooLarge,
            QStringLiteral("The JSON message exceeds the configured frame limit."),
            true);
    }
}

void VisionTcpServer::onDisconnected()
{
    releaseClient(qobject_cast<QTcpSocket*>(sender()));
}

void VisionTcpServer::onHeartbeatTimeout()
{
    if (client_ == nullptr) {
        return;
    }
    emit protocolMessage(QStringLiteral("[STATE] Heartbeat timed out; closing the TCP session."));
    client_->disconnectFromHost();
}

void VisionTcpServer::processLine(const QByteArray& line)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    static_cast<void>(decoder.decode(line));
    if (decoder.hasError()) {
        writeProtocolError(
            TcpErrorCode::ProtocolError,
            QStringLiteral("The message is not valid UTF-8."));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        writeProtocolError(
            TcpErrorCode::ProtocolError,
            QStringLiteral("Expected one JSON object terminated by a newline."));
        return;
    }
    processObject(document.object());
}

void VisionTcpServer::processObject(const QJsonObject& object)
{
    const QJsonValue versionValue =
        object.value(QLatin1String(kVersionKey));
    if (!versionValue.isDouble()) {
        writeProtocolError(
            TcpErrorCode::ProtocolError,
            QStringLiteral("version must be the integer 1."));
        return;
    }
    if (versionValue.toDouble() != kProtocolVersion) {
        writeProtocolError(
            TcpErrorCode::UnsupportedVersion,
            QStringLiteral("Only protocol version 1 is supported."),
            true);
        return;
    }
    const QJsonValue typeValue = object.value(QLatin1String(kTypeKey));
    if (!typeValue.isString()) {
        writeProtocolError(TcpErrorCode::ProtocolError, QStringLiteral("type must be a string."));
        return;
    }
    const QString type = typeValue.toString();
    if (type == QStringLiteral("heartbeat")) {
        processHeartbeat(object);
    }
    else if (type == QStringLiteral("prepareLocate")) {
        processPrepareLocate(object);
    }
    else if (type == QStringLiteral("queryStatus")) {
        processQueryStatus(object);
    }
    else if (type == QStringLiteral("recover")) {
        processRecover(object);
    }
    else if (type == QStringLiteral("resultAck")) {
        processResultAck(object);
    }
    else {
        writeProtocolError(TcpErrorCode::ProtocolError, QStringLiteral("The message type is unsupported."));
    }
}

void VisionTcpServer::processHeartbeat(const QJsonObject& object)
{
    const auto sequence = validSequence(object);
    if (!sequence.has_value()) {
        writeProtocolError(TcpErrorCode::ProtocolError, QStringLiteral("heartbeat.sequence must be a uint32 integer."));
        return;
    }
    restartHeartbeatTimer();
    QJsonObject reply = baseObject(QStringLiteral("heartbeatAck"));
    reply.insert(QLatin1String(kSequenceKey), static_cast<qint64>(*sequence));
    static_cast<void>(writeObject(reply, ConnectionContext{connectionGeneration_}));
}

void VisionTcpServer::processPrepareLocate(const QJsonObject& object)
{
    const auto taskId = validTaskId(object);
    const QJsonValue modeValue = object.value(QStringLiteral("mode"));
    if (!taskId.has_value() || !modeValue.isString()) {
        writeProtocolError(TcpErrorCode::ProtocolError, QStringLiteral("prepareLocate requires a decimal taskId and mode."));
        return;
    }
    TcpLocateMode mode;
    if (modeValue.toString() == QStringLiteral("EDGE")) {
        mode = TcpLocateMode::Edge;
    }
    else if (modeValue.toString() == QStringLiteral("MARK_POINT")) {
        mode = TcpLocateMode::MarkPoint;
    }
    else {
        writeProtocolError(TcpErrorCode::ProtocolError, QStringLiteral("mode must be EDGE or MARK_POINT."));
        return;
    }
    emit prepareLocateRequested(PrepareLocateCommand{
        *taskId, mode, ConnectionContext{connectionGeneration_}});
}

void VisionTcpServer::processQueryStatus(const QJsonObject& object)
{
    const auto sequence = validSequence(object);
    if (!sequence.has_value()) {
        writeProtocolError(TcpErrorCode::ProtocolError, QStringLiteral("queryStatus.sequence must be a uint32 integer."));
        return;
    }
    emit queryStatusRequested(QueryStatusCommand{
        *sequence, ConnectionContext{connectionGeneration_}});
}

void VisionTcpServer::processRecover(const QJsonObject& object)
{
    const auto sequence = validSequence(object);
    if (!sequence.has_value()) {
        writeProtocolError(TcpErrorCode::ProtocolError, QStringLiteral("recover.sequence must be a uint32 integer."));
        return;
    }
    emit recoverRequested(RecoverCommand{
        *sequence, ConnectionContext{connectionGeneration_}});
}

void VisionTcpServer::processResultAck(const QJsonObject& object)
{
    const auto taskId = validTaskId(object);
    if (!taskId.has_value()) {
        writeProtocolError(TcpErrorCode::ProtocolError, QStringLiteral("resultAck.taskId must be a non-zero decimal string."));
        return;
    }
    emit resultAcknowledged(ResultAck{
        *taskId, ConnectionContext{connectionGeneration_}});
}

bool VisionTcpServer::writeObject(
    const QJsonObject& object,
    ConnectionContext connection)
{
    if (!isActive(connection)) {
        emit protocolMessage(
            QStringLiteral("[DROP] Outgoing message belongs to an inactive TCP generation."));
        return false;
    }
    const QByteArray bytes = serialize(object);
    if (client_->write(bytes) < 0 || client_->write("\n") < 0) {
        lastError_ = client_->errorString();
        return false;
    }
    emit protocolMessage(QStringLiteral("[TX] %1").arg(QString::fromUtf8(bytes)));
    return true;
}

void VisionTcpServer::writeProtocolError(
    TcpErrorCode code,
    const QString& message,
    bool disconnectAfterWrite)
{
    emit protocolMessage(QStringLiteral("[PROTO ERROR] %1").arg(message));
    if (client_ == nullptr) {
        return;
    }
    QJsonObject object = baseObject(QStringLiteral("protocolError"));
    object.insert(QStringLiteral("errorCode"), protocolName(code));
    object.insert(QStringLiteral("message"), message);
    static_cast<void>(writeObject(object, ConnectionContext{connectionGeneration_}));
    if (disconnectAfterWrite && client_ != nullptr) {
        client_->disconnectFromHost();
    }
}

bool VisionTcpServer::isActive(ConnectionContext connection) const noexcept
{
    return connection.isValid() && connection.generation == connectionGeneration_ &&
        client_ != nullptr && client_->state() == QAbstractSocket::ConnectedState;
}

void VisionTcpServer::restartHeartbeatTimer()
{
    heartbeatTimer_->start(static_cast<int>(config_.heartbeatTimeout.count()));
}

void VisionTcpServer::releaseClient(QTcpSocket* socket)
{
    if (socket == nullptr) {
        return;
    }
    if (socket != client_) {
        socket->deleteLater();
        return;
    }
    const ConnectionContext context{connectionGeneration_};
    heartbeatTimer_->stop();
    receiveBuffer_.clear();
    client_ = nullptr;
    socket->deleteLater();
    emit clientDisconnected(context);
    emit protocolMessage(
        QStringLiteral("[STATE] TCP client disconnected (generation %1).")
            .arg(context.generation));
}

} // namespace vision::tcp
