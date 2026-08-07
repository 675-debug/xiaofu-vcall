#include "Connection.h"
#include "../util/Log.h"
#include <cstdint>

Connection::Connection(SocketHandle socketHandle, std::uint64_t connectionId)
    : socketHandle(socketHandle), connectionId(connectionId), closedFlag(false) {}

Connection::~Connection() {
    if (!closedFlag)
        closeSocket(socketHandle);
}

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
        const int socketError = lastSocketError();
        if (isWouldBlockError(socketError)) return true;
        if (isPeerClosedError(socketError)) { close(); return false; }
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
    compactOutputBuffer();
    const uint32_t payloadLength = static_cast<uint32_t>(jsonPayload.size());
    outputBuffer.push_back(static_cast<char>(payloadLength >> 24));
    outputBuffer.push_back(static_cast<char>(payloadLength >> 16));
    outputBuffer.push_back(static_cast<char>(payloadLength >> 8));
    outputBuffer.push_back(static_cast<char>(payloadLength));
    outputBuffer.insert(outputBuffer.end(), jsonPayload.begin(), jsonPayload.end());
    onWritable();
}

void Connection::onWritable() {
    while (!closedFlag && hasPendingOutput()) {
#ifdef _WIN32
        const int sendFlags = 0;
#else
        const int sendFlags = MSG_NOSIGNAL;
#endif
        const int writtenBytes = send(socketHandle, outputBuffer.data() + outputOffset,
                                      static_cast<int>(outputBuffer.size() - outputOffset),
                                      sendFlags);
        if (writtenBytes > 0) {
            outputOffset += static_cast<std::size_t>(writtenBytes);
            continue;
        }
        const int socketError = lastSocketError();
        if (isWouldBlockError(socketError))
            return;
        Log::error("send failed, closing: " + std::to_string(socketError));
        close();
        return;
    }
    compactOutputBuffer();
}

void Connection::compactOutputBuffer() {
    if (outputOffset == 0)
        return;
    if (outputOffset >= outputBuffer.size()) {
        outputBuffer.clear();
        outputOffset = 0;
        return;
    }
    outputBuffer.erase(outputBuffer.begin(), outputBuffer.begin() + outputOffset);
    outputOffset = 0;
}

void Connection::close() {
    if (closedFlag) return;
    closedFlag = true;
    closeSocket(socketHandle);
    if (closeCallback) closeCallback(this);
}
