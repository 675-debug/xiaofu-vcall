#include "HeartbeatManager.h"
#include "../net/EventLoop.h"
#include "../handler/JoinHandler.h"
#include "../util/Log.h"

HeartbeatManager::HeartbeatManager(EventLoop* eventLoop, JoinHandler* handler,
                                   int intervalMs, int timeoutMs)
    : loop(eventLoop), joinHandler(handler), checkIntervalMs(intervalMs), timeoutMs(timeoutMs) {
    loop->setTimer(checkIntervalMs, [this] { check(); });
}

void HeartbeatManager::check() {
    const int kickedCount = joinHandler->kickTimeoutUsers(timeoutMs);
    if (kickedCount > 0) {
        Log::info("heartbeat timeout cleanup: online="
                  + std::to_string(joinHandler->onlineCount())
                  + ", kicked=" + std::to_string(kickedCount));
    }
}
