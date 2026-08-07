#pragma once

#include "EventLoop.h"
#include <chrono>
#include <map>

class EpollLoop : public EventLoop {
public:
    EpollLoop();
    ~EpollLoop() override;

    bool valid() const;
    bool addFd(int fd, std::uint32_t interests, IoCallback callback) override;
    bool updateFd(int fd, std::uint32_t interests) override;
    bool removeFd(int fd) override;
    bool setTimer(int intervalMs, TimerCallback callback) override;
    void run() override;
    void stop() override;

private:
    struct Registration {
        std::uint32_t interests = Read;
        IoCallback callback;
    };

    bool control(int operation, int fd, std::uint32_t interests);
    int waitTimeoutMs() const;
    void runTimerIfDue();

    int epollFd = -1;
    std::map<int, Registration> registrations;
    TimerCallback timerCallback;
    int timerIntervalMs = 0;
    std::chrono::steady_clock::time_point nextTimer;
    bool running = false;
};
