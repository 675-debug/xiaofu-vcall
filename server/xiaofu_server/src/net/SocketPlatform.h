#pragma once

#ifdef _WIN32
#include <winsock2.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

bool initializeSocketPlatform();
void shutdownSocketPlatform();
void ignoreBrokenPipeSignal();
bool setSocketNonBlocking(SocketHandle socketHandle);
void closeSocket(SocketHandle socketHandle);
int lastSocketError();
bool isWouldBlockError(int errorCode);
bool isPeerClosedError(int errorCode);
