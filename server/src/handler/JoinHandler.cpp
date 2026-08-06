#include "JoinHandler.h"
#include <chrono>
#include <vector>

namespace {
long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

JoinHandler::JoinHandler(KickCallback callback) : kickCallback(callback) {}

bool JoinHandler::handleJoin(int fd, const std::string& username) {
    if (username.empty() || sessions.count(username)) return false;
    Session session;
    session.fd = fd;
    session.lastHeartbeatMs = nowMs();
    sessions[username] = session;
    usernamesByFd[fd] = username;
    return true;
}

void JoinHandler::handleHeartbeat(int fd) {
    const auto userIterator = usernamesByFd.find(fd);
    if (userIterator == usernamesByFd.end()) return;
    const auto sessionIterator = sessions.find(userIterator->second);
    if (sessionIterator != sessions.end())
        sessionIterator->second.lastHeartbeatMs = nowMs();
}

void JoinHandler::removeConnection(int fd) {
    const auto userIterator = usernamesByFd.find(fd);
    if (userIterator == usernamesByFd.end()) return;
    sessions.erase(userIterator->second);
    usernamesByFd.erase(userIterator);
}

size_t JoinHandler::onlineCount() const { return sessions.size(); }

std::string JoinHandler::usernameOf(int fd) const {
    const auto userIterator = usernamesByFd.find(fd);
    return userIterator == usernamesByFd.end() ? std::string() : userIterator->second;
}

int JoinHandler::kickTimeoutUsers(int timeoutMs) {
    const long long currentTimeMs = nowMs();
    std::vector<int> socketsToKick;
    for (const auto& entry : sessions) {
        if (currentTimeMs - entry.second.lastHeartbeatMs > timeoutMs)
            socketsToKick.push_back(entry.second.fd);
    }
    for (int fd : socketsToKick) {
        removeConnection(fd);  // 先清表，再回调关连接（回调会触发 removeConnection，必须幂等）
        if (kickCallback) kickCallback(fd);
    }
    return static_cast<int>(socketsToKick.size());
}
