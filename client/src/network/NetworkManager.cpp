#include "NetworkManager.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

NetworkManager::NetworkManager(QObject* parent)
    : QObject(parent), tcpSocket(new QTcpSocket(this)), heartbeatTimer(new QTimer(this)) {
    connect(tcpSocket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(tcpSocket, &QTcpSocket::connected, this, &NetworkManager::onConnected);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
    // Qt 5.12: QOverload required for overloaded signal
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(tcpSocket, &QTcpSocket::errorOccurred, this, &NetworkManager::onError);
#else
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &NetworkManager::onError);
#endif
    heartbeatTimer->setInterval(15000);
    connect(heartbeatTimer, &QTimer::timeout, this, &NetworkManager::sendHeartbeat);
}

void NetworkManager::connectToServer(const QString& host, quint16 port) {
    qDebug() << "[Network] connecting:" << host << port;
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

void NetworkManager::sendRegister(const QString& username, const QString& email, const QString& password,
                                  const QString& nickname, int avatarSeed) {
    QJsonObject request;
    request["type"] = "register";
    request["username"] = username;
    request["email"] = email;
    request["password"] = password;
    request["nickname"] = nickname;
    request["avatarSeed"] = avatarSeed;
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

void NetworkManager::sendJoin(const QString& username) {
    qDebug() << "[Network] join request:" << username;
    QJsonObject request;
    request["type"] = "join";
    request["username"] = username;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::sendHeartbeat() {
    if (loggedInUsername.isEmpty() || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;
    QJsonObject request;
    request["type"] = "heartbeat";
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
    qDebug() << "[Network] heartbeat sent:" << loggedInUsername;
}

void NetworkManager::logout() {
    heartbeatTimer->stop();
    if (loggedInUsername.isEmpty())
        return;

    // 保持 TCP 连接，发送 leave 后可在同一连接中登录其他账号。
    if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject request;
        request["type"] = "leave";
        sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
        qDebug() << "[Network] leave request:" << loggedInUsername;
    }
    loggedInUsername.clear();
}

void NetworkManager::sendChat(const QString& receiver, const QString& content) {
    qDebug() << "[Network] chat request: receiver=" << receiver << "characters=" << content.size();
    QJsonObject request;
    request["type"] = "chat";
    request["to"] = receiver;
    request["content"] = content;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::requestHistory(const QString& peer) {
    qDebug() << "[Network] history request:" << peer;
    QJsonObject request;
    request["type"] = "history";
    request["peer"] = peer;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::deleteConversation(const QString& peer) {
    QJsonObject request;
    request["type"] = "delete_chat";
    request["peer"] = peer;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::clearAllChats() {
    QJsonObject request;
    request["type"] = "clear_chats";
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::requestContacts() {
    qDebug() << "[Network] contacts request";
    QJsonObject request;
    request["type"] = "contacts";
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::addContact(const QString& username) {
    if (loggedInUsername.isEmpty() || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "[Network] add contact ignored: not connected or not logged in";
        return;
    }
    qDebug() << "[Network] friend request:" << username;
    QJsonObject request;
    request["type"] = "friend_request";
    request["username"] = username;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::requestFriendRequests() {
    qDebug() << "[Network] pending friend requests";
    QJsonObject request;
    request["type"] = "friend_requests";
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void NetworkManager::respondToFriendRequest(const QString& sender, bool accepted) {
    QJsonObject request;
    request["type"] = "friend_request_response";
    request["sender"] = sender;
    request["accepted"] = accepted;
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
    qDebug() << "[Network] friend request response:" << sender << accepted;
}

void NetworkManager::sendCallSignal(const QVariantMap& signal) {
    if (loggedInUsername.isEmpty() || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "[Network] call signal ignored: not connected or not logged in";
        return;
    }

    const QJsonObject request = QJsonObject::fromVariantMap(signal);
    qDebug() << "[Network] call signal:" << request.value("type").toString()
             << "to=" << request.value("to").toString();
    sendFrame(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

QString NetworkManager::currentUsername() const {
    return loggedInUsername;
}

bool NetworkManager::isConnected() const {
    return tcpSocket->state() == QAbstractSocket::ConnectedState;
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
    qDebug() << "[Network] response:" << type << "code=" << code;

    if (type == "register_resp") {
        emit registerResult(code, message);
    } else if (type == "login_resp") {
        const QString username = response.value("username").toString();
        if (code == 0 && !username.isEmpty()) {
            loggedInUsername = username;
            qDebug() << "[Network] login success:" << loggedInUsername;
            sendJoin(loggedInUsername);
        }
        emit loginResult(code, message, username);
    } else if (type == "forgot_resp") {
        emit forgotResult(code, message);
    } else if (type == "join_resp") {
        if (code == 0) {
            sendHeartbeat();
            heartbeatTimer->start();
            requestContacts();
            requestFriendRequests();
        }
        emit joinResult(code, message);
    } else if (type == "leave_resp") {
        qDebug() << "[Network] leave response:" << code;
    } else if (type == "presence_push") {
        emit presenceChanged(response.value("username").toString(), response.value("online").toBool());
    } else if (type == "contacts_resp") {
        qDebug() << "[Network] contacts received:" << response.value("contacts").toArray().size();
        emit contactsReceived(response.value("contacts").toArray());
    } else if (type == "friend_request_resp") {
        qDebug() << "[Network] friend request result:" << code << message;
    } else if (type == "friend_request_push") {
        const QJsonObject request = response.value("request").toObject();
        emit friendRequestReceived(request.value("sender").toString(),
                                   request.value("nickname").toString(),
                                   request.value("avatarSeed").toInt());
    } else if (type == "friend_requests_resp") {
        const QJsonArray requests = response.value("requests").toArray();
        qDebug() << "[Network] pending friend requests received:" << requests.size();
        for (const QJsonValue& value : requests) {
            const QJsonObject request = value.toObject();
            emit friendRequestReceived(request.value("sender").toString(),
                                       request.value("nickname").toString(),
                                       request.value("avatarSeed").toInt());
        }
    } else if (type == "friend_request_response_resp") {
        if (code == 0)
            requestContacts();
    } else if (type == "friend_accepted_push") {
        qDebug() << "[Network] friend request accepted by:" << response.value("username").toString();
        requestContacts();
    } else if (type == "call_signal_resp") {
        emit callSignalSendResult(response.value("signalType").toString(), code, message);
    } else if (type == "call_request" || type == "call_accept" || type == "call_reject"
               || type == "call_hangup" || type == "call_cancel"
               || type == "peer_disconnected" || type == "call_ended"
               || type == "webrtc_offer" || type == "webrtc_answer"
               || type == "ice_candidate") {
        qDebug() << "[Network] call signal received:" << type
                 << "from=" << response.value("from").toString();
        emit callSignalReceived(response.toVariantMap());
    } else if (type == "chat_resp") {
        const QJsonObject chatMessage = response.value("message").toObject();
        emit chatSendResult(chatMessage.value("to").toString(), response.value("online").toBool(), code, message);
    } else if (type == "chat_push") {
        const QJsonObject chatMessage = response.value("message").toObject();
        emit chatReceived(chatMessage.value("from").toString(),
                          chatMessage.value("content").toString(),
                          chatMessage.value("sentAt").toString());
    } else if (type == "history_resp") {
        emit historyReceived(response.value("peer").toString(),
                             response.value("messages").toArray());
    } else if (type == "delete_chat_resp") {
        emit conversationDeleted(response.value("peer").toString(), code, message);
    } else if (type == "clear_chats_resp") {
        emit allChatsCleared(code, message);
    }
}

void NetworkManager::onConnected() {
    qDebug() << "[Network] connected to server";
    if (!loggedInUsername.isEmpty())
        sendJoin(loggedInUsername);
    emit connected();
}

void NetworkManager::onDisconnected() {
    heartbeatTimer->stop();
    qDebug() << "[Network] disconnected from server";
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
