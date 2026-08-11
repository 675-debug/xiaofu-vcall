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

bool JoinHandler::handleJoin(int fd, const std::string& username, bool kickExisting) {
    if (username.empty()) return false;

    // same fd already bound to a username → reject
    if (usernamesByFd.count(fd)) return false;

    // username already online → kick or reject
    auto existingIt = sessions.find(username);
    if (existingIt != sessions.end()) {
        if (!kickExisting) return false;
        // kick old connection
        const int oldFd = existingIt->second.fd;
        sessions.erase(existingIt);
        usernamesByFd.erase(oldFd);
        if (kickCallback) kickCallback(oldFd);
    }

    Session session;
    session.fd = fd;
    session.lastHeartbeatMs = nowMs();
    sessions[username] = session;
    usernamesByFd[fd] = username;
    return true;
}

size_t JoinHandler::onlineCount() const { return sessions.size(); }

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

std::string JoinHandler::usernameOf(int fd) const {
    const auto userIterator = usernamesByFd.find(fd);
    return userIterator == usernamesByFd.end() ? std::string() : userIterator->second;
}

int JoinHandler::fdOf(const std::string& username) const {
    const auto sessionIterator = sessions.find(username);
    return sessionIterator == sessions.end() ? -1 : sessionIterator->second.fd;
}

int JoinHandler::fdOfUsername(const std::string& username) const {
    return fdOf(username);
}

int JoinHandler::kickTimeoutUsers(int timeoutMs) {
    const long long currentTimeMs = nowMs();
    std::vector<int> socketsToKick;
    for (const auto& entry : sessions) {
        if (currentTimeMs - entry.second.lastHeartbeatMs > timeoutMs)
            socketsToKick.push_back(entry.second.fd);
    }
    for (int fd : socketsToKick) {
        // Let kickCallback handle all cleanup (including JoinHandler removal).
        // We must NOT call removeConnection here first, or ServerApp won't
        // be able to look up the username for CallSession cleanup.
        if (kickCallback) kickCallback(fd);
        // If callback didn't remove (e.g. fd already gone), clean up here.
        if (usernamesByFd.count(fd)) {
            removeConnection(fd);
        }
    }
    return static_cast<int>(socketsToKick.size());
}
