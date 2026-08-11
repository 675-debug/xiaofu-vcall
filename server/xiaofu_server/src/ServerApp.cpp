#include "ServerApp.h"

#include "concurrency/CompletionDispatcher.h"
#include "concurrency/ThreadPool.h"
#include "db/DbManager.h"
#include "handler/ForgotHandler.h"
#include "handler/LoginHandler.h"
#include "handler/RegisterHandler.h"
#include "heartbeat/HeartbeatManager.h"
#include "net/Connection.h"
#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "protocol/JsonValue.h"
#include "protocol/ResultCode.h"
#include "util/Log.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <utility>
#include <vector>

// ============================================================
//  Anonymous helpers
// ============================================================

namespace {

std::string makeResponse(const std::string& type, ResultCode code, const std::string& message)
{
    JsonValue response;
    response.set("type", JsonValue(type));
    response.set("code", JsonValue(static_cast<int>(code)));
    response.set("msg", JsonValue(message));
    return response.serialize();
}

JsonValue messageToJson(const ChatMessage& message)
{
    JsonValue jsonMessage;
    jsonMessage.set("id", JsonValue(static_cast<int>(message.id)));
    jsonMessage.set("from", JsonValue(message.sender));
    jsonMessage.set("to", JsonValue(message.receiver));
    jsonMessage.set("content", JsonValue(message.content));
    jsonMessage.set("sentAt", JsonValue(message.sentAt));
    return jsonMessage;
}

JsonValue friendRequestToJson(const FriendRequest& request)
{
    JsonValue jsonRequest;
    jsonRequest.set("sender", JsonValue(request.senderUsername));
    jsonRequest.set("nickname", JsonValue(request.nickname));
    jsonRequest.set("avatarSeed", JsonValue(request.avatarSeed));
    jsonRequest.set("createdAt", JsonValue(request.createdAt));
    return jsonRequest;
}

std::int64_t steadyNowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string clockMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const auto sec = ms / 1000;
    const auto msec = ms % 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld.%03lld",
                  static_cast<long long>(sec), static_cast<long long>(msec));
    return buf;
}

std::string pairKey(const std::string& a, const std::string& b) {
    return a < b ? (a + "|" + b) : (b + "|" + a);
}

// --------------- ICE candidate field extraction ---------------

struct IceFields {
    std::string type = "?";
    std::string protocol = "?";
    std::string address = "?";
    std::string port = "?";
    std::string priority = "?";
    std::string foundation = "?";
    std::string ufrag;
};

IceFields parseIce(const std::string& candidateStr) {
    IceFields f;
    if (candidateStr.empty()) return f;
    std::istringstream stream(candidateStr);
    std::vector<std::string> tokens;
    std::string t;
    while (stream >> t) tokens.push_back(t);
    if (tokens.empty()) return f;
    const auto cp = tokens[0].find(':');
    f.foundation = (cp != std::string::npos) ? tokens[0].substr(cp + 1) : tokens[0];
    if (tokens.size() > 2) f.protocol = tokens[2];
    if (tokens.size() > 3) f.priority = tokens[3];
    if (tokens.size() > 4) f.address = tokens[4];
    if (tokens.size() > 5) f.port = tokens[5];
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == "typ" && i + 1 < tokens.size()) f.type = tokens[i + 1];
        if (tokens[i] == "ufrag" && i + 1 < tokens.size()) f.ufrag = tokens[i + 1];
    }
    return f;
}

// --------------- SDP diagnostic parser ---------------

struct MediaSection {
    bool present = false;
    std::string mid;
    int port = 0;
    std::string direction;
    std::string directionSource;  // explicit / session / default
    std::string primaryCodec;
    bool rtx = false;
    bool red = false;
    bool ulpfec = false;
    std::string allCodecs;  // comma-separated all codec names found
    bool msid = false;
    bool ssrc = false;
    int ssrcCount = 0;
    bool rtcpMux = false;
};

struct SdpDiag {
    int length = 0;
    int mediaSections = 0;
    MediaSection video;
    MediaSection audio;
    std::string fingerprintAlgo;
    std::string fingerprintHash;
    std::string ufrag;
    std::string sessionDirection;  // session-level direction if set
};

