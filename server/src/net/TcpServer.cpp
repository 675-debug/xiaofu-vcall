#include "TcpServer.h"
#include "EventLoop.h"
#include "../util/Log.h"
#include <ws2tcpip.h>

TcpServer::TcpServer(EventLoop* eventLoop)
    : eventLoop(eventLoop), listenSocket(INVALID_SOCKET) {}
TcpServer::~TcpServer() { if (listenSocket != INVALID_SOCKET) closesocket(listenSocket); }

bool TcpServer::listen(const char* ip, int port) {
    WSADATA winsockData;
    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0) {
        Log::error("WSAStartup failed");
        return false;
    }
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        Log::error(std::string("socket failed: ") + std::to_string(WSAGetLastError()));
        return false;
    }
    u_long mode = 1;
    ioctlsocket(listenSocket, FIONBIO, &mode);
    BOOL reuse = TRUE;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(ip);
    serverAddress.sin_port = htons(static_cast<u_short>(port));
    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        Log::error(std::string("bind failed: ") + std::to_string(WSAGetLastError()));
        return false;
    }
    if (::listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        Log::error(std::string("listen failed: ") + std::to_string(WSAGetLastError()));
        return false;
    }
    eventLoop->addFd(static_cast<int>(listenSocket),
                     [this](int) { onAccept(static_cast<int>(listenSocket)); });
    Log::info(std::string("listening on ") + ip + ":" + std::to_string(port));
    return true;
}

void TcpServer::onAccept(int) {
    while (true) {
        sockaddr_in peerAddress = {};
        int peerAddressLength = sizeof(peerAddress);
        const SOCKET clientSocket = accept(listenSocket,
                                           reinterpret_cast<sockaddr*>(&peerAddress),
                                           &peerAddressLength);
        if (clientSocket == INVALID_SOCKET) {
            const int socketError = WSAGetLastError();
            if (socketError != WSAEWOULDBLOCK)
                Log::error(std::string("accept failed: ") + std::to_string(socketError));
            break;
        }
        u_long mode = 1;
        ioctlsocket(clientSocket, FIONBIO, &mode);
        Log::info(std::string("client connected from ") + inet_ntoa(peerAddress.sin_addr) + ":"
                  + std::to_string(ntohs(peerAddress.sin_port))
                  + " (fd=" + std::to_string(static_cast<int>(clientSocket)) + ")");
        if (acceptCallback) acceptCallback(clientSocket);
    }
}
