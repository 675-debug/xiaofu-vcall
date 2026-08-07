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

#include <functional>
#include <utility>
#include <vector>

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

} // namespace

ServerApp::ServerApp(std::string databasePath, std::size_t workerCount)
    : databasePath(std::move(databasePath)),
      configuredWorkerCount(workerCount),
      joinHandler([this](int fd) {
          Log::info("kick timeout fd=" + std::to_string(fd));
          removeConnection(fd);
      })
{
}

ServerApp::~ServerApp()
{
    // 先等待数据库任务退出，避免工作线程继续向已销毁的完成队列投递结果。
    workerPool.reset();
    heartbeatManager.reset();
    tcpServer.reset();
    connections.clear();
    completionDispatcher.reset();
    shutdownSocketPlatform();
}

bool ServerApp::start(const std::string& listenAddress, int port)
{
    DbManager setupDatabase;
    if (!setupDatabase.open(databasePath) || !setupDatabase.createTables()) {
        Log::error("server startup aborted: cannot prepare database " + databasePath);
        return false;
    }
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

void ServerApp::run()
{
    eventLoop.run();
}

void ServerApp::stop()
{
    eventLoop.stop();
}

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
    if (iterator == connections.end())
        return;

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
    if (current == connections.end())
        return;
    if (current->second->closed())
        removeConnection(fd);
    else
        refreshConnectionInterest(fd);
}

void ServerApp::removeConnection(int fd)
{
    const auto iterator = connections.find(fd);
    if (iterator == connections.end())
        return;

    const std::string username = joinHandler.usernameOf(fd);
    const std::uint64_t connectionId = iterator->second->id();
    joinHandler.removeConnection(fd);
    eventLoop.removeFd(fd);
    fdByConnectionId.erase(connectionId);
    connections.erase(iterator);
    Log::info("client disconnected fd=" + std::to_string(fd));
    if (!username.empty())
        broadcastPresence(username, false);
}

void ServerApp::refreshConnectionInterest(int fd)
{
    const auto iterator = connections.find(fd);
    if (iterator == connections.end() || iterator->second->closed())
        return;
    std::uint32_t interests = EventLoop::Read;
    if (iterator->second->hasPendingOutput())
        interests |= EventLoop::Write;
    eventLoop.updateFd(fd, interests);
}

Connection* ServerApp::findConnection(std::uint64_t connectionId)
{
    const auto fdIterator = fdByConnectionId.find(connectionId);
    if (fdIterator == fdByConnectionId.end())
        return nullptr;
    const auto connectionIterator = connections.find(fdIterator->second);
    return connectionIterator == connections.end() ? nullptr : connectionIterator->second.get();
}

void ServerApp::sendTo(std::uint64_t connectionId, const std::string& payload)
{
    Connection* connection = findConnection(connectionId);
    if (connection == nullptr)
        return;
    const int fd = connection->fd();
    connection->sendMessage(payload);
    if (connection->closed()) {
        if (dispatchingFd != fd)
            removeConnection(fd);
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

void ServerApp::handleMessage(std::uint64_t connectionId, const std::string& messageBody)
{
    Connection* connection = findConnection(connectionId);
    if (connection == nullptr)
        return;
    const int fd = connection->fd();

    bool parseSucceeded = false;
    const JsonValue request = JsonValue::parse(messageBody, &parseSucceeded);
    if (!parseSucceeded || !request.isObject()) {
        sendTo(connectionId, makeResponse("error_resp", ResultCode::Failed, "bad json"));
        return;
    }
    const std::string type = request.get("type").asString();

    if (type == "join") {
        const std::string username = request.get("username").asString();
        if (joinHandler.handleJoin(fd, username)) {
            Log::info("user joined: " + username + " fd=" + std::to_string(fd));
            sendTo(connectionId, makeResponse("join_resp", ResultCode::Ok, "ok"));
            broadcastPresence(username, true);
        } else {
            sendTo(connectionId, makeResponse("join_resp", ResultCode::JoinRejected,
                                               "username empty or already online"));
        }
        return;
    }
    if (type == "heartbeat") {
        joinHandler.handleHeartbeat(fd);
        sendTo(connectionId, makeResponse("heartbeat_resp", ResultCode::Ok, "ok"));
        return;
    }
    if (type == "leave") {
        const std::string username = joinHandler.usernameOf(fd);
        if (username.empty()) {
            sendTo(connectionId, makeResponse("leave_resp", ResultCode::Failed, "join required"));
            return;
        }
        joinHandler.removeConnection(fd);
        sendTo(connectionId, makeResponse("leave_resp", ResultCode::Ok, "ok"));
        broadcastPresence(username, false);
        Log::info("user left: " + username + " fd=" + std::to_string(fd));
        return;
    }

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
            response.set("msg", JsonValue(code == ResultCode::Ok ? "ok" : "failed"));
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
            response.set("code", JsonValue(static_cast<int>(succeeded ? ResultCode::Ok
                                                                       : ResultCode::Failed)));
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
                if (!succeeded || pendingRequest.senderUsername.empty())
                    return;
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
            response.set("code", JsonValue(static_cast<int>(succeeded ? ResultCode::Ok
                                                                       : ResultCode::Failed)));
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
                if (!succeeded || !accepted)
                    return;
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
            response.set("code", JsonValue(static_cast<int>(succeeded ? ResultCode::Ok
                                                                       : ResultCode::Failed)));
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

    if (type == "call_request" || type == "call_accept" || type == "call_reject"
        || type == "call_hangup" || type == "webrtc_offer" || type == "webrtc_answer"
        || type == "ice_candidate") {
        // 服务器只转发 WebRTC 信令；摄像头采集、编码、解码和媒体流都在客户端。
        const std::string receiver = request.get("to").asString();
        const int receiverFd = joinHandler.fdOf(receiver);
        const auto receiverIterator = connections.find(receiverFd);
        const bool canRelay = !currentUsername.empty() && !receiver.empty()
            && receiver != currentUsername && receiverIterator != connections.end();
        const std::uint64_t receiverConnectionId = canRelay
            ? receiverIterator->second->id() : 0;

        JsonValue response;
        response.set("type", JsonValue("call_signal_resp"));
        response.set("signalType", JsonValue(type));
        response.set("code", JsonValue(static_cast<int>(canRelay ? ResultCode::Ok
                                                                  : ResultCode::Failed)));
        response.set("msg", JsonValue(canRelay ? "relayed" : "peer is offline or invalid"));
        sendTo(connectionId, response.serialize());
        if (!canRelay)
            return;

        JsonValue relay;
        relay.set("type", JsonValue(type));
        relay.set("from", JsonValue(currentUsername));
        relay.set("sdp", JsonValue(request.get("sdp").asString()));
        relay.set("candidate", JsonValue(request.get("candidate").asString()));
        relay.set("sdpMid", JsonValue(request.get("sdpMid").asString()));
        relay.set("sdpMLineIndex",
                  JsonValue(static_cast<int>(request.get("sdpMLineIndex").asNumber())));
        sendTo(receiverConnectionId, relay.serialize());
        Log::info("call signal relayed: " + type + " " + currentUsername + " -> " + receiver);
        return;
    }

    sendTo(connectionId, makeResponse("error_resp", ResultCode::Failed, "unknown type"));
}
