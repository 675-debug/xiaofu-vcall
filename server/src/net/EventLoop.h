#pragma once
#include <functional>

class EventLoop {
public:
    using IoCallback = std::function<void(int fd)>;
    using TimerCallback = std::function<void()>;

    virtual ~EventLoop() {}
    virtual bool addFd(int fd, IoCallback callback) = 0;
    virtual bool removeFd(int fd) = 0;
    virtual bool setTimer(int intervalMs, TimerCallback callback) = 0;
    virtual void run() = 0;
    virtual void stop() = 0;
};

EventLoop* createEventLoop();
