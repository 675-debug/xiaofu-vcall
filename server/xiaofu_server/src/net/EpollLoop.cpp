#include "EpollLoop.h"

#include "../util/Log.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>

namespace {
std::uint32_t nativeEvents(std::uint32_t interests) {
    std::uint32_t events = EPOLLET | EPOLLRDHUP;
    if (interests & EventLoop::Read)
        events |= EPOLLIN;
    if (interests & EventLoop::Write)
        events |= EPOLLOUT;
    return events;
}

std::uint32_t portableEvents(std::uint32_t events) {
    std::uint32_t result = 0;
    if (events & EPOLLIN)
        result |= EventLoop::Read;
    if (events & EPOLLOUT)
        result |= EventLoop::Write;
    if (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
        result |= EventLoop::Error;
    return result;
}
}

EpollLoop::EpollLoop()
    : epollFd(epoll_create1(EPOLL_CLOEXEC)) {
    if (epollFd < 0)
        Log::error(std::string("epoll_create1 failed: ") + std::strerror(errno));
}

EpollLoop::~EpollLoop() {
    if (epollFd >= 0)
        ::close(epollFd);
}

bool EpollLoop::valid() const {
    return epollFd >= 0;
}

bool EpollLoop::addFd(int fd, std::uint32_t interests, IoCallback callback) {
    if (!valid() || registrations.count(fd) != 0 || !callback)
        return false;
    if (!control(EPOLL_CTL_ADD, fd, interests))
        return false;
    registrations.emplace(fd, Registration{interests, std::move(callback)});
    return true;
}

bool EpollLoop::updateFd(int fd, std::uint32_t interests) {
    auto iterator = registrations.find(fd);
    if (iterator == registrations.end() || !control(EPOLL_CTL_MOD, fd, interests))
        return false;
    iterator->second.interests = interests;
    return true;
}

bool EpollLoop::removeFd(int fd) {
    const auto erased = registrations.erase(fd);
    if (erased == 0)
        return false;
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
    return true;
}

bool EpollLoop::setTimer(int intervalMs, TimerCallback callback) {
    if (intervalMs <= 0 || !callback)
        return false;
    timerIntervalMs = intervalMs;
    timerCallback = std::move(callback);
    nextTimer = std::chrono::steady_clock::now() + std::chrono::milliseconds(timerIntervalMs);
    return true;
}

void EpollLoop::run() {
    if (!valid())
        return;

    running = true;
    std::vector<epoll_event> events(64);
    while (running) {
        const int readyCount = epoll_wait(epollFd, events.data(),
                                         static_cast<int>(events.size()), waitTimeoutMs());
        if (readyCount < 0) {
            if (errno == EINTR)
                continue;
            Log::error(std::string("epoll_wait failed: ") + std::strerror(errno));
            break;
        }

        for (int index = 0; index < readyCount; ++index) {
            const int fd = events[index].data.fd;
            const auto iterator = registrations.find(fd);
            if (iterator == registrations.end())
                continue;
            const IoCallback callback = iterator->second.callback;
            callback(portableEvents(events[index].events));
        }
        runTimerIfDue();
    }
}

void EpollLoop::stop() {
    running = false;
}

bool EpollLoop::control(int operation, int fd, std::uint32_t interests) {
    epoll_event event = {};
    event.events = nativeEvents(interests);
    event.data.fd = fd;
    if (epoll_ctl(epollFd, operation, fd, &event) == 0)
        return true;
    Log::error(std::string("epoll_ctl failed fd=") + std::to_string(fd)
               + ": " + std::strerror(errno));
    return false;
}

int EpollLoop::waitTimeoutMs() const {
    if (!timerCallback)
        return 1000;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        nextTimer - std::chrono::steady_clock::now()).count();
    return static_cast<int>(std::max<long long>(0, std::min<long long>(remaining, 1000)));
}

void EpollLoop::runTimerIfDue() {
    if (!timerCallback || std::chrono::steady_clock::now() < nextTimer)
        return;
    timerCallback();
    nextTimer = std::chrono::steady_clock::now() + std::chrono::milliseconds(timerIntervalMs);
}
