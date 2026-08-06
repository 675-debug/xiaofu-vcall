#include "Connection.h"
#include "../util/Log.h"
#include <cstdint>
#include <windows.h>

Connection::Connection(SOCKET socketHandle)
    : socketHandle(socketHandle), closedFlag(false) {}
Connection::~Connection() { if (!closedFlag) closesocket(socketHandle); }

void Connection::onReadable() {
    if (!readMore() || closedFlag) return;
    tryParseFrames();
}

bool Connection::readMore() {
    char readBuffer[4096];
    while (true) {
        const int receivedBytes = recv(socketHandle, readBuffer, sizeof(readBuffer), 0);
        if (receivedBytes > 0) {
            inputBuffer.insert(inputBuffer.end(), readBuffer, readBuffer + receivedBytes);
            continue;
        }
        if (receivedBytes == 0) { close(); return false; }
        const int socketError = WSAGetLastError();
        if (socketError == WSAEWOULDBLOCK) return true;
        if (socketError == WSAECONNRESET || socketError == WSAECONNABORTED) { close(); return false; }
        Log::error(std::string("recv error: ") + std::to_string(socketError));
        close();
        return false;
    }
}

bool Connection::tryParseFrames() {
    // 解析长度头并拆分完整消息。
    while (inputBuffer.size() >= 4) {
        const uint32_t payloadLength = (static_cast<uint32_t>(static_cast<uint8_t>(inputBuffer[0])) << 24)
                                       | (static_cast<uint32_t>(static_cast<uint8_t>(inputBuffer[1])) << 16)
                                       | (static_cast<uint32_t>(static_cast<uint8_t>(inputBuffer[2])) << 8)
                                       | static_cast<uint32_t>(static_cast<uint8_t>(inputBuffer[3]));
        if (payloadLength > 1024 * 1024) {
            Log::error("frame too large, closing");
            close();
            return false;
        }
        if (inputBuffer.size() < static_cast<size_t>(4) + payloadLength) return true;
        const std::string messageBody(inputBuffer.begin() + 4,
                                      inputBuffer.begin() + 4 + payloadLength);
        inputBuffer.erase(inputBuffer.begin(), inputBuffer.begin() + 4 + payloadLength);
        if (messageCallback) messageCallback(this, messageBody);
        if (closedFlag) return false;
    }
    return true;
}

void Connection::sendMessage(const std::string& jsonPayload) {
    if (closedFlag) return;
    std::vector<char> frame;
    const uint32_t payloadLength = static_cast<uint32_t>(jsonPayload.size());
    frame.push_back(static_cast<char>(payloadLength >> 24));
    frame.push_back(static_cast<char>(payloadLength >> 16));
    frame.push_back(static_cast<char>(payloadLength >> 8));
    frame.push_back(static_cast<char>(payloadLength));
    frame.insert(frame.end(), jsonPayload.begin(), jsonPayload.end());
    if (!writeAll(frame.data(), frame.size())) {
        Log::error("send failed, closing");
        close();
    }
}

bool Connection::writeAll(const char* data, size_t length) {
    size_t sentBytes = 0;
    while (sentBytes < length) {
        const int writtenBytes = send(socketHandle, data + sentBytes,
                                      static_cast<int>(length - sentBytes), 0);
        if (writtenBytes > 0) {
            sentBytes += static_cast<size_t>(writtenBytes);
            continue;
        }
        const int socketError = WSAGetLastError();
        if (socketError == WSAEWOULDBLOCK) { Sleep(1); continue; }  // v1 简化：小响应几乎不会触发
        return false;
    }
    return true;
}

void Connection::close() {
    if (closedFlag) return;
    closedFlag = true;
    closesocket(socketHandle);
    if (closeCallback) closeCallback(this);
}
