#include <winsock2.h>
#include <functional>
#include <map>
#include <string>
#include <windows.h>

#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "net/Connection.h"
#include "protocol/JsonValue.h"
#include "protocol/ResultCode.h"
#include "handler/JoinHandler.h"
#include "handler/LoginHandler.h"
#include "handler/RegisterHandler.h"
#include "handler/ForgotHandler.h"
#include "handler/ChatHandler.h"
#include "heartbeat/HeartbeatManager.h"
#include "db/DbManager.h"
#include "util/Log.h"

int main()
{
    // 服务端启动流程：准备数据库、网络事件循环和各类请求处理器。
    // 数据库统一放在项目根目录的 db/ 下：从 exe 位置向上找项目根（server/cmake-build-debug -> server -> 项目根）
    char executablePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    std::string databaseDirectory = std::string(executablePath);
    size_t separatorPosition = databaseDirectory.find_last_of("\\/");
    if (separatorPosition != std::string::npos)
        databaseDirectory = databaseDirectory.substr(0, separatorPosition);
    separatorPosition = databaseDirectory.find_last_of("\\/");
    if (separatorPosition != std::string::npos)
        databaseDirectory = databaseDirectory.substr(0, separatorPosition);
    separatorPosition = databaseDirectory.find_last_of("\\/");
    if (separatorPosition != std::string::npos)
        databaseDirectory = databaseDirectory.substr(0, separatorPosition);
    databaseDirectory += "/db";
    CreateDirectoryA(databaseDirectory.c_str(), nullptr);

    EventLoop* loop = createEventLoop();

    DbManager db;
    if (!db.open(databaseDirectory + "/xiaofu.db"))
    {
        Log::error("server startup aborted: cannot open database " + databaseDirectory + "/xiaofu.db");
        return 1;
    }
    Log::info("database loaded: " + databaseDirectory + "/xiaofu.db");
    if (!db.createTables())
    {
        Log::error("server startup aborted: cannot create database tables");
        return 1;
    }
    Log::info("database tables ready");

    LoginHandler loginHandler(&db);
    RegisterHandler registerHandler(&db);
    ForgotHandler forgotHandler(&db);

    std::map<int, Connection*> connections;

    auto removeConnection = [&](int fd)
    {
        auto connectionIterator = connections.find(fd);
        if (connectionIterator != connections.end())
        {
            loop->removeFd(fd);
            delete connectionIterator->second;
            connections.erase(connectionIterator);
        }
    };

    JoinHandler joinHandler([&](int fd)
    {
        Log::info("kick timeout fd=" + std::to_string(fd));
        auto connectionIterator = connections.find(fd);
        if (connectionIterator != connections.end())
        {
            connectionIterator->second->close();
            removeConnection(fd);
        }
    });
    ChatHandler chatHandler(&db, &joinHandler);

    auto broadcastPresence = [&](const std::string& username, bool online)
    {
        JsonValue presence;
        presence.set("type", JsonValue("presence_push"));
        presence.set("username", JsonValue(username));
        presence.set("online", JsonValue(online));
        const std::string payload = presence.serialize();
        for (const auto& entry : connections)
        {
            if (!joinHandler.usernameOf(entry.first).empty())
                entry.second->sendMessage(payload);
        }
    };

    HeartbeatManager heartbeat(loop, &joinHandler, 15000, 50000);

    auto makeResponse = [](const std::string& type, ResultCode code, const std::string& message)
    {
        JsonValue response;
        response.set("type", JsonValue(type));
        response.set("code", JsonValue(static_cast<int>(code)));
        response.set("msg", JsonValue(message));
        return response.serialize();
    };

    auto messageToJson = [](const ChatMessage& message)
    {
        JsonValue jsonMessage;
        jsonMessage.set("id", JsonValue(static_cast<int>(message.id)));
        jsonMessage.set("from", JsonValue(message.sender));
        jsonMessage.set("to", JsonValue(message.receiver));
        jsonMessage.set("content", JsonValue(message.content));
        jsonMessage.set("sentAt", JsonValue(message.sentAt));
        return jsonMessage;
    };

    auto friendRequestToJson = [](const FriendRequest& request)
    {
        JsonValue jsonRequest;
        jsonRequest.set("sender", JsonValue(request.senderUsername));
        jsonRequest.set("nickname", JsonValue(request.nickname));
        jsonRequest.set("avatarSeed", JsonValue(request.avatarSeed));
        jsonRequest.set("createdAt", JsonValue(request.createdAt));
        return jsonRequest;
    };

    std::function<void(Connection*, const std::string&)> handleMessage;
    handleMessage = [&](Connection* connection, const std::string& messageBody)
    {
        bool parseSucceeded = false;
        JsonValue request = JsonValue::parse(messageBody, &parseSucceeded);
        if (!parseSucceeded || !request.isObject())
        {
            connection->sendMessage(makeResponse("error_resp", ResultCode::Failed, "bad json"));
            return;
        }
        const std::string type = request.get("type").asString();

        if (type == "join")
        {
            const std::string username = request.get("username").asString();
            if (joinHandler.handleJoin(connection->fd(), username))
            {
                Log::info("user joined: " + username + " fd=" + std::to_string(connection->fd()));
                connection->sendMessage(makeResponse("join_resp", ResultCode::Ok, "ok"));
                broadcastPresence(username, true);
            }
            else
            {
                connection->sendMessage(makeResponse("join_resp", ResultCode::JoinRejected,
                                                     "username empty or already online"));
            }
        }
        else if (type == "heartbeat")
        {
            joinHandler.handleHeartbeat(connection->fd());
            Log::info("heartbeat received: " + joinHandler.usernameOf(connection->fd()));
            connection->sendMessage(makeResponse("heartbeat_resp", ResultCode::Ok, "ok"));
        }
        else if (type == "leave")
        {
            // 主动退出时立即移除在线会话，避免切换账号后留下旧连接。
            const std::string username = joinHandler.usernameOf(connection->fd());
            if (username.empty())
            {
                connection->sendMessage(makeResponse("leave_resp", ResultCode::Failed, "join required"));
                return;
            }
            joinHandler.removeConnection(connection->fd());
            connection->sendMessage(makeResponse("leave_resp", ResultCode::Ok, "ok"));
            broadcastPresence(username, false);
            Log::info("user left: " + username + " fd=" + std::to_string(connection->fd()));
        }
        else if (type == "register")
        {
            const std::string username = request.get("username").asString();
            const std::string email = request.get("email").asString();
            const std::string password = request.get("password").asString();
            const std::string nickname = request.get("nickname").asString();
            const int avatarSeed = static_cast<int>(request.get("avatarSeed").asNumber());
            const ResultCode code = registerHandler.handleRegister(username, email, password, nickname, avatarSeed);
            std::string responseMessage = "failed";
            if (code == ResultCode::Ok) responseMessage = "ok";
            else if (code == ResultCode::UserExists) responseMessage = "username already exists";
            else if (code == ResultCode::InvalidEmail) responseMessage = "invalid email";
            else if (code == ResultCode::InvalidPassword) responseMessage = "invalid password";
            connection->sendMessage(makeResponse("register_resp", code, responseMessage));
        }
        else if (type == "login")
        {
            const std::string username = request.get("username").asString();
            const std::string password = request.get("password").asString();
            const ResultCode code = loginHandler.handleLogin(username, password);
            JsonValue response;
            response.set("type", JsonValue("login_resp"));
            response.set("code", JsonValue(static_cast<int>(code)));
            response.set("msg", JsonValue(code == ResultCode::Ok ? "ok" : "failed"));
            if (code == ResultCode::Ok) response.set("username", JsonValue(username));
            connection->sendMessage(response.serialize());
        }
        else if (type == "forgot")
        {
            const std::string username = request.get("username").asString();
            const std::string newPassword = request.get("newPassword").asString();
            const ResultCode code = forgotHandler.handleForgot(username, newPassword);
            std::string responseMessage = "failed";
            if (code == ResultCode::Ok) responseMessage = "ok";
            else if (code == ResultCode::UserNotFound) responseMessage = "user not found";
            else if (code == ResultCode::InvalidPassword) responseMessage = "invalid password";
            connection->sendMessage(makeResponse("forgot_resp", code, responseMessage));
        }
        else if (type == "chat")
        {
            // 聊天发送者由当前连接的 join 身份决定，客户端不能伪造 from 字段。
            const std::string sender = joinHandler.usernameOf(connection->fd());
            const std::string receiver = request.get("to").asString();
            const std::string content = request.get("content").asString();
            ChatMessage message;
            if (!chatHandler.createMessage(sender, receiver, content, message))
            {
                connection->sendMessage(makeResponse("chat_resp", ResultCode::Failed,
                                                     "join required or invalid message"));
                return;
            }

            const int receiverFd = chatHandler.receiverFd(receiver);
            JsonValue response;
            response.set("type", JsonValue("chat_resp"));
            response.set("code", JsonValue(static_cast<int>(ResultCode::Ok)));
            response.set("msg", JsonValue("stored"));
            response.set("online", JsonValue(receiverFd >= 0));
            response.set("message", messageToJson(message));
            connection->sendMessage(response.serialize());

            const auto receiverIterator = connections.find(receiverFd);
            if (receiverIterator != connections.end() && receiverFd != connection->fd())
            {
                JsonValue push;
                push.set("type", JsonValue("chat_push"));
                push.set("message", messageToJson(message));
                receiverIterator->second->sendMessage(push.serialize());
            }
        }
        else if (type == "history")
        {
            const std::string username = joinHandler.usernameOf(connection->fd());
            const std::string peer = request.get("peer").asString();
            std::vector<ChatMessage> messages;
            if (!chatHandler.loadHistory(username, peer, messages))
            {
                connection->sendMessage(makeResponse("history_resp", ResultCode::Failed,
                                                     "join required or invalid peer"));
                return;
            }

            JsonValue response;
            response.set("type", JsonValue("history_resp"));
            response.set("code", JsonValue(static_cast<int>(ResultCode::Ok)));
            response.set("peer", JsonValue(peer));
            JsonValue messageArray;
            for (const ChatMessage& message : messages)
                messageArray.push(messageToJson(message));
            response.set("messages", messageArray);
            connection->sendMessage(response.serialize());
        }
        else if (type == "add_contact")
        {
            const std::string username = joinHandler.usernameOf(connection->fd());
            const std::string contactUsername = request.get("username").asString();
            std::string passwordHash;
            const bool contactExists = db.findUser(contactUsername, passwordHash);
            const bool succeeded = !username.empty() && contactExists
                && db.addContact(username, contactUsername);
            connection->sendMessage(makeResponse("add_contact_resp",
                                                 succeeded ? ResultCode::Ok : ResultCode::Failed,
                                                 succeeded ? "ok" : "invalid contact"));
        }
        else if (type == "contacts")
        {
            const std::string username = joinHandler.usernameOf(connection->fd());
            std::vector<ContactProfile> contacts;
            if (username.empty() || !db.loadContacts(username, contacts))
            {
                connection->sendMessage(makeResponse("contacts_resp", ResultCode::Failed, "join required"));
                return;
            }
            JsonValue response;
            response.set("type", JsonValue("contacts_resp"));
            response.set("code", JsonValue(static_cast<int>(ResultCode::Ok)));
            JsonValue contactArray;
            for (const ContactProfile& contact : contacts)
            {
                JsonValue contactJson;
                contactJson.set("username", JsonValue(contact.username));
                contactJson.set("nickname", JsonValue(contact.nickname.empty() ? contact.username : contact.nickname));
                contactJson.set("avatarSeed", JsonValue(contact.avatarSeed));
                contactJson.set("online", JsonValue(joinHandler.fdOf(contact.username) >= 0));
                contactArray.push(contactJson);
            }
            response.set("contacts", contactArray);
            connection->sendMessage(response.serialize());
        }
        else if (type == "friend_request")
        {
            const std::string senderUsername = joinHandler.usernameOf(connection->fd());
            const std::string receiverUsername = request.get("username").asString();
            std::string passwordHash;
            const bool receiverExists = db.findUser(receiverUsername, passwordHash);
            const bool succeeded = !senderUsername.empty() && receiverExists
                && db.createFriendRequest(senderUsername, receiverUsername);
            connection->sendMessage(makeResponse("friend_request_resp",
                                                 succeeded ? ResultCode::Ok : ResultCode::Failed,
                                                 succeeded ? "request sent" : "invalid or duplicate request"));
            if (!succeeded)
                return;

            std::vector<FriendRequest> requests;
            if (!db.loadPendingFriendRequests(receiverUsername, requests))
                return;
            for (const FriendRequest& friendRequest : requests)
            {
                if (friendRequest.senderUsername != senderUsername)
                    continue;
                const int receiverFd = joinHandler.fdOf(receiverUsername);
                const auto receiverIterator = connections.find(receiverFd);
                if (receiverIterator != connections.end())
                {
                    JsonValue push;
                    push.set("type", JsonValue("friend_request_push"));
                    push.set("request", friendRequestToJson(friendRequest));
                    receiverIterator->second->sendMessage(push.serialize());
                }
                break;
            }
        }
        else if (type == "friend_requests")
        {
            const std::string receiverUsername = joinHandler.usernameOf(connection->fd());
            std::vector<FriendRequest> requests;
            if (receiverUsername.empty() || !db.loadPendingFriendRequests(receiverUsername, requests))
            {
                connection->sendMessage(makeResponse("friend_requests_resp", ResultCode::Failed,
                                                     "join required"));
                return;
            }
            JsonValue response;
            response.set("type", JsonValue("friend_requests_resp"));
            response.set("code", JsonValue(static_cast<int>(ResultCode::Ok)));
            JsonValue requestArray;
            for (const FriendRequest& friendRequest : requests)
                requestArray.push(friendRequestToJson(friendRequest));
            response.set("requests", requestArray);
            connection->sendMessage(response.serialize());
        }
        else if (type == "friend_request_response")
        {
            const std::string receiverUsername = joinHandler.usernameOf(connection->fd());
            const std::string senderUsername = request.get("sender").asString();
            const bool accepted = request.get("accepted").asBool();
            const bool succeeded = db.respondToFriendRequest(receiverUsername, senderUsername, accepted);
            connection->sendMessage(makeResponse("friend_request_response_resp",
                                                 succeeded ? ResultCode::Ok : ResultCode::Failed,
                                                 succeeded ? "ok" : "request not found"));
            if (succeeded && accepted)
            {
                const int senderFd = joinHandler.fdOf(senderUsername);
                const auto senderIterator = connections.find(senderFd);
                if (senderIterator != connections.end())
                {
                    JsonValue push;
                    push.set("type", JsonValue("friend_accepted_push"));
                    push.set("username", JsonValue(receiverUsername));
                    senderIterator->second->sendMessage(push.serialize());
                }
            }
        }
        else if (type == "delete_chat")
        {
            const std::string username = joinHandler.usernameOf(connection->fd());
            const std::string peer = request.get("peer").asString();
            const bool succeeded = chatHandler.deleteConversation(username, peer);
            JsonValue response;
            response.set("type", JsonValue("delete_chat_resp"));
            response.set("code", JsonValue(static_cast<int>(succeeded ? ResultCode::Ok : ResultCode::Failed)));
            response.set("msg", JsonValue(succeeded ? "ok" : "join required or invalid peer"));
            response.set("peer", JsonValue(peer));
            connection->sendMessage(response.serialize());
        }
        else if (type == "clear_chats")
        {
            const std::string username = joinHandler.usernameOf(connection->fd());
            const bool succeeded = chatHandler.clearAllMessages(username);
            connection->sendMessage(makeResponse("clear_chats_resp",
                                                 succeeded ? ResultCode::Ok : ResultCode::Failed,
                                                 succeeded ? "ok" : "join required"));
        }
        else
        {
            connection->sendMessage(makeResponse("error_resp", ResultCode::Failed, "unknown type"));
        }
    };

    TcpServer server(loop);
    server.setAcceptCallback([&](SOCKET clientSocket)
    {
        Connection* connection = new Connection(clientSocket);
        const int fd = static_cast<int>(clientSocket);
        connections[fd] = connection;
        loop->addFd(fd, [&, connection, fd](int)
        {
            connection->onReadable();
            if (connection->closed())
                removeConnection(fd);
        });
        connection->setMessageCallback(handleMessage);
        connection->setCloseCallback([&](Connection* closedConnection)
        {
            const int fd = closedConnection->fd();
            const std::string username = joinHandler.usernameOf(fd);
            joinHandler.removeConnection(fd);
            if (!username.empty())
                broadcastPresence(username, false);
            loop->removeFd(fd);
            Log::info("client disconnected fd=" + std::to_string(fd));
        });
    });

    if (!server.listen("0.0.0.0", 9000)) return 1;
    Log::info("xiaofu-vcall server started");
    loop->run();

    for (const auto& entry : connections) delete entry.second;
    connections.clear();
    delete loop;
    return 0;
}