SdpDiag parseSdpDiag(const std::string& sdp) {
    SdpDiag d;
    if (sdp.empty()) return d;
    d.length = static_cast<int>(sdp.size());

    std::istringstream stream(sdp);
    std::string line;
    MediaSection* cur = nullptr;
    bool inVideo = false, inAudio = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // session-level direction
        if (line == "a=sendrecv" || line == "a=sendonly" ||
            line == "a=recvonly" || line == "a=inactive") {
            if (!cur) d.sessionDirection = line.substr(2);
            else {
                cur->direction = line.substr(2);
                cur->directionSource = "explicit";
            }
            if (cur == &d.video) continue;
            // fall through for audio too
            if (cur) continue;
        }

        // media line
        if (line.rfind("m=", 0) == 0) {
            ++d.mediaSections;
            inVideo = false; inAudio = false;
            if (line.rfind("m=video", 0) == 0) {
                inVideo = true; cur = &d.video; d.video.present = true;
                // parse port: m=video 9 UDP/TLS/RTP/SAVPF ...
                std::istringstream ml(line.substr(7));
                ml >> d.video.port;
            } else if (line.rfind("m=audio", 0) == 0) {
                inAudio = true; cur = &d.audio; d.audio.present = true;
                std::istringstream ml(line.substr(7));
                ml >> d.audio.port;
            } else {
                cur = nullptr;
            }
            continue;
        }

        // mid
        if (line.rfind("a=mid:", 0) == 0) {
            if (inVideo) d.video.mid = line.substr(6);
            else if (inAudio) d.audio.mid = line.substr(6);
            continue;
        }

        // direction in media section
        if (line == "a=sendrecv" || line == "a=sendonly" ||
            line == "a=recvonly" || line == "a=inactive") {
            if (inVideo) { d.video.direction = line.substr(2); d.video.directionSource = "explicit"; }
            else if (inAudio) { d.audio.direction = line.substr(2); d.audio.directionSource = "explicit"; }
            continue;
        }

        // rtpmap / codec
        if (line.rfind("a=rtpmap:", 0) == 0 && cur) {
            const auto sp = line.find(' ');
            if (sp != std::string::npos) {
                const std::string rest = line.substr(sp + 1);
                const auto sl = rest.find('/');
                if (sl != std::string::npos) {
                    std::string c = rest.substr(0, sl);
                    // Distinguish primary codec from auxiliary payload types
                    bool isAux = false;
                    if (c == "rtx" || c == "RTX")      { cur->rtx = true; isAux = true; }
                    else if (c == "red" || c == "RED")  { cur->red = true; isAux = true; }
                    else if (c == "ulpfec" || c == "ULPFEC") { cur->ulpfec = true; isAux = true; }
                    else if (c == "flexfec" || c == "FLEXFEC") isAux = true;
                    // Normalize primary codec name
                    if      (c == "vp8"  || c == "VP8")  c = "VP8";
                    else if (c == "vp9"  || c == "VP9")  c = "VP9";
                    else if (c == "h264" || c == "H264") c = "H264";
                    else if (c == "h265" || c == "H265") c = "H265";
                    else if (c == "av1"  || c == "AV1")  c = "AV1";
                    else if (c == "opus") c = "opus";
                    else if (c == "pcma" || c == "PCMA") c = "PCMA";
                    else if (c == "pcmu" || c == "PCMU") c = "PCMU";
                    if (!isAux && cur->primaryCodec.empty())
                        cur->primaryCodec = c;
                    if (!cur->allCodecs.empty()) cur->allCodecs += ",";
                    cur->allCodecs += c;
                }
            }
            continue;
        }

        // msid
        if (line.rfind("a=msid", 0) == 0) {
            if (cur) cur->msid = true;
            continue;
        }

        // ssrc
        if (line.rfind("a=ssrc:", 0) == 0) {
            if (cur) { cur->ssrc = true; ++cur->ssrcCount; }
            continue;
        }

        // rtcp-mux
        if (line.rfind("a=rtcp-mux", 0) == 0) {
            if (inVideo) d.video.rtcpMux = true;
            else if (inAudio) d.audio.rtcpMux = true;
            continue;
        }

        // fingerprint
        if (line.rfind("a=fingerprint:", 0) == 0) {
            const std::string fp = line.substr(15);
            const auto spacePos = fp.find(' ');
            if (spacePos != std::string::npos) {
                d.fingerprintAlgo = fp.substr(0, spacePos);
                d.fingerprintHash = fp.substr(spacePos + 1);
            } else {
                d.fingerprintAlgo = fp;
            }
            continue;
        }

        // ice-ufrag
        if (line.rfind("a=ice-ufrag:", 0) == 0) {
            d.ufrag = line.substr(12);
            continue;
        }
    }

    // Fill default directions
    for (MediaSection* ms : {&d.video, &d.audio}) {
        if (!ms->present) continue;
        if (ms->direction.empty()) {
            if (!d.sessionDirection.empty()) {
                ms->direction = d.sessionDirection;
                ms->directionSource = "session";
            } else {
                ms->direction = "sendrecv";
                ms->directionSource = "default";
            }
        }
    }

    return d;
}

std::string trimFingerprint(const std::string& s) {
    return s.size() > 24 ? s.substr(0, 24) + "..." : s;
}

std::string describeMedia(const MediaSection& m, const std::string& label) {
    if (!m.present) return "";
    std::ostringstream out;
    out << label << ": mid=" << (m.mid.empty() ? "?" : m.mid)
        << " port=" << m.port
        << " dir=" << m.direction << "(" << m.directionSource << ")"
        << " codec=" << (m.primaryCodec.empty() ? "?" : m.primaryCodec)
        << " msid=" << (m.msid ? "yes" : "no")
        << " ssrc=" << (m.ssrc ? "yes" : "no")
        << "(" << m.ssrcCount << ")"
        << " rtcp_mux=" << (m.rtcpMux ? "yes" : "no");
    // auxiliary payloads
    if (m.rtx || m.red || m.ulpfec) {
        out << " aux=";
        std::vector<std::string> aux;
        if (m.rtx) aux.push_back("rtx");
        if (m.red) aux.push_back("red");
        if (m.ulpfec) aux.push_back("ulpfec");
        for (size_t i = 0; i < aux.size(); ++i) {
            if (i) out << ",";
            out << aux[i];
        }
    }
    return out.str();
}

} // anonymous namespace

// ============================================================
//  ServerApp constructor / destructor
// ============================================================

ServerApp::ServerApp(std::string databasePath, std::size_t workerCount)
    : databasePath(std::move(databasePath)),
      configuredWorkerCount(workerCount),
      joinHandler([this](int fd) {
          Log::info("heartbeat timeout, kicking fd=" + std::to_string(fd));
          removeConnection(fd);
      })
{
    callManager.setEndCallback([this](const std::string& callId,
                                       const std::string& caller,
                                       const std::string& callee) {
        busyTracker.release(caller);
        busyTracker.release(callee);
    });
    callManager.setPersistCallback([this](const CallSession& session) {
        persistCallRecord(session);
    });
}

ServerApp::~ServerApp()
{
    workerPool.reset();
    heartbeatManager.reset();
    tcpServer.reset();
    connections.clear();
    completionDispatcher.reset();
    shutdownSocketPlatform();
}

// ============================================================
//  start / run / stop
// ============================================================

bool ServerApp::start(const std::string& listenAddress, int port)
{
    DbManager setupDatabase;
    if (!setupDatabase.open(databasePath) || !setupDatabase.createTables()) {
        Log::error("server startup aborted: cannot prepare database " + databasePath);
        return false;
    }
    // After restart, all old TCP connections are gone — reset any lingering "登录" to "下线"
    setupDatabase.resetAllLoginStatus();
    setupDatabase.close();
    Log::info("database tables ready: " + databasePath);

    completionDispatcher = std::make_unique<CompletionDispatcher>();
    if (!completionDispatcher->valid()) {
        Log::error("server startup aborted: cannot create eventfd");
        return false;
    }
    if (!eventLoop.addFd(completionDispatcher->fd(), EventLoop::Read,
                         [this](std::uint32_t events) {
        if ((events & EventLoop::Read) != 0U)
            completionDispatcher->drain();
    })) {
        Log::error("server startup aborted: cannot register completion eventfd");
        return false;
    }

    workerPool = std::make_unique<ThreadPool>(configuredWorkerCount, databasePath);
    heartbeatManager = std::make_unique<HeartbeatManager>(&eventLoop, &joinHandler,
                                                           15000, 50000);
    tcpServer = std::make_unique<TcpServer>(&eventLoop);
    tcpServer->setAcceptCallback([this](SocketHandle socketHandle) {
        acceptConnection(socketHandle);
    });
    if (!tcpServer->listen(listenAddress.c_str(), port))
        return false;

    Log::info("xiaofu-vcall epoll server started, workers="
              + std::to_string(workerPool->workerCount()));
    return true;
}

void ServerApp::run() { eventLoop.run(); }
void ServerApp::stop() { eventLoop.stop(); }

// ============================================================
//  Connection management
// ============================================================

