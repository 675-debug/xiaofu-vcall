#include "SocketPlatform.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

bool initializeSocketPlatform() {
#ifdef _WIN32
    WSADATA winsockData;
    return WSAStartup(MAKEWORD(2, 2), &winsockData) == 0;
#else
    return true;
#endif
}

void shutdownSocketPlatform() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void ignoreBrokenPipeSignal() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

bool setSocketNonBlocking(SocketHandle socketHandle) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socketHandle, FIONBIO, &mode) == 0;
#else
    const int currentFlags = fcntl(socketHandle, F_GETFL, 0);
    return currentFlags >= 0
        && fcntl(socketHandle, F_SETFL, currentFlags | O_NONBLOCK) == 0;
#endif
}

void closeSocket(SocketHandle socketHandle) {
#ifdef _WIN32
    closesocket(socketHandle);
#else
    ::close(socketHandle);
#endif
}

int lastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool isWouldBlockError(int errorCode) {
#ifdef _WIN32
    return errorCode == WSAEWOULDBLOCK;
#else
    return errorCode == EAGAIN || errorCode == EWOULDBLOCK;
#endif
}

bool isPeerClosedError(int errorCode) {
#ifdef _WIN32
    return errorCode == WSAECONNRESET || errorCode == WSAECONNABORTED;
#else
    return errorCode == ECONNRESET || errorCode == ECONNABORTED || errorCode == EPIPE;
#endif
}
