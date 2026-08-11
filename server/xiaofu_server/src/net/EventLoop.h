#pragma once
#include <cstdint>
#include <functional>

class EventLoop {
public:
    enum Event : std::uint32_t {
        Read = 1U << 0,
        Write = 1U << 1,
        Error = 1U << 2
    };

    using IoCallback = std::function<void(std::uint32_t events)>;
    using TimerCallback = std::function<void()>;

    virtual ~EventLoop() {}
    virtual bool addFd(int fd, std::uint32_t interests, IoCallback callback) = 0;
    virtual bool updateFd(int fd, std::uint32_t interests) = 0;
    virtual bool removeFd(int fd) = 0;
    virtual bool setTimer(int intervalMs, TimerCallback callback) = 0;
    virtual void run() = 0;
    virtual void stop() = 0;
};

EventLoop* createEventLoop();
