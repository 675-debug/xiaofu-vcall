#pragma once

#include "handler/JoinHandler.h"
#include "net/EpollLoop.h"
#include "net/SocketPlatform.h"

#include <cstdint>
#include <functional>
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

    std::string databasePath;
    std::size_t configuredWorkerCount;
    EpollLoop eventLoop;
    std::unique_ptr<CompletionDispatcher> completionDispatcher;
    std::unique_ptr<ThreadPool> workerPool;
    std::unique_ptr<TcpServer> tcpServer;
    std::unique_ptr<HeartbeatManager> heartbeatManager;
    JoinHandler joinHandler;
    std::unordered_map<int, std::unique_ptr<Connection>> connections;
    std::unordered_map<std::uint64_t, int> fdByConnectionId;
    std::uint64_t nextConnectionId = 1;
    int dispatchingFd = -1;
};