void ServerApp::acceptConnection(SocketHandle socketHandle)
{
    const int fd = static_cast<int>(socketHandle);
    const std::uint64_t connectionId = nextConnectionId++;
    auto connection = std::make_unique<Connection>(socketHandle, connectionId);
    connection->setMessageCallback([this, connectionId](Connection*, const std::string& body) {
        handleMessage(connectionId, body);
    });
    connections.emplace(fd, std::move(connection));
    fdByConnectionId[connectionId] = fd;
    Log::info("client connected fd=" + std::to_string(fd)
              + " connId=" + std::to_string(connectionId));

    if (!eventLoop.addFd(fd, EventLoop::Read,
                         [this, fd](std::uint32_t events) {
        handleConnectionEvent(fd, events);
    })) {
        Log::error("register client in epoll failed fd=" + std::to_string(fd));
        removeConnection(fd);
    }
}

void ServerApp::handleConnectionEvent(int fd, std::uint32_t events)
{
    const auto iterator = connections.find(fd);
    if (iterator == connections.end()) return;

    dispatchingFd = fd;
    Connection* connection = iterator->second.get();
    if ((events & EventLoop::Error) != 0U)
        connection->close();
    if (!connection->closed() && (events & EventLoop::Read) != 0U)
        connection->onReadable();
    if (!connection->closed() && (events & EventLoop::Write) != 0U)
        connection->onWritable();
    dispatchingFd = -1;

    const auto current = connections.find(fd);
    if (current == connections.end()) return;
    if (current->second->closed())
        removeConnection(fd);
    else
        refreshConnectionInterest(fd);
}

void ServerApp::removeConnection(int fd)
{
    const auto iterator = connections.find(fd);
    if (iterator == connections.end()) return;

    const std::string username = joinHandler.usernameOf(fd);
    const std::uint64_t connectionId = iterator->second->id();
    const std::int64_t now = steadyNowMs();

    // --- P0: cleanup all active call sessions for this user ---
    if (!username.empty()) {
        Log::info("=== CLIENT DISCONNECTED === username=" + username
                  + " fd=" + std::to_string(fd)
                  + " connId=" + std::to_string(connectionId));

        // Find and end all active calls for this user
        const CallSession* active = callManager.findActiveForUser(username);
        std::string activeCallId;
        if (active) activeCallId = active->callId;

        auto endedCallIds = callManager.endAllForUser(username, "disconnected", now);

        for (const auto& cid : endedCallIds) {
            const CallSession* cs = callManager.findByCallId(cid);
            if (!cs) continue;
            const std::string peer = cs->otherUser(username);
            busyTracker.release(username);
            busyTracker.release(peer);

            Log::info("[CALL " + cid + "][CLEANUP] reason=PEER_DISCONNECTED"
                      + std::string(" state=ended user=") + username
                      + " peer=" + peer);

            // Notify peer if still online
            const int peerFd = joinHandler.fdOf(peer);
            if (peerFd >= 0) {
                const auto peerConnIt = connections.find(peerFd);
                if (peerConnIt != connections.end()) {
                    JsonValue notify;
                    notify.set("type", JsonValue("call_ended"));
                    notify.set("callId", JsonValue(cid));
                    notify.set("reason", JsonValue("PEER_DISCONNECTED"));
                    notify.set("peer", JsonValue(username));
                    sendTo(peerConnIt->second->id(), notify.serialize());
                    Log::info("[CALL " + cid + "][CLEANUP] peer_notified=" + peer);
                }
            }
        }
    } else {
        Log::info("client disconnected fd=" + std::to_string(fd)
                  + " connId=" + std::to_string(connectionId) + " (no username)");
    }

    // Clean up connection resources
    joinHandler.removeConnection(fd);
    eventLoop.removeFd(fd);
    fdByConnectionId.erase(connectionId);
    connections.erase(iterator);

    if (!username.empty()) {
        broadcastPresence(username, false);
        // Persist loginlog as offline (connection is gone, heartbeat won't refresh)
        if (workerPool) {
            workerPool->submit([username](DbManager& db) {
                db.setOffline(username);
            });
        }
    }
}

void ServerApp::refreshConnectionInterest(int fd)
{
    const auto iterator = connections.find(fd);
    if (iterator == connections.end() || iterator->second->closed()) return;
    std::uint32_t interests = EventLoop::Read;
    if (iterator->second->hasPendingOutput())
        interests |= EventLoop::Write;
    eventLoop.updateFd(fd, interests);
}

Connection* ServerApp::findConnection(std::uint64_t connectionId)
{
    const auto fdIterator = fdByConnectionId.find(connectionId);
    if (fdIterator == fdByConnectionId.end()) return nullptr;
    const auto connectionIterator = connections.find(fdIterator->second);
    return connectionIterator == connections.end() ? nullptr : connectionIterator->second.get();
}

void ServerApp::sendTo(std::uint64_t connectionId, const std::string& payload)
{
    Connection* connection = findConnection(connectionId);
    if (connection == nullptr) return;
    const int fd = connection->fd();
    connection->sendMessage(payload);
    if (connection->closed()) {
        if (dispatchingFd != fd) removeConnection(fd);
        return;
    }
    refreshConnectionInterest(fd);
}

void ServerApp::broadcastPresence(const std::string& username, bool online)
{
    JsonValue presence;
    presence.set("type", JsonValue("presence_push"));
    presence.set("username", JsonValue(username));
    presence.set("online", JsonValue(online));
    const std::string payload = presence.serialize();

    std::vector<std::uint64_t> recipients;
    recipients.reserve(connections.size());
    for (const auto& entry : connections) {
        if (!joinHandler.usernameOf(entry.first).empty())
            recipients.push_back(entry.second->id());
    }
    for (const std::uint64_t connectionId : recipients)
        sendTo(connectionId, payload);
}

bool ServerApp::submitDatabaseTask(std::uint64_t connectionId,
                                   std::function<void(DbManager&)> task)
{
    if (workerPool && workerPool->submit(std::move(task)))
        return true;
    sendTo(connectionId, makeResponse("error_resp", ResultCode::Failed, "server is stopping"));
    return false;
}

// ============================================================
//  Call session helpers
// ============================================================

std::string ServerApp::generateCallId()
{
    const auto t = std::time(nullptr);
    const auto tm = std::localtime(&t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%05d",
                  1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday,
                  nextCallSeq++);
    return buf;
}

std::string ServerApp::callIdForPair(const std::string& userA, const std::string& userB)
{
    const CallSession* cs = callManager.findActiveForUser(userA);
    if (cs && cs->hasParticipant(userB)) return cs->callId;
    cs = callManager.findActiveForUser(userB);
    if (cs && cs->hasParticipant(userA)) return cs->callId;
    return userA + "-" + userB;
}

