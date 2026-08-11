#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// --------------- CallSession data ---------------

struct CallSession {
    std::string callId;
    std::string caller;
    std::string callee;
    std::uint64_t callerConnectionId = 0;
    std::uint64_t calleeConnectionId = 0;
    std::string state;          // ringing / connecting / connected / ended
    std::int64_t createdAt = 0;     // ms since epoch (steady_clock)
    std::int64_t acceptedAt = 0;
    std::int64_t connectedAt = 0;
    std::int64_t endedAt = 0;
    std::string endReason;
    int generation = 0;

    bool isEnded() const { return state == "ended"; }
    bool isActive() const { return !isEnded(); }
    bool isRinging() const { return state == "ringing"; }
    bool hasParticipant(const std::string& user) const {
        return user == caller || user == callee;
    }
    std::string otherUser(const std::string& user) const {
        return user == caller ? callee : caller;
    }
};

// --------------- CallSessionManager ---------------

class CallSessionManager {
public:
    using EndCallback = std::function<void(const std::string& callId,
                                            const std::string& caller,
                                            const std::string& callee)>;
    using PersistCallback = std::function<void(const CallSession& session)>;
    void setEndCallback(EndCallback cb) { endCallback_ = std::move(cb); }
    void setPersistCallback(PersistCallback cb) { persistCallback_ = std::move(cb); }

    // Create a new call session; returns nullptr if caller or callee is busy.
    CallSession* create(const std::string& callId,
                        const std::string& caller, std::uint64_t callerConnId,
                        const std::string& callee, std::uint64_t calleeConnId,
                        std::int64_t nowMs);

    CallSession* findByCallId(const std::string& callId);
    const CallSession* findByCallId(const std::string& callId) const;

    // Find the active call session for a user (if any).
    CallSession* findActiveForUser(const std::string& username);
    const CallSession* findActiveForUser(const std::string& username) const;

    // Transition ringing → connecting. Returns false if not in ringing state.
    bool accept(const std::string& callId, std::int64_t nowMs);

    // End a call session with a reason. Idempotent.
    // Returns true if the session was active and got ended now.
    bool end(const std::string& callId, const std::string& reason, std::int64_t nowMs);

    // End all calls a user participates in (called on disconnect).
    std::vector<std::string> endAllForUser(const std::string& username,
                                            const std::string& reason,
                                            std::int64_t nowMs);

    // Is user busy (has an active call)?
    bool isUserBusy(const std::string& username) const;

    // Clean up ended sessions older than `maxAgeMs`. Returns count removed.
    std::size_t purgeEnded(std::int64_t maxAgeMs, std::int64_t nowMs);

    // Find ringing sessions that have timed out. Returns list of callIds.
    std::vector<std::string> timeoutRinging(std::int64_t timeoutMs, std::int64_t nowMs) const;

    // For debug logging
    const std::map<std::string, CallSession>& all() const { return sessions_; }

private:
    std::map<std::string, CallSession> sessions_;  // callId → session
    EndCallback endCallback_;
    PersistCallback persistCallback_;
};

// --------------- BusyTracker ---------------

class BusyTracker {
public:
    void setBusy(const std::string& username, const std::string& callId) {
        busy_[username] = callId;
    }
    void release(const std::string& username) {
        busy_.erase(username);
    }
    bool isBusy(const std::string& username) const {
        return busy_.count(username) > 0;
    }
    std::string callIdFor(const std::string& username) const {
        auto it = busy_.find(username);
        return it != busy_.end() ? it->second : std::string();
    }
private:
    std::map<std::string, std::string> busy_;  // username → callId
};
