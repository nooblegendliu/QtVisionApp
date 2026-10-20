#pragma once

#include "VisionTcp/VisionTcpTypes.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace vision::tcp {

class VisionTcpServer final : public QObject {
    Q_OBJECT

public:
    explicit VisionTcpServer(
        TcpServerConfig config,
        QObject* parent = nullptr);
    ~VisionTcpServer() override;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool reconfigure(TcpServerConfig config);

    [[nodiscard]] bool isListening() const noexcept;
    [[nodiscard]] quint16 serverPort() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] std::optional<ConnectionContext>
        activeConnection() const noexcept;

    [[nodiscard]] bool sendLocatePrepared(
        const LocatePreparedReply& reply);
    [[nodiscard]] bool sendPrepareRejected(
        const PrepareRejectedReply& reply);
    [[nodiscard]] bool sendLocateResult(
        const LocateResultReply& reply);
    [[nodiscard]] bool sendStatus(const StatusReply& reply);
    [[nodiscard]] bool sendRecoverResult(const RecoverReply& reply);

    void disconnectClient(ConnectionContext connection);

signals:
    void prepareLocateRequested(
        const vision::tcp::PrepareLocateCommand& command);
    void queryStatusRequested(
        const vision::tcp::QueryStatusCommand& command);
    void recoverRequested(
        const vision::tcp::RecoverCommand& command);
    void resultAcknowledged(const vision::tcp::ResultAck& acknowledgement);
    void clientConnected(vision::tcp::ConnectionContext connection);
    void clientDisconnected(vision::tcp::ConnectionContext connection);
    void protocolMessage(const QString& message);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onHeartbeatTimeout();

private:
    void processLine(const QByteArray& line);
    void processObject(const class QJsonObject& object);
    void processHeartbeat(const class QJsonObject& object);
    void processPrepareLocate(const class QJsonObject& object);
    void processQueryStatus(const class QJsonObject& object);
    void processRecover(const class QJsonObject& object);
    void processResultAck(const class QJsonObject& object);

    [[nodiscard]] bool writeObject(
        const class QJsonObject& object,
        ConnectionContext connection);
    void writeProtocolError(
        TcpErrorCode code,
        const QString& message,
        bool disconnectAfterWrite = false);
    [[nodiscard]] bool isActive(ConnectionContext connection) const noexcept;
    void restartHeartbeatTimer();
    void releaseClient(QTcpSocket* socket);

    TcpServerConfig config_;
    QTcpServer* server_{nullptr};
    QTcpSocket* client_{nullptr};
    QTimer* heartbeatTimer_{nullptr};
    QByteArray receiveBuffer_;
    quint64 connectionGeneration_{0};
    QString lastError_;
};

} // namespace vision::tcp
