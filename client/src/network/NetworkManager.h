#pragma once
#include <QObject>
#include <QJsonArray>
#include <QString>
#include <QTcpSocket>

class QTimer;

class NetworkManager : public QObject {
    Q_OBJECT
public:
    explicit NetworkManager(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port);

    void sendRegister(const QString& username, const QString& email, const QString& password,
                      const QString& nickname = QString(), int avatarSeed = 0);
    void sendLogin(const QString& username, const QString& password);
    void sendForgot(const QString& username, const QString& newPassword);
    void sendJoin(const QString& username);
    void sendHeartbeat();
    void logout();
    void sendChat(const QString& receiver, const QString& content);
    void requestHistory(const QString& peer);
    void deleteConversation(const QString& peer);
    void clearAllChats();
    void requestContacts();
    void addContact(const QString& username);
    void requestFriendRequests();
    void respondToFriendRequest(const QString& sender, bool accepted);
    QString currentUsername() const;
    bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void registerResult(int code, const QString& message);
    void loginResult(int code, const QString& message, const QString& username);
    void forgotResult(int code, const QString& message);
    void joinResult(int code, const QString& message);
    void chatReceived(const QString& sender, const QString& content, const QString& sentAt);
    void historyReceived(const QString& peer, const QJsonArray& messages);
    void conversationDeleted(const QString& peer, int code, const QString& message);
    void allChatsCleared(int code, const QString& message);
    void contactsReceived(const QJsonArray& contacts);
    void presenceChanged(const QString& username, bool online);
    void chatSendResult(const QString& peer, bool online, int code, const QString& message);
    void friendRequestReceived(const QString& sender, const QString& nickname, int avatarSeed);
    void connectionError(const QString& errorMessage);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);

private:
    void sendFrame(const QByteArray& payload);
    void parseFrames();
    void dispatchResponse(const QByteArray& payload);

    QTcpSocket* tcpSocket;
    QTimer* heartbeatTimer;
    QByteArray receiveBuffer;
    QString loggedInUsername;
};
