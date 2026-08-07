#pragma once

#include "SocketPlatform.h"

#include <functional>

class EventLoop;

class TcpServer {
public:
    using AcceptCallback = std::function<void(SocketHandle)>;

    explicit TcpServer(EventLoop* eventLoop);
    ~TcpServer();

    bool listen(const char* ip, int port);
    void setAcceptCallback(AcceptCallback callback) { acceptCallback = std::move(callback); }

private:
    void onAccept();

    EventLoop* eventLoop;
    SocketHandle listenSocket;
    AcceptCallback acceptCallback;
};
