#pragma once
#include <winsock2.h>
#include <functional>

class EventLoop;

class TcpServer {
public:
    using AcceptCallback = std::function<void(SOCKET)>;

    explicit TcpServer(EventLoop* eventLoop);
    ~TcpServer();

    bool listen(const char* ip, int port);
    void setAcceptCallback(AcceptCallback callback) { acceptCallback = callback; }

private:
    void onAccept(int fd);

    EventLoop* eventLoop;
    SOCKET listenSocket;
    AcceptCallback acceptCallback;
};