std::string ServerApp::findActiveCallForUser(const std::string& username) const
{
    const CallSession* cs = callManager.findActiveForUser(username);
    return cs ? cs->callId : std::string();
}

void ServerApp::cleanupCallsForUser(const std::string& username, std::int64_t nowMs,
                                     const std::string& reason)
{
    auto ended = callManager.endAllForUser(username, reason, nowMs);
    for (const auto& cid : ended) {
        const CallSession* cs = callManager.findByCallId(cid);
        if (!cs) continue;
        const std::string peer = cs->otherUser(username);
        busyTracker.release(username);
        busyTracker.release(peer);
        Log::info("[CALL " + cid + "][CLEANUP] reason=" + reason
                  + " user=" + username + " peer=" + peer);

        const int peerFd = joinHandler.fdOf(peer);
        if (peerFd >= 0) {
            const auto it = connections.find(peerFd);
            if (it != connections.end()) {
                JsonValue notify;
                notify.set("type", JsonValue("call_ended"));
                notify.set("callId", JsonValue(cid));
                notify.set("reason", JsonValue(reason));
                notify.set("peer", JsonValue(username));
                sendTo(it->second->id(), notify.serialize());
                Log::info("[CALL " + cid + "][CLEANUP] peer_notified=" + peer);
            }
        }
    }
}

void ServerApp::sendCallEnded(std::uint64_t connId, const std::string& callId,
                               const std::string& reason, const std::string& peer)
{
    JsonValue notify;
    notify.set("type", JsonValue("call_ended"));
    notify.set("callId", JsonValue(callId));
    notify.set("reason", JsonValue(reason));
    notify.set("peer", JsonValue(peer));
    sendTo(connId, notify.serialize());
}

void ServerApp::checkRingingTimeouts(std::int64_t nowMs)
{
    auto timedOut = callManager.timeoutRinging(RINGING_TIMEOUT_MS, nowMs);
    for (const auto& cid : timedOut) {
        // Re-verify: session must still exist AND still be ringing.
        // accept() increments generation, so a stale timeout cannot
        // accidentally kill a session that has already moved to connecting.
        const CallSession* cs = callManager.findByCallId(cid);
        if (!cs) {
            Log::debug("[CALL " + cid + "][TIMEOUT] skipped: session no longer exists");
            continue;
        }
        if (!cs->isRinging()) {
            Log::debug("[CALL " + cid + "][TIMEOUT] skipped: state already "
                       + cs->state + " (generation="
                       + std::to_string(cs->generation) + ")");
            continue;
        }
        Log::info("[CALL " + cid + "][TIMEOUT] ringing timeout  generation="
                  + std::to_string(cs->generation));
        callManager.end(cid, "timeout", nowMs);
        busyTracker.release(cs->caller);
        busyTracker.release(cs->callee);

        // notify caller
        const int callerFd = joinHandler.fdOf(cs->caller);
        if (callerFd >= 0) {
            const auto it = connections.find(callerFd);
            if (it != connections.end())
                sendCallEnded(it->second->id(), cid, "NO_ANSWER", cs->callee);
        }
        // notify callee
        const int calleeFd = joinHandler.fdOf(cs->callee);
        if (calleeFd >= 0) {
            const auto it = connections.find(calleeFd);
            if (it != connections.end())
                sendCallEnded(it->second->id(), cid, "MISSED_CALL", cs->caller);
        }
    }
}

// ============================================================
//  Signal validation
// ============================================================

ServerApp::SignalResult ServerApp::validateCallSignal(
    const std::string& type, const std::string& callId,
    const std::string& from, const std::string& to,
    const CallSession* session)
{
    if (!session)
        return SignalResult::CallNotFound;
    if (session->isEnded())
        return SignalResult::StaleCallIgnored;
    if (!session->hasParticipant(from))
        return SignalResult::NotParticipant;
    if (!session->hasParticipant(to))
        return SignalResult::NotParticipant;

    // state checks
    const std::string& state = session->state;
    if (type == "call_accept" && state != "ringing")
        return SignalResult::WrongState;
    if (type == "call_reject" && state != "ringing")
        return SignalResult::WrongState;
    if (type == "call_cancel" && state != "ringing")
        return SignalResult::WrongState;
    // offer/answer/candidate valid in ringing/connecting/connected
    if ((type == "webrtc_offer" || type == "webrtc_answer" || type == "ice_candidate")
        && (state != "ringing" && state != "connecting" && state != "connected"))
        return SignalResult::WrongState;

    return SignalResult::Ok;
}

// ============================================================
//  SDP diagnostics
// ============================================================

void ServerApp::logSdpDiag(const std::string& callId, const std::string& signalType,
                            const std::string& sender, const std::string& receiver,
                            const std::string& sdp)
{
    const auto d = parseSdpDiag(sdp);
    const std::string ts = clockMs();

    Log::debug("--- [CALL " + callId + "][" + signalType + "] [" + ts + "] ---");
    Log::debug("  sender=" + sender + " -> receiver=" + receiver);
    Log::debug("  SDP len=" + std::to_string(d.length)
               + " media_sections=" + std::to_string(d.mediaSections));

    std::string videoInfo = describeMedia(d.video, "VIDEO");
    std::string audioInfo = describeMedia(d.audio, "AUDIO");

    if (!videoInfo.empty()) Log::debug("  " + videoInfo);
    if (!audioInfo.empty()) Log::debug("  " + audioInfo);

    if (!d.fingerprintAlgo.empty())
        Log::debug("  DTLS fp=" + d.fingerprintAlgo + " " + trimFingerprint(d.fingerprintHash));
    if (!d.ufrag.empty()) {
        Log::debug("  ICE ufrag=" + d.ufrag);
        callUfrags_[callId] = d.ufrag;
    }
}

// ============================================================
//  ICE diagnostics
// ============================================================

void ServerApp::logIceDiag(const std::string& callId,
                            const std::string& sender, const std::string& receiver,
                            const std::string& candidateStr,
                            const CallSession* session)
{
    const IceFields ice = parseIce(candidateStr);
    const std::string ts = clockMs();
    const std::string state = session ? session->state : "?";

    Log::debug("--- [CALL " + callId + "][ICE] [" + ts + "] ---");
    Log::debug("  from=" + sender + " -> to=" + receiver + " callState=" + state);
    Log::debug("  candidate: type=" + ice.type
               + " proto=" + ice.protocol
               + " ip=" + ice.address
               + " port=" + ice.port
               + " prio=" + ice.priority
               + " foundation=" + ice.foundation
               + (ice.ufrag.empty() ? "" : " ufrag=" + ice.ufrag));
}

// ============================================================
//  Call history persistence
// ============================================================

