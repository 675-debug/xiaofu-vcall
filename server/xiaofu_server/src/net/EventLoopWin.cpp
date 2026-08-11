#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "EventLoop.h"
#include "../util/Log.h"
#include <winsock2.h>
#include <windows.h>
#include <chrono>
#include <map>
#include <vector>

class EventLoopWin : public EventLoop {
public:
    bool addFd(int fd, IoCallback callback) override {
        if (callbacks.count(fd)) return false;
        callbacks[fd] = callback;
        return true;
    }
    bool removeFd(int fd) override { return callbacks.erase(fd) > 0; }
    bool setTimer(int intervalMs, TimerCallback callback) override {
        timerIntervalMs = intervalMs;
        timerCallback = callback;
        return true;
    }
    void run() override {
        running = true;
        auto lastTick = std::chrono::steady_clock::now();
        while (running) {
            if (timerCallback) {
                const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - lastTick).count();
                if (elapsedMs >= timerIntervalMs) {
                    timerCallback();
                    lastTick = std::chrono::steady_clock::now();
                    continue;
                }
            }
            const int pollTimeoutMs = timerCallback ? 1000 : -1;
            if (callbacks.empty()) {
                Sleep(pollTimeoutMs < 0 ? 100 : pollTimeoutMs);
                continue;
            }
            std::vector<WSAPOLLFD> pollDescriptors;
            std::vector<int> socketIds;
            for (const auto& entry : callbacks) {
                WSAPOLLFD descriptor = {};
                descriptor.fd = entry.first;
                descriptor.events = POLLRDNORM;
                pollDescriptors.push_back(descriptor);
                socketIds.push_back(entry.first);
            }
            const int readyCount = WSAPoll(pollDescriptors.data(),
                                           static_cast<ULONG>(pollDescriptors.size()),
                                           pollTimeoutMs);
            if (readyCount == SOCKET_ERROR) {
                Log::error(std::string("WSAPoll error: ") + std::to_string(WSAGetLastError()));
                continue;
            }
            for (size_t index = 0; index < pollDescriptors.size(); ++index) {
                if (pollDescriptors[index].revents & (POLLRDNORM | POLLERR | POLLHUP | POLLNVAL)) {
                    const auto callbackIterator = callbacks.find(socketIds[index]);
                    if (callbackIterator != callbacks.end())
                        callbackIterator->second(socketIds[index]);
                }
            }
        }
    }
    void stop() override { running = false; }

private:
    std::map<int, IoCallback> callbacks;
    TimerCallback timerCallback;
    int timerIntervalMs = 15000;
    bool running = false;
};

EventLoop* createEventLoop() { return new EventLoopWin(); }
