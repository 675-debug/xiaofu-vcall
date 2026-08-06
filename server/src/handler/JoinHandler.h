#pragma once
#include <functional>
#include <map>
#include <string>

class JoinHandler {
public:
    using KickCallback = std::function<void(int fd)>;

    explicit JoinHandler(KickCallback callback);

    bool handleJoin(int fd, const std::string& username);
    size_t onlineCount() const;
    void handleHeartbeat(int fd);
    void removeConnection(int fd);
    std::string usernameOf(int fd) const;
    int kickTimeoutUsers(int timeoutMs);

private:
    struct Session {
        int fd;
        long long lastHeartbeatMs;
    };
    std::map<std::string, Session> sessions;
    std::map<int, std::string> usernamesByFd;
    KickCallback kickCallback;
};