void ServerApp::persistCallRecord(const CallSession& session) {
    CallRecord record;
    record.callId      = session.callId;
    record.caller      = session.caller;
    record.callee      = session.callee;
    record.state       = session.state;
    record.createdAt   = session.createdAt;
    record.acceptedAt  = session.acceptedAt;
    record.connectedAt = session.connectedAt;
    record.endedAt     = session.endedAt;
    record.duration    = session.endedAt - session.createdAt;
    record.endReason   = session.endReason;
    // Run on worker thread to avoid blocking epoll
    if (workerPool) {
        workerPool->submit([this, record](DbManager& db) {
            db.saveCallRecord(record);
        });
    }
}

void ServerApp::handleCallConnected(std::uint64_t /*connectionId*/,
                                     const std::string& callId, std::int64_t nowMs)
{
    CallSession* cs = callManager.findByCallId(callId);
    if (!cs || cs->isEnded()) return;
    // Only allow connecting → connected
    if (cs->state == "connecting") {
        cs->state = "connected";
        cs->connectedAt = nowMs;
        Log::info("[CALL " + callId + "] state connecting -> connected");
    }
}

void ServerApp::handleCallHistory(std::uint64_t connectionId, const std::string& username)
{
    if (username.empty()) {
        sendTo(connectionId, makeResponse("call_history_resp", ResultCode::Failed, "join required"));
        return;
    }
    // Run on worker thread
    if (workerPool && workerPool->submit([this, connectionId, username](DbManager& db) {
            std::vector<CallRecord> records;
            db.loadCallRecords(username, 50, records);
            JsonValue arr;
            for (const auto& r : records) {
                JsonValue item;
                item.set("callId", JsonValue(r.callId));
                item.set("caller", JsonValue(r.caller));
                item.set("callee", JsonValue(r.callee));
                item.set("createdAt", JsonValue(static_cast<double>(r.createdAt)));
                item.set("acceptedAt", JsonValue(static_cast<double>(r.acceptedAt)));
                item.set("connectedAt", JsonValue(static_cast<double>(r.connectedAt)));
                item.set("endedAt", JsonValue(static_cast<double>(r.endedAt)));
                item.set("duration", JsonValue(static_cast<double>(r.duration)));
                item.set("endReason", JsonValue(r.endReason));
                // direction relative to requester
                std::string dir = (r.caller == username) ? "outgoing" : "incoming";
                item.set("direction", JsonValue(dir));
                std::string peer = (r.caller == username) ? r.callee : r.caller;
                item.set("peer", JsonValue(peer));
                arr.push(item);
            }
            JsonValue resp;
            resp.set("type", JsonValue("call_history_resp"));
            resp.set("code", JsonValue(static_cast<int>(ResultCode::Ok)));
            resp.set("records", arr);
            const std::string payload = resp.serialize();
            completionDispatcher->push([this, connectionId, payload] {
                sendTo(connectionId, payload);
            });
        })) return;
    sendTo(connectionId, makeResponse("call_history_resp", ResultCode::Failed, "server is stopping"));
}

// ============================================================
//  FD tracking
// ============================================================

void ServerApp::trackFdForUser(const std::string& username, int fd) {
    // fd tracking is now handled via connectionId in CallSession
    (void)username;
    (void)fd;
}

void ServerApp::checkFdStability(const std::string& user, int fd) {
    (void)user;
    (void)fd;
}

// ============================================================
//  handleMessage — main message dispatcher
// ============================================================

