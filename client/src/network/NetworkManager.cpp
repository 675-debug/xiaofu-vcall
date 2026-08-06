#include "NetworkManager.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

NetworkManager::NetworkManager(QObject* parent)
    : QObject(parent), tcpSocket(new QTcpSocket(this)) {
    connect(tcpSocket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(tcpSocket, &QTcpSocket::connected, this, &NetworkManager::onConnected);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
    // Qt 5.12: QOverload required for overloaded signal
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &NetworkManager::onError);
}

void NetworkManager::connectToServer(const QString& host, quint16 port) {
    tcpSocket->connectToHost(host, port);
}

// 为 JSON 数据添加 4 字节大端长度头。
void NetworkManager::sendFrame(const QByteArray& payload) {
    const quint32 payloadLength = static_cast<quint32>(payload.size());
    QByteArray frame;
    frame.append(static_cast<char>(payloadLength >> 24));
    frame.append(static_cast<char>(payloadLength >> 16));
    frame.append(static_cast<char>(payloadLength >> 8));
    frame.append(static_cast<char>(payloadLength));
    frame.append(payload);
    tcpSocket->write(frame);
}

void NetworkManager::sendRegister(const QString& username, const QString& email, const QString& password) {
    QJsonObject request;
    request["type"] = "register";
    request["username"] = username;
    request["email"] = email;
    request["password"] = password;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::sendLogin(const QString& username, const QString& password) {
    QJsonObject request;
    request["type"] = "login";
    request["username"] = username;
    request["password"] = password;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::sendForgot(const QString& username, const QString& newPassword) {
    QJsonObject request;
    request["type"] = "forgot";
    request["username"] = username;
    request["newPassword"] = newPassword;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

// 累积 TCP 字节流，再按长度头拆分完整消息。
void NetworkManager::onReadyRead() {
    receiveBuffer.append(tcpSocket->readAll());
    parseFrames();
}

void NetworkManager::parseFrames() {
    while (receiveBuffer.size() >= 4) {
        const quint32 payloadLength = (static_cast<quint32>(static_cast<quint8>(receiveBuffer[0])) << 24)
                                      | (static_cast<quint32>(static_cast<quint8>(receiveBuffer[1])) << 16)
                                      | (static_cast<quint32>(static_cast<quint8>(receiveBuffer[2])) << 8)
                                      | static_cast<quint32>(static_cast<quint8>(receiveBuffer[3]));
        if (payloadLength > 1024 * 1024) {  // 单条消息最多 1MB
            receiveBuffer.clear();
            return;
        }
        if (static_cast<quint32>(receiveBuffer.size()) < 4 + payloadLength)
            return;
        const QByteArray payload = receiveBuffer.mid(4, static_cast<int>(payloadLength));
        receiveBuffer.remove(0, 4 + static_cast<int>(payloadLength));
        dispatchResponse(payload);
    }
}

void NetworkManager::dispatchResponse(const QByteArray& payload) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const QJsonObject response = document.object();
    const QString type = response.value("type").toString();
    const int code = response.value("code").toInt(-1);
    const QString message = response.value("msg").toString();

    if (type == "register_resp") {
        emit registerResult(code, message);
    } else if (type == "login_resp") {
        const QString username = response.value("username").toString();
        emit loginResult(code, message, username);
    } else if (type == "forgot_resp") {
        emit forgotResult(code, message);
    }
}

void NetworkManager::onConnected() {
    emit connected();
}

void NetworkManager::onDisconnected() {
    emit disconnected();
}

void NetworkManager::onError(QAbstractSocket::SocketError error) {
    if (error == QAbstractSocket::ConnectionRefusedError) {
        qDebug() << "[Network] server not started, please start server first (服务端未启动，请先启动服务器)";
    } else {
        qDebug() << "[Network] connection error:" << tcpSocket->errorString();
    }
    emit connectionError(tcpSocket->errorString());
}
