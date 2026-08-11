#include "CallSession.h"
#include "../util/Log.h"

CallSession* CallSessionManager::create(const std::string& callId,
                                         const std::string& caller, std::uint64_t callerConnId,
                                         const std::string& callee, std::uint64_t calleeConnId,
                                         std::int64_t nowMs)
{
    if (callId.empty() || caller.empty() || callee.empty() || caller == callee)
        return nullptr;
    CallSession s;
    s.callId = callId;
    s.caller = caller;
    s.callee = callee;
    s.callerConnectionId = callerConnId;
    s.calleeConnectionId = calleeConnId;
    s.state = "ringing";
    s.createdAt = nowMs;
    sessions_[callId] = s;
    return &sessions_[callId];
}

CallSession* CallSessionManager::findByCallId(const std::string& callId) {
    auto it = sessions_.find(callId);
    return it != sessions_.end() ? &it->second : nullptr;
}

const CallSession* CallSessionManager::findByCallId(const std::string& callId) const {
    auto it = sessions_.find(callId);
    return it != sessions_.end() ? &it->second : nullptr;
}

CallSession* CallSessionManager::findActiveForUser(const std::string& username) {
    for (auto& entry : sessions_) {
        if (!entry.second.isEnded() && entry.second.hasParticipant(username))
            return &entry.second;
    }
    return nullptr;
}

const CallSession* CallSessionManager::findActiveForUser(const std::string& username) const {
    for (const auto& entry : sessions_) {
        if (!entry.second.isEnded() && entry.second.hasParticipant(username))
            return &entry.second;
    }
    return nullptr;
}

bool CallSessionManager::end(const std::string& callId, const std::string& reason,
                              std::int64_t nowMs)
{
    auto it = sessions_.find(callId);
    if (it == sessions_.end() || it->second.isEnded())
        return false;
    it->second.state = "ended";
    it->second.endedAt = nowMs;
    it->second.endReason = reason;
    Log::info("=== CALL ENDED === callId=" + callId
              + " reason=" + reason
              + " caller=" + it->second.caller
              + " callee=" + it->second.callee
              + " duration=" + std::to_string(it->second.endedAt - it->second.createdAt) + "ms");
    if (endCallback_)
        endCallback_(callId, it->second.caller, it->second.callee);
    if (persistCallback_)
        persistCallback_(it->second);
    return true;
}

std::vector<std::string> CallSessionManager::endAllForUser(const std::string& username,
                                                            const std::string& reason,
                                                            std::int64_t nowMs)
{
    std::vector<std::string> ended;
    for (auto& entry : sessions_) {
        if (!entry.second.isEnded() && entry.second.hasParticipant(username)) {
            if (end(entry.first, reason, nowMs))
                ended.push_back(entry.first);
        }
    }
    return ended;
}

bool CallSessionManager::accept(const std::string& callId, std::int64_t nowMs)
{
    auto it = sessions_.find(callId);
    if (it == sessions_.end()) return false;
    if (it->second.state != "ringing") return false;
    it->second.state = "connecting";
    it->second.acceptedAt = nowMs;
    ++it->second.generation;  // invalidates any pending ringing timeout
    Log::info("[CALL " + callId + "] state ringing -> connecting  generation="
              + std::to_string(it->second.generation));
    return true;
}

bool CallSessionManager::isUserBusy(const std::string& username) const {
    for (const auto& entry : sessions_) {
        if (!entry.second.isEnded() && entry.second.hasParticipant(username))
            return true;
    }
    return false;
}

std::size_t CallSessionManager::purgeEnded(std::int64_t maxAgeMs, std::int64_t nowMs) {
    std::vector<std::string> toRemove;
    for (const auto& entry : sessions_) {
        if (entry.second.isEnded() && (nowMs - entry.second.endedAt) > maxAgeMs)
            toRemove.push_back(entry.first);
    }
    for (const auto& id : toRemove)
        sessions_.erase(id);
    return toRemove.size();
}

std::vector<std::string> CallSessionManager::timeoutRinging(std::int64_t timeoutMs,
                                                              std::int64_t nowMs) const
{
    std::vector<std::string> ids;
    for (const auto& entry : sessions_) {
        if (entry.second.state == "ringing" && (nowMs - entry.second.createdAt) > timeoutMs)
            ids.push_back(entry.first);
    }
    return ids;
}
