#pragma once
#include <functional>
#include <map>
#include <string>

class JoinHandler {
public:
    using KickCallback = std::function<void(int fd)>;

    explicit JoinHandler(KickCallback callback);

    // Try to join. If another connection already uses this username:
    //   kickExisting=true  → kick old fd via callback, then accept new
    //   kickExisting=false → reject (return false)
    // Returns true on success.
    bool handleJoin(int fd, const std::string& username, bool kickExisting = true);
    size_t onlineCount() const;
    void handleHeartbeat(int fd);
    void removeConnection(int fd);
    std::string usernameOf(int fd) const;
    int fdOf(const std::string& username) const;
    int kickTimeoutUsers(int timeoutMs);
    int fdOfUsername(const std::string& username) const;

private:
    struct Session {
        int fd;
        long long lastHeartbeatMs;
    };
    std::map<std::string, Session> sessions;
    std::map<int, std::string> usernamesByFd;
    KickCallback kickCallback;
};
