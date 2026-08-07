#include "TcpServer.h"

#include "EventLoop.h"
#include "../util/Log.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <cstring>
#include <string>

TcpServer::TcpServer(EventLoop* eventLoop)
    : eventLoop(eventLoop), listenSocket(kInvalidSocket)
{
}

TcpServer::~TcpServer()
{
    if (listenSocket != kInvalidSocket) {
        eventLoop->removeFd(static_cast<int>(listenSocket));
        closeSocket(listenSocket);
    }
}

bool TcpServer::listen(const char* ip, int port)
{
    if (!initializeSocketPlatform()) {
        Log::error("socket platform initialization failed");
        return false;
    }
    ignoreBrokenPipeSignal();

    listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == kInvalidSocket) {
        Log::error("socket failed: " + std::to_string(lastSocketError()));
        return false;
    }
    if (!setSocketNonBlocking(listenSocket)) {
        Log::error("set nonblocking failed: " + std::to_string(lastSocketError()));
        return false;
    }

    const int reuseAddress = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(static_cast<unsigned short>(port));
    if (inet_pton(AF_INET, ip, &serverAddress.sin_addr) != 1) {
        Log::error("invalid listen address: " + std::string(ip));
        return false;
    }
    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddress),
               sizeof(serverAddress)) != 0) {
        Log::error("bind failed: " + std::to_string(lastSocketError()));
        return false;
    }
    if (::listen(listenSocket, SOMAXCONN) != 0) {
        Log::error("listen failed: " + std::to_string(lastSocketError()));
        return false;
    }

    const int listenFd = static_cast<int>(listenSocket);
    if (!eventLoop->addFd(listenFd, EventLoop::Read,
                          [this](std::uint32_t events) {
        if ((events & EventLoop::Read) != 0U)
            onAccept();
        if ((events & EventLoop::Error) != 0U)
            Log::error("listen socket reported an error");
    })) {
        Log::error("register listen socket in epoll failed");
        return false;
    }

    Log::info(std::string("listening on ") + ip + ":" + std::to_string(port));
    return true;
}

void TcpServer::onAccept()
{
    // ET 模式下必须持续 accept，直到 EAGAIN，才能取完本轮全部新连接。
    while (true) {
        sockaddr_in peerAddress{};
#ifdef _WIN32
        int peerAddressLength = sizeof(peerAddress);
#else
        socklen_t peerAddressLength = sizeof(peerAddress);
#endif
        const SocketHandle clientSocket = ::accept(
            listenSocket, reinterpret_cast<sockaddr*>(&peerAddress), &peerAddressLength);
        if (clientSocket == kInvalidSocket) {
            const int errorCode = lastSocketError();
            if (!isWouldBlockError(errorCode))
                Log::error("accept failed: " + std::to_string(errorCode));
            break;
        }

        if (!setSocketNonBlocking(clientSocket)) {
            Log::error("set client nonblocking failed: " + std::to_string(lastSocketError()));
            closeSocket(clientSocket);
            continue;
        }

        char peerIp[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &peerAddress.sin_addr, peerIp, sizeof(peerIp));
        Log::info(std::string("client connected from ") + peerIp + ":"
                  + std::to_string(ntohs(peerAddress.sin_port))
                  + " (fd=" + std::to_string(static_cast<int>(clientSocket)) + ")");
        if (acceptCallback)
            acceptCallback(clientSocket);
        else
            closeSocket(clientSocket);
    }
}
