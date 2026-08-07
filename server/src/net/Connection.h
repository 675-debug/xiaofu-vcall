#pragma once
#include "SocketPlatform.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Connection {
public:
    using MessageCallback = std::function<void(Connection*, const std::string&)>;
    using CloseCallback = std::function<void(Connection*)>;

    explicit Connection(SocketHandle socketHandle, std::uint64_t connectionId = 0);
    ~Connection();

    void setMessageCallback(MessageCallback callback) { messageCallback = callback; }
    void setCloseCallback(CloseCallback callback) { closeCallback = callback; }

    void onReadable();
    void onWritable();
    void sendMessage(const std::string& jsonPayload);
    void close();

    int fd() const { return static_cast<int>(socketHandle); }
    std::uint64_t id() const { return connectionId; }
    bool closed() const { return closedFlag; }
    bool hasPendingOutput() const { return outputOffset < outputBuffer.size(); }

private:
    bool readMore();
    bool tryParseFrames();
    void compactOutputBuffer();

    SocketHandle socketHandle;
    std::uint64_t connectionId;
    bool closedFlag;
    std::vector<char> inputBuffer;
    std::vector<char> outputBuffer;
    std::size_t outputOffset = 0;
    MessageCallback messageCallback;
    CloseCallback closeCallback;
};
