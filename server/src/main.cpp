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
#include "heartbeat/HeartbeatManager.h"
#include "db/DbManager.h"
#include "util/Log.h"

int main() {
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
    if (!db.open(databaseDirectory + "/xiaofu.db")) {
        Log::error("server startup aborted: cannot open database " + databaseDirectory + "/xiaofu.db");
        return 1;
    }
    Log::info("database loaded: " + databaseDirectory + "/xiaofu.db");
    if (!db.createTables()) {
        Log::error("server startup aborted: cannot create database tables");
        return 1;
    }
    Log::info("database tables ready");

    LoginHandler loginHandler(&db);
    RegisterHandler registerHandler(&db);
    ForgotHandler forgotHandler(&db);

    std::map<int, Connection*> connections;

    auto removeConnection = [&](int fd) {
        auto connectionIterator = connections.find(fd);
        if (connectionIterator != connections.end()) {
            loop->removeFd(fd);
            delete connectionIterator->second;
            connections.erase(connectionIterator);
        }
    };

    JoinHandler joinHandler([&](int fd) {
        Log::info("kick timeout fd=" + std::to_string(fd));
        auto connectionIterator = connections.find(fd);
        if (connectionIterator != connections.end()) {
            connectionIterator->second->close();
            removeConnection(fd);
        }
    });

    HeartbeatManager heartbeat(loop, &joinHandler, 15000, 50000);

    auto makeResponse = [](const std::string& type, ResultCode code, const std::string& message) {
        JsonValue response;
        response.set("type", JsonValue(type));
        response.set("code", JsonValue(static_cast<int>(code)));
        response.set("msg", JsonValue(message));
        return response.serialize();
    };

    std::function<void(Connection*, const std::string&)> handleMessage;
    handleMessage = [&](Connection* connection, const std::string& messageBody) {
        bool parseSucceeded = false;
        JsonValue request = JsonValue::parse(messageBody, &parseSucceeded);
        if (!parseSucceeded || !request.isObject()) {
            connection->sendMessage(makeResponse("error_resp", ResultCode::Failed, "bad json"));
            return;
        }
        const std::string type = request.get("type").asString();

        if (type == "join") {
            const std::string username = request.get("username").asString();
            if (joinHandler.handleJoin(connection->fd(), username)) {
                connection->sendMessage(makeResponse("join_resp", ResultCode::Ok, "ok"));
            } else {
                connection->sendMessage(makeResponse("join_resp", ResultCode::JoinRejected, "username empty or already online"));
            }
        } else if (type == "heartbeat") {
            joinHandler.handleHeartbeat(connection->fd());
            connection->sendMessage(makeResponse("heartbeat_resp", ResultCode::Ok, "ok"));
        } else if (type == "register") {
            const std::string username = request.get("username").asString();
            const std::string email = request.get("email").asString();
            const std::string password = request.get("password").asString();
            const ResultCode code = registerHandler.handleRegister(username, email, password);
            std::string responseMessage = "failed";
            if (code == ResultCode::Ok) responseMessage = "ok";
            else if (code == ResultCode::UserExists) responseMessage = "username already exists";
            else if (code == ResultCode::InvalidEmail) responseMessage = "invalid email";
            else if (code == ResultCode::InvalidPassword) responseMessage = "invalid password";
            connection->sendMessage(makeResponse("register_resp", code, responseMessage));
        } else if (type == "login") {
            const std::string username = request.get("username").asString();
            const std::string password = request.get("password").asString();
            const ResultCode code = loginHandler.handleLogin(username, password);
            JsonValue response;
            response.set("type", JsonValue("login_resp"));
            response.set("code", JsonValue(static_cast<int>(code)));
            response.set("msg", JsonValue(code == ResultCode::Ok ? "ok" : "failed"));
            if (code == ResultCode::Ok) response.set("username", JsonValue(username));
            connection->sendMessage(response.serialize());
        } else if (type == "forgot") {
            const std::string username = request.get("username").asString();
            const std::string newPassword = request.get("newPassword").asString();
            const ResultCode code = forgotHandler.handleForgot(username, newPassword);
            std::string responseMessage = "failed";
            if (code == ResultCode::Ok) responseMessage = "ok";
            else if (code == ResultCode::UserNotFound) responseMessage = "user not found";
            else if (code == ResultCode::InvalidPassword) responseMessage = "invalid password";
            connection->sendMessage(makeResponse("forgot_resp", code, responseMessage));
        } else {
            connection->sendMessage(makeResponse("error_resp", ResultCode::Failed, "unknown type"));
        }
    };

    TcpServer server(loop);
    server.setAcceptCallback([&](SOCKET clientSocket) {
        Connection* connection = new Connection(clientSocket);
        const int fd = static_cast<int>(clientSocket);
        connections[fd] = connection;
        loop->addFd(fd, [&, connection, fd](int) {
            connection->onReadable();
            if (connection->closed())
                removeConnection(fd);
        });
        connection->setMessageCallback(handleMessage);
        connection->setCloseCallback([&](Connection* closedConnection) {
            const int fd = closedConnection->fd();
            joinHandler.removeConnection(fd);
            loop->removeFd(fd);
            Log::info("client disconnected fd=" + std::to_string(fd));
        });
    });

    if (!server.listen("127.0.0.1", 9000)) return 1;
    Log::info("xiaofu-vcall server started");
    loop->run();

    for (const auto& entry : connections) delete entry.second;
    connections.clear();
    delete loop;
    return 0;
}