void ServerApp::handleMessage(std::uint64_t connectionId, const std::string& messageBody)
{
    Connection* connection = findConnection(connectionId);
    if (connection == nullptr) return;
    const int fd = connection->fd();
    const std::int64_t now = steadyNowMs();

    bool parseSucceeded = false;
    const JsonValue request = JsonValue::parse(messageBody, &parseSucceeded);
    if (!parseSucceeded || !request.isObject()) {
        sendTo(connectionId, makeResponse("error_resp", ResultCode::Failed, "bad json"));
        return;
    }
    const std::string type = request.get("type").asString();

    // --------------- join ---------------
    if (type == "join") {
        const std::string username = request.get("username").asString();
        // kickExisting=true: new login kicks old connection
        if (joinHandler.handleJoin(fd, username, true)) {
            Log::info("user joined: " + username + " fd=" + std::to_string(fd)
                      + " connId=" + std::to_string(connectionId));
            sendTo(connectionId, makeResponse("join_resp", ResultCode::Ok, "ok"));
            broadcastPresence(username, true);
        } else {
            sendTo(connectionId, makeResponse("join_resp", ResultCode::JoinRejected,
                                               "username empty or already bound to this connection"));
        }
        return;
    }

    // --------------- heartbeat ---------------
    if (type == "heartbeat") {
        joinHandler.handleHeartbeat(fd);
        sendTo(connectionId, makeResponse("heartbeat_resp", ResultCode::Ok, "ok"));
        // Check ringing timeouts
        checkRingingTimeouts(now);
        return;
    }

    // --------------- leave ---------------
    if (type == "leave") {
        const std::string username = joinHandler.usernameOf(fd);
        if (username.empty()) {
            sendTo(connectionId, makeResponse("leave_resp", ResultCode::Failed, "join required"));
            return;
        }
        cleanupCallsForUser(username, now, "disconnected");
        joinHandler.removeConnection(fd);
        sendTo(connectionId, makeResponse("leave_resp", ResultCode::Ok, "ok"));
        broadcastPresence(username, false);
        Log::info("user left: " + username + " fd=" + std::to_string(fd));
        // Persist loginlog as offline
        if (workerPool) {
            workerPool->submit([username](DbManager& db) {
                db.setOffline(username);
            });
        }
        return;
    }

    // --------------- auth / chat / contacts ---------------
    if (type == "register") {
        const std::string username = request.get("username").asString();
        const std::string email = request.get("email").asString();
        const std::string password = request.get("password").asString();
        const std::string nickname = request.get("nickname").asString();
        const int avatarSeed = static_cast<int>(request.get("avatarSeed").asNumber());
        submitDatabaseTask(connectionId, [this, connectionId, username, email, password,
                                          nickname, avatarSeed](DbManager& db) {
            RegisterHandler handler(&db);
            const ResultCode code = handler.handleRegister(username, email, password,
                                                           nickname, avatarSeed);
            std::string message = "failed";
            if (code == ResultCode::Ok) message = "ok";
            else if (code == ResultCode::UserExists) message = "username already exists";
            else if (code == ResultCode::InvalidEmail) message = "invalid email";
            else if (code == ResultCode::InvalidPassword) message = "invalid password";
            completionDispatcher->push([this, connectionId, code, message] {
                sendTo(connectionId, makeResponse("register_resp", code, message));
            });
        });
        return;
    }
    if (type == "login") {
        const std::string username = request.get("username").asString();
        const std::string password = request.get("password").asString();
        submitDatabaseTask(connectionId, [this, connectionId, username, password](DbManager& db) {
            LoginHandler handler(&db);
            const ResultCode code = handler.handleLogin(username, password);
            JsonValue response;
            response.set("type", JsonValue("login_resp"));
            response.set("code", JsonValue(static_cast<int>(code)));
            response.set("success", JsonValue(code == ResultCode::Ok));
            std::string msg;
            switch (code) {
                case ResultCode::Ok:                    msg = "ok"; break;
                case ResultCode::UserNotFound:          msg = "user not found"; break;
                case ResultCode::WrongPassword:         msg = "wrong password"; break;
                case ResultCode::AccountAlreadyLoggedIn:msg = "该账户在异地登录。"; break;
                default:                                msg = "failed"; break;
            }
            response.set("msg", JsonValue(msg));
            if (code == ResultCode::Ok)
                response.set("username", JsonValue(username));
            const std::string payload = response.serialize();
            completionDispatcher->push([this, connectionId, payload] {
                sendTo(connectionId, payload);
            });
        });
        return;
    }
    if (type == "forgot") {
        const std::string username = request.get("username").asString();
        const std::string newPassword = request.get("newPassword").asString();
        submitDatabaseTask(connectionId, [this, connectionId, username, newPassword](DbManager& db) {
            ForgotHandler handler(&db);
            const ResultCode code = handler.handleForgot(username, newPassword);
            std::string message = "failed";
            if (code == ResultCode::Ok) message = "ok";
            else if (code == ResultCode::UserNotFound) message = "user not found";
            else if (code == ResultCode::InvalidPassword) message = "invalid password";
            completionDispatcher->push([this, connectionId, code, message] {
                sendTo(connectionId, makeResponse("forgot_resp", code, message));
            });
        });
        return;
    }

    const std::string currentUsername = joinHandler.usernameOf(fd);
    if (type == "chat") {
        const std::string receiver = request.get("to").asString();
        const std::string content = request.get("content").asString();
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername, receiver,
                                          content](DbManager& db) {
            ChatMessage message;
            message.sender = currentUsername;
            message.receiver = receiver;
            message.content = content;
            const bool succeeded = !currentUsername.empty() && !receiver.empty()
                && !content.empty() && db.saveMessage(message);
            completionDispatcher->push([this, connectionId, receiver, message, succeeded] {
                if (!succeeded) {
                    sendTo(connectionId, makeResponse("chat_resp", ResultCode::Failed,
                                                       "join required or invalid message"));
                    return;
                }
                const int receiverFd = joinHandler.fdOf(receiver);
                JsonValue response;
                response.set("type", JsonValue("chat_resp"));
                response.set("code", JsonValue(static_cast<int>(ResultCode::Ok)));
                response.set("msg", JsonValue("stored"));
                response.set("online", JsonValue(receiverFd >= 0));
                response.set("message", messageToJson(message));
                const auto receiverIterator = connections.find(receiverFd);
                const std::uint64_t receiverConnectionId = receiverIterator == connections.end()
                    ? 0 : receiverIterator->second->id();
                sendTo(connectionId, response.serialize());

                if (receiverConnectionId != 0 && receiverConnectionId != connectionId) {
                    JsonValue push;
                    push.set("type", JsonValue("chat_push"));
                    push.set("message", messageToJson(message));
                    sendTo(receiverConnectionId, push.serialize());
                }
            });
        });
        return;
    }
    if (type == "history") {
        const std::string peer = request.get("peer").asString();
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername, peer](DbManager& db) {
            std::vector<ChatMessage> messages;
            const bool succeeded = !currentUsername.empty() && !peer.empty()
                && db.loadConversation(currentUsername, peer, messages);
            JsonValue response;
            response.set("type", JsonValue("history_resp"));
            response.set("code", JsonValue(static_cast<int>(succeeded ? ResultCode::Ok : ResultCode::Failed)));
            response.set("peer", JsonValue(peer));
            response.set("msg", JsonValue(succeeded ? "ok" : "join required or invalid peer"));
            JsonValue messageArray;
            if (succeeded) {
                for (const ChatMessage& message : messages)
                    messageArray.push(messageToJson(message));
            }
            response.set("messages", messageArray);
            const std::string payload = response.serialize();
            completionDispatcher->push([this, connectionId, payload] {
                sendTo(connectionId, payload);
            });
        });
        return;
    }
    if (type == "add_contact") {
        const std::string contactUsername = request.get("username").asString();
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername,
                                          contactUsername](DbManager& db) {
            std::string passwordHash;
            const bool succeeded = !currentUsername.empty()
                && db.findUser(contactUsername, passwordHash)
                && db.addContact(currentUsername, contactUsername);
            completionDispatcher->push([this, connectionId, succeeded] {
                sendTo(connectionId, makeResponse("add_contact_resp",
                    succeeded ? ResultCode::Ok : ResultCode::Failed,
                    succeeded ? "ok" : "invalid contact"));
            });
        });
        return;
    }
    if (type == "contacts") {
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername](DbManager& db) {
            std::vector<ContactProfile> contacts;
            const bool succeeded = !currentUsername.empty()
                && db.loadContacts(currentUsername, contacts);
            completionDispatcher->push([this, connectionId, contacts = std::move(contacts),
                                         succeeded]() mutable {
                if (!succeeded) {
                    sendTo(connectionId, makeResponse("contacts_resp", ResultCode::Failed,
                                                       "join required"));
                    return;
                }
                JsonValue response;
                response.set("type", JsonValue("contacts_resp"));
                response.set("code", JsonValue(static_cast<int>(ResultCode::Ok)));
                JsonValue contactArray;
                for (const ContactProfile& contact : contacts) {
                    JsonValue contactJson;
                    contactJson.set("username", JsonValue(contact.username));
                    contactJson.set("nickname", JsonValue(contact.nickname.empty()
                                                             ? contact.username : contact.nickname));
                    contactJson.set("avatarSeed", JsonValue(contact.avatarSeed));
                    contactJson.set("online", JsonValue(joinHandler.fdOf(contact.username) >= 0));
                    contactArray.push(contactJson);
                }
                response.set("contacts", contactArray);
                sendTo(connectionId, response.serialize());
            });
        });
        return;
    }
    if (type == "friend_request") {
        const std::string receiverUsername = request.get("username").asString();
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername,
                                          receiverUsername](DbManager& db) {
            std::string passwordHash;
            const bool succeeded = !currentUsername.empty()
                && db.findUser(receiverUsername, passwordHash)
                && db.createFriendRequest(currentUsername, receiverUsername);
            FriendRequest pendingRequest;
            if (succeeded) {
                std::vector<FriendRequest> requests;
                if (db.loadPendingFriendRequests(receiverUsername, requests)) {
                    for (const FriendRequest& item : requests) {
                        if (item.senderUsername == currentUsername) {
                            pendingRequest = item;
                            break;
                        }
                    }
                }
            }
            completionDispatcher->push([this, connectionId, receiverUsername,
                                         pendingRequest, succeeded] {
                sendTo(connectionId, makeResponse("friend_request_resp",
                    succeeded ? ResultCode::Ok : ResultCode::Failed,
                    succeeded ? "request sent" : "invalid or duplicate request"));
                if (!succeeded || pendingRequest.senderUsername.empty()) return;
                const int receiverFd = joinHandler.fdOf(receiverUsername);
                const auto receiverIterator = connections.find(receiverFd);
                if (receiverIterator != connections.end()) {
                    const std::uint64_t receiverConnectionId = receiverIterator->second->id();
                    JsonValue push;
                    push.set("type", JsonValue("friend_request_push"));
                    push.set("request", friendRequestToJson(pendingRequest));
                    sendTo(receiverConnectionId, push.serialize());
                }
            });
        });
        return;
    }
    if (type == "friend_requests") {
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername](DbManager& db) {
            std::vector<FriendRequest> requests;
            const bool succeeded = !currentUsername.empty()
                && db.loadPendingFriendRequests(currentUsername, requests);
            JsonValue response;
            response.set("type", JsonValue("friend_requests_resp"));
            response.set("code", JsonValue(static_cast<int>(succeeded ? ResultCode::Ok : ResultCode::Failed)));
            response.set("msg", JsonValue(succeeded ? "ok" : "join required"));
            JsonValue requestArray;
            if (succeeded) {
                for (const FriendRequest& friendRequest : requests)
                    requestArray.push(friendRequestToJson(friendRequest));
            }
            response.set("requests", requestArray);
            const std::string payload = response.serialize();
            completionDispatcher->push([this, connectionId, payload] {
                sendTo(connectionId, payload);
            });
        });
        return;
    }
    if (type == "friend_request_response") {
        const std::string senderUsername = request.get("sender").asString();
        const bool accepted = request.get("accepted").asBool();
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername,
                                          senderUsername, accepted](DbManager& db) {
            const bool succeeded = db.respondToFriendRequest(currentUsername,
                                                             senderUsername, accepted);
            completionDispatcher->push([this, connectionId, currentUsername,
                                         senderUsername, accepted, succeeded] {
                sendTo(connectionId, makeResponse("friend_request_response_resp",
                    succeeded ? ResultCode::Ok : ResultCode::Failed,
                    succeeded ? "ok" : "request not found"));
                if (!succeeded || !accepted) return;
                const int senderFd = joinHandler.fdOf(senderUsername);
                const auto senderIterator = connections.find(senderFd);
                if (senderIterator != connections.end()) {
                    const std::uint64_t senderConnectionId = senderIterator->second->id();
                    JsonValue push;
                    push.set("type", JsonValue("friend_accepted_push"));
                    push.set("username", JsonValue(currentUsername));
                    sendTo(senderConnectionId, push.serialize());
                }
            });
        });
        return;
    }
    if (type == "delete_chat") {
        const std::string peer = request.get("peer").asString();
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername, peer](DbManager& db) {
            const bool succeeded = !currentUsername.empty() && !peer.empty()
                && db.deleteConversation(currentUsername, peer);
            JsonValue response;
            response.set("type", JsonValue("delete_chat_resp"));
            response.set("code", JsonValue(static_cast<int>(succeeded ? ResultCode::Ok : ResultCode::Failed)));
            response.set("msg", JsonValue(succeeded ? "ok" : "join required or invalid peer"));
            response.set("peer", JsonValue(peer));
            const std::string payload = response.serialize();
            completionDispatcher->push([this, connectionId, payload] {
                sendTo(connectionId, payload);
            });
        });
        return;
    }
    if (type == "clear_chats") {
        submitDatabaseTask(connectionId, [this, connectionId, currentUsername](DbManager& db) {
            const bool succeeded = !currentUsername.empty() && db.deleteAllMessages(currentUsername);
            completionDispatcher->push([this, connectionId, succeeded] {
                sendTo(connectionId, makeResponse("clear_chats_resp",
                    succeeded ? ResultCode::Ok : ResultCode::Failed,
                    succeeded ? "ok" : "join required"));
            });
        });
        return;
    }

    // --------------- call_connected ---------------
    if (type == "call_connected") {
        const std::string callId = request.get("callId").asString();
        handleCallConnected(connectionId, callId, now);
        sendTo(connectionId, makeResponse("call_connected_resp", ResultCode::Ok, "ok"));
        return;
    }

    // --------------- call_history ---------------
    if (type == "call_history") {
        handleCallHistory(connectionId, currentUsername);
        return;
    }

    // ============================================================
    //  WebRTC signaling — with CallSession validation
    // ============================================================

    if (type == "call_request" || type == "call_accept" || type == "call_reject"
        || type == "call_cancel" || type == "call_hangup" || type == "webrtc_offer"
        || type == "webrtc_answer" || type == "ice_candidate") {

        const std::string receiver = request.get("to").asString();
        const int receiverFd = joinHandler.fdOf(receiver);
        const auto receiverIt = connections.find(receiverFd);
        const bool peerOnline = !currentUsername.empty() && !receiver.empty()
            && receiver != currentUsername && receiverIt != connections.end();
        const std::uint64_t receiverConnId = peerOnline
            ? receiverIt->second->id() : 0;

        // --- call_request: create session ---
        if (type == "call_request") {
            // check busy
            if (busyTracker.isBusy(currentUsername)) {
                Log::debug("[CALL ?][SIGNAL] call_request from=" + currentUsername
                          + " IGNORED reason=caller_busy");
                sendTo(connectionId, makeResponse("call_signal_resp", ResultCode::Failed,
                                                   "caller is busy"));
                return;
            }
            if (busyTracker.isBusy(receiver)) {
                Log::debug("[CALL ?][SIGNAL] call_request to=" + receiver
                          + " IGNORED reason=callee_busy");
                sendTo(connectionId, makeResponse("call_signal_resp", ResultCode::UserBusy,
                                                   "user is busy"));
                return;
            }
            if (!peerOnline) {
                sendTo(connectionId, makeResponse("call_signal_resp", ResultCode::UserOffline,
                                                   "peer is offline"));
                return;
            }

            const std::string newCallId = generateCallId();
            CallSession* cs = callManager.create(newCallId,
                                                  currentUsername, connectionId,
                                                  receiver, receiverConnId,
                                                  now);
            if (!cs) {
                sendTo(connectionId, makeResponse("call_signal_resp", ResultCode::Failed,
                                                   "cannot create call"));
                return;
            }
            busyTracker.setBusy(currentUsername, newCallId);
            busyTracker.setBusy(receiver, newCallId);

            Log::info("=== NEW CALL SESSION ===");
            Log::info("[CALL " + newCallId + "] caller=" + currentUsername
                      + " connId=" + std::to_string(connectionId)
                      + " callee=" + receiver
                      + " connId=" + std::to_string(receiverConnId));

            // forward to callee
            JsonValue relay;
            relay.set("type", JsonValue(type));
            relay.set("from", JsonValue(currentUsername));
            relay.set("callId", JsonValue(newCallId));
            relay.set("sdp", JsonValue(request.get("sdp").asString()));
            sendTo(receiverConnId, relay.serialize());

            // ack to caller
            JsonValue ack;
            ack.set("type", JsonValue("call_signal_resp"));
            ack.set("signalType", JsonValue(type));
            ack.set("callId", JsonValue(newCallId));
            ack.set("code", JsonValue(static_cast<int>(ResultCode::Ok)));
            ack.set("msg", JsonValue("sent"));
            sendTo(connectionId, ack.serialize());
            return;
        }

        // --- all other signals need a callId ---
        // derive callId from sender/receiver pair
        const std::string callId = callIdForPair(currentUsername, receiver);
        const CallSession* session = callManager.findByCallId(callId);

        // Validate
        SignalResult vr = validateCallSignal(type, callId, currentUsername, receiver, session);

        if (vr == SignalResult::CallNotFound || vr == SignalResult::StaleCallIgnored) {
            Log::debug("[CALL " + callId + "][SIGNAL] type=" + type
                       + " from=" + currentUsername
                       + " IGNORED reason="
                       + (vr == SignalResult::StaleCallIgnored ? "STALE_CALL" : "CALL_NOT_FOUND"));
            sendTo(connectionId, makeResponse("call_signal_resp", ResultCode::CallNotFound,
                                               vr == SignalResult::StaleCallIgnored
                                               ? "call already ended" : "call not found"));
            return;
        }

        if (vr == SignalResult::NotParticipant) {
            Log::warn("[CALL " + callId + "][SIGNAL] type=" + type
                      + " from=" + currentUsername
                      + " IGNORED reason=NOT_PARTICIPANT");
            sendTo(connectionId, makeResponse("call_signal_resp", ResultCode::NotCallParticipant,
                                               "not a call participant"));
            return;
        }

        if (vr == SignalResult::WrongState) {
            Log::debug("[CALL " + callId + "][SIGNAL] type=" + type
                       + " from=" + currentUsername
                       + " IGNORED reason=WRONG_STATE current="
                       + (session ? session->state : "?"));
            sendTo(connectionId, makeResponse("call_signal_resp", ResultCode::InvalidCallState,
                                               "invalid state for this operation"));
            return;
        }

        // --- stateful handling ---
        const std::string& state = session ? session->state : "";

        if (type == "call_accept") {
            // Atomic: ringing → connecting, cancel ringing timeout.
            // Idempotent: duplicate accept on already-connecting returns false (safe).
            callManager.accept(callId, now);
            busyTracker.setBusy(currentUsername, callId);
            busyTracker.setBusy(receiver, callId);
            // Re-read state after transition
            const CallSession* updated = callManager.findByCallId(callId);
            Log::info("[CALL " + callId + "][SIGNAL] call_accept from=" + currentUsername
                      + " to=" + receiver
                      + " state=" + (updated ? updated->state : "?")
                      + " generation=" + std::to_string(updated ? updated->generation : -1)
                      + " forwarded=yes");
        }

        if (type == "call_reject") {
            callManager.end(callId, "rejected", now);
            busyTracker.release(currentUsername);
            busyTracker.release(receiver);
            Log::info("[CALL " + callId + "][SIGNAL] call_reject from=" + currentUsername
                      + " to=" + receiver + " state=ended forwarded=yes");
        }

        if (type == "call_cancel") {
            callManager.end(callId, "cancelled", now);
            busyTracker.release(currentUsername);
            busyTracker.release(receiver);
            Log::info("[CALL " + callId + "][SIGNAL] call_cancel from=" + currentUsername
                      + " to=" + receiver + " state=ended forwarded=yes");
        }

        if (type == "call_hangup") {
            callManager.end(callId, "completed", now);
            busyTracker.release(currentUsername);
            busyTracker.release(receiver);
            Log::info("[CALL " + callId + "][SIGNAL] call_hangup from=" + currentUsername
                      + " to=" + receiver + " state=ended forwarded=yes");
        }

        if (type == "webrtc_offer") {
            Log::info("[CALL " + callId + "][SIGNAL] webrtc_offer from=" + currentUsername
                      + " to=" + receiver + " state=" + state + " forwarded=yes");
            logSdpDiag(callId, "SDP OFFER", currentUsername, receiver,
                       request.get("sdp").asString());
        }

        if (type == "webrtc_answer") {
            Log::info("[CALL " + callId + "][SIGNAL] webrtc_answer from=" + currentUsername
                      + " to=" + receiver + " state=" + state + " forwarded=yes");
            logSdpDiag(callId, "SDP ANSWER", currentUsername, receiver,
                       request.get("sdp").asString());
        }

        if (type == "ice_candidate") {
            logIceDiag(callId, currentUsername, receiver,
                       request.get("candidate").asString(), session);
        }

        // --- relay response ---
        JsonValue response;
        response.set("type", JsonValue("call_signal_resp"));
        response.set("signalType", JsonValue(type));
        response.set("callId", JsonValue(callId));
        response.set("code", JsonValue(static_cast<int>(peerOnline ? ResultCode::Ok : ResultCode::Failed)));
        response.set("msg", JsonValue(peerOnline ? "relayed" : "peer is offline"));
        sendTo(connectionId, response.serialize());

        if (!peerOnline) return;

        // --- forward to peer ---
        JsonValue relay;
        relay.set("type", JsonValue(type));
        relay.set("from", JsonValue(currentUsername));
        relay.set("callId", JsonValue(callId));
        relay.set("sdp", JsonValue(request.get("sdp").asString()));
        relay.set("candidate", JsonValue(request.get("candidate").asString()));
        relay.set("sdpMid", JsonValue(request.get("sdpMid").asString()));
        relay.set("sdpMLineIndex",
                  JsonValue(static_cast<int>(request.get("sdpMLineIndex").asNumber())));
        sendTo(receiverConnId, relay.serialize());
        return;
    }

    sendTo(connectionId, makeResponse("error_resp", ResultCode::Failed, "unknown type"));
}
