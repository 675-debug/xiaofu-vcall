#pragma once
#include <winsock2.h>
#include <functional>
#include <string>
#include <vector>

class Connection {
public:
    using MessageCallback = std::function<void(Connection*, const std::string&)>;
    using CloseCallback = std::function<void(Connection*)>;

    explicit Connection(SOCKET socketHandle);
    ~Connection();

    void setMessageCallback(MessageCallback callback) { messageCallback = callback; }
    void setCloseCallback(CloseCallback callback) { closeCallback = callback; }

    void onReadable();
    void sendMessage(const std::string& jsonPayload);
    void close();

    int fd() const { return static_cast<int>(socketHandle); }
    bool closed() const { return closedFlag; }

private:
    bool readMore();
    bool tryParseFrames();
    bool writeAll(const char* data, size_t length);

    SOCKET socketHandle;
    bool closedFlag;
    std::vector<char> inputBuffer;
    MessageCallback messageCallback;
    CloseCallback closeCallback;
};
