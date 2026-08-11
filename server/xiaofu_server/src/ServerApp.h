#pragma once

#include "handler/JoinHandler.h"
#include "handler/CallSession.h"
#include "net/EpollLoop.h"
#include "net/SocketPlatform.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

class CompletionDispatcher;
class Connection;
class HeartbeatManager;
class TcpServer;
class ThreadPool;

class ServerApp {
public:
    static constexpr std::int64_t RINGING_TIMEOUT_MS = 30000;  // 30 seconds

    explicit ServerApp(std::string databasePath, std::size_t workerCount = 2);
    ~ServerApp();

    bool start(const std::string& listenAddress, int port);
    void run();
    void stop();

private:
    void acceptConnection(SocketHandle socketHandle);
    void handleConnectionEvent(int fd, std::uint32_t events);
    void handleMessage(std::uint64_t connectionId, const std::string& messageBody);
    void removeConnection(int fd);
    void refreshConnectionInterest(int fd);

    Connection* findConnection(std::uint64_t connectionId);
    void sendTo(std::uint64_t connectionId, const std::string& payload);
    void broadcastPresence(const std::string& username, bool online);
    bool submitDatabaseTask(std::uint64_t connectionId,
                            std::function<void(class DbManager&)> task);

    // --------------- CallSession management ---------------
    std::string generateCallId();
    std::string callIdForPair(const std::string& userA, const std::string& userB);
    std::string findActiveCallForUser(const std::string& username) const;

    // Cleanup all calls for a disconnected user. Notifies peer if still online.
    void cleanupCallsForUser(const std::string& username, std::int64_t nowMs,
                             const std::string& reason);
    // Send a call-ended notification to a specific connection.
    void sendCallEnded(std::uint64_t connId, const std::string& callId,
                       const std::string& reason, const std::string& peer);
    // Check and handle ringing timeouts.
    void checkRingingTimeouts(std::int64_t nowMs);

    // --------------- Signal validation ---------------
    enum class SignalResult {
        Ok,
        StaleCallIgnored,
        NotParticipant,
        WrongState,
        CallNotFound,
        PeerOffline,
        UserBusy,
        DuplicateLogin,
    };
    SignalResult validateCallSignal(const std::string& type,
                                    const std::string& callId,
                                    const std::string& from,
                                    const std::string& to,
                                    const CallSession* session);

    // --------------- SDP diagnostics ---------------
    void logSdpDiag(const std::string& callId, const std::string& signalType,
                    const std::string& sender, const std::string& receiver,
                    const std::string& sdp);

    // --------------- ICE logging ---------------
    void logIceDiag(const std::string& callId,
                    const std::string& sender, const std::string& receiver,
                    const std::string& candidateStr,
                    const CallSession* session);

    // --------------- Call history ---------------
    void persistCallRecord(const CallSession& session);
    void handleCallConnected(std::uint64_t connectionId, const std::string& callId,
                             std::int64_t nowMs);
    void handleCallHistory(std::uint64_t connectionId, const std::string& username);

    // --------------- FD tracking ---------------
    void trackFdForUser(const std::string& username, int fd);
    void checkFdStability(const std::string& user, int fd);

    // --------------- Data ---------------
    std::string databasePath;
    std::size_t configuredWorkerCount;
    EpollLoop eventLoop;
    std::unique_ptr<CompletionDispatcher> completionDispatcher;
    std::unique_ptr<ThreadPool> workerPool;
    std::unique_ptr<TcpServer> tcpServer;
    std::unique_ptr<HeartbeatManager> heartbeatManager;
    JoinHandler joinHandler;
    CallSessionManager callManager;
    BusyTracker busyTracker;
    std::unordered_map<int, std::unique_ptr<Connection>> connections;
    std::unordered_map<std::uint64_t, int> fdByConnectionId;
    std::uint64_t nextConnectionId = 1;
    int dispatchingFd = -1;

    // call session id sequence
    int nextCallSeq = 1;

    // debug: ice ufrag tracking per callId
    std::map<std::string, std::string> callUfrags_;
};
