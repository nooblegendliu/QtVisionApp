#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>

#include "TestSupport/QtEventLoopTestSupport.h"

namespace vision::test::support {

inline quint16 reserveLocalTcpPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

class JsonLineClient final {
public:
    JsonLineClient() {
        QObject::connect(&socket_, &QTcpSocket::readyRead, [&] {
            received_.append(socket_.readAll());
        });
    }

    JsonLineClient(const JsonLineClient&) = delete;
    JsonLineClient& operator=(const JsonLineClient&) = delete;

    QTcpSocket& socket() noexcept {
        return socket_;
    }

    const QByteArray& receivedBytes() const noexcept {
        return received_;
    }

    void connectToLocalHost(quint16 port) {
        socket_.connectToHost(QHostAddress::LocalHost, port);
    }

    void sendBytes(const QByteArray& bytes) {
        socket_.write(bytes);
        socket_.flush();
    }

    void sendObject(const QJsonObject& object, bool useCrLf = false) {
        QByteArray frame = QJsonDocument(object).toJson(QJsonDocument::Compact);
        frame.append(useCrLf ? "\r\n" : "\n");
        sendBytes(frame);
    }

    std::optional<QByteArray> takeLine(
        std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
        if (!processEventsUntil([&] { return received_.indexOf('\n') >= 0; }, timeout)) {
            return std::nullopt;
        }

        const qsizetype newline = received_.indexOf('\n');
        QByteArray line = received_.left(newline);
        received_.remove(0, newline + 1);
        if (!line.isEmpty() && line.back() == '\r') {
            line.chop(1);
        }
        return line;
    }

    std::optional<QJsonObject> takeObject(
        std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
        const auto line = takeLine(timeout);
        if (!line) {
            return std::nullopt;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(*line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return std::nullopt;
        }
        return document.object();
    }

    std::vector<QJsonObject> takeAvailableObjects() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::vector<QJsonObject> objects;
        qsizetype newline = received_.indexOf('\n');
        while (newline >= 0) {
            QByteArray line = received_.left(newline);
            received_.remove(0, newline + 1);
            if (!line.isEmpty() && line.back() == '\r') {
                line.chop(1);
            }
            QJsonParseError error;
            const QJsonDocument document =
                QJsonDocument::fromJson(line, &error);
            if (error.error == QJsonParseError::NoError &&
                document.isObject()) {
                objects.push_back(document.object());
            }
            newline = received_.indexOf('\n');
        }
        return objects;
    }

    void clearReceived() noexcept {
        received_.clear();
    }

private:
    QTcpSocket socket_;
    QByteArray received_;
};

} // namespace vision::test::support
