#pragma once
#include <QObject>
#include <QString>
#include <QTcpSocket>

class NetworkManager : public QObject {
    Q_OBJECT
public:
    explicit NetworkManager(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port);

    void sendRegister(const QString& username, const QString& email, const QString& password);
    void sendLogin(const QString& username, const QString& password);
    void sendForgot(const QString& username, const QString& newPassword);

signals:
    void connected();
    void disconnected();
    void registerResult(int code, const QString& message);
    void loginResult(int code, const QString& message, const QString& username);
    void forgotResult(int code, const QString& message);
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
    QByteArray receiveBuffer;
};
