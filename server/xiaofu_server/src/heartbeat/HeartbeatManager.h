#pragma once

class EventLoop;
class JoinHandler;

class HeartbeatManager {
public:
    HeartbeatManager(EventLoop* eventLoop, JoinHandler* handler,
                     int intervalMs, int timeoutMs);
    void check();
private:
    EventLoop* loop;
    JoinHandler* joinHandler;
    int checkIntervalMs;
    int timeoutMs;
};
