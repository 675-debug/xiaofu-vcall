#include "DbManager.h"
#include "../util/Log.h"

#include <cstdlib>
#include <cstring>

namespace {

const char* envValue(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value && value[0] != '\0') ? value : fallback;
}

MYSQL_STMT* prepareStatement(MYSQL* mysql, const char* sql) {
    MYSQL_STMT* statement = mysql_stmt_init(mysql);
    if (!statement) {
        Log::error("mysql_stmt_init failed");
        return nullptr;
    }
    if (mysql_stmt_prepare(statement, sql,
                           static_cast<unsigned long>(std::strlen(sql))) != 0) {
        Log::error(std::string("mysql_stmt_prepare failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return nullptr;
    }
    return statement;
}

bool executeBound(MYSQL_STMT* statement, MYSQL_BIND* params, unsigned long paramCount) {
    if (paramCount > 0 && mysql_stmt_bind_param(statement, params) != 0) {
        Log::error(std::string("mysql_stmt_bind_param failed: ") + mysql_stmt_error(statement));
        return false;
    }
    if (mysql_stmt_execute(statement) != 0) {
        Log::error(std::string("mysql_stmt_execute failed: ") + mysql_stmt_error(statement));
        return false;
    }
    return true;
}

void bindStringParam(MYSQL_BIND& bind, const std::string& value) {
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char*>(value.c_str());
    bind.buffer_length = static_cast<unsigned long>(value.size());
}

void bindIntParam(MYSQL_BIND& bind, int& value) {
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &value;
}

template <typename IntegerT>
void bindInt64Param(MYSQL_BIND& bind, IntegerT& value) {
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &value;
}

void bindStringResult(MYSQL_BIND& bind, char* buffer, unsigned long bufferLength) {
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = buffer;
    bind.buffer_length = bufferLength;
}

void bindIntResult(MYSQL_BIND& bind, int& value) {
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &value;
}

void bindInt64Result(MYSQL_BIND& bind, std::int64_t& value) {
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &value;
}

} // anonymous namespace

DbManager::DbManager() : mysql(nullptr) {}
DbManager::~DbManager() { close(); }

bool DbManager::open(const std::string& dbPath) {
    if (mysql)
        return true;
    databasePath = dbPath;

    const std::string host = envValue("XIAOFU_MYSQL_HOST", "127.0.0.1");
    const unsigned int port = static_cast<unsigned int>(
        std::strtoul(envValue("XIAOFU_MYSQL_PORT", "3306"), nullptr, 10));
    const std::string user = envValue("XIAOFU_MYSQL_USER", "xiaofu");
    const std::string password = envValue("XIAOFU_MYSQL_PASSWORD", "");
    const std::string database = envValue("XIAOFU_MYSQL_DATABASE", "xiaofu");

    mysql = mysql_init(nullptr);
    if (!mysql) {
        Log::error("mysql_init failed");
        return false;
    }

    unsigned int connectTimeout = 5;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeout);

    if (!mysql_real_connect(mysql, host.c_str(), user.c_str(), password.c_str(),
                            database.c_str(), port, nullptr, 0)) {
        Log::error(std::string("mysql connect failed: ") + mysql_error(mysql));
        close();
        return false;
    }

    if (mysql_set_character_set(mysql, "utf8mb4") != 0) {
        Log::error(std::string("mysql set charset failed: ") + mysql_error(mysql));
        close();
        return false;
    }

    Log::info("mysql connected, database=" + database + " (path hint: " + dbPath + ")");
    return true;
}

bool DbManager::ping() {
    if (mysql && mysql_ping(mysql) == 0)
        return true;

    if (mysql) {
        Log::error(std::string("mysql connection lost, reconnecting: ") + mysql_error(mysql));
        close();
    }
    return open(databasePath);
}

bool DbManager::createTables() {
    static const char* statements[] = {
        "CREATE TABLE IF NOT EXISTS users ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "username VARCHAR(64) UNIQUE NOT NULL,"
        "password VARCHAR(128) NOT NULL,"
        "email VARCHAR(255) NOT NULL DEFAULT '',"
        "created_at DATETIME NOT NULL DEFAULT NOW(),"
        "nickname VARCHAR(64) NOT NULL DEFAULT '',"
        "avatar_seed INT NOT NULL DEFAULT 0"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS contacts ("
        "owner_username VARCHAR(64) NOT NULL,"
        "contact_username VARCHAR(64) NOT NULL,"
        "PRIMARY KEY(owner_username, contact_username)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS friend_requests ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "sender_username VARCHAR(64) NOT NULL,"
        "receiver_username VARCHAR(64) NOT NULL,"
        "status VARCHAR(16) NOT NULL DEFAULT 'pending',"
        "created_at DATETIME NOT NULL DEFAULT NOW()"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS messages ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "sender VARCHAR(64) NOT NULL,"
        "receiver VARCHAR(64) NOT NULL,"
        "content TEXT NOT NULL,"
        "sent_at DATETIME NOT NULL DEFAULT NOW()"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS call_history ("
        "call_id VARCHAR(64) PRIMARY KEY,"
        "caller VARCHAR(64) NOT NULL,"
        "callee VARCHAR(64) NOT NULL,"
        "state VARCHAR(16) NOT NULL,"
        "created_at BIGINT NOT NULL,"
        "accepted_at BIGINT NOT NULL DEFAULT 0,"
        "connected_at BIGINT NOT NULL DEFAULT 0,"
        "ended_at BIGINT NOT NULL,"
        "duration BIGINT NOT NULL DEFAULT 0,"
        "end_reason VARCHAR(64) NOT NULL"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS loginlog ("
        "username VARCHAR(64) PRIMARY KEY,"
        "status VARCHAR(16) NOT NULL,"
        "updated_at DATETIME NOT NULL DEFAULT NOW()"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
    };

    for (const char* sql : statements) {
        if (mysql_query(mysql, sql) != 0) {
            Log::error(std::string("createTables failed: ") + mysql_error(mysql));
            return false;
        }
    }
    return true;
}

bool DbManager::insertUser(const std::string& username, const std::string& passwordHash,
                           const std::string& email, const std::string& nickname, int avatarSeed) {
    MYSQL_STMT* statement = prepareStatement(mysql,
        "INSERT INTO users (username, password, email, nickname, avatar_seed) "
        "VALUES (?, ?, ?, ?, ?);");
    if (!statement)
        return false;
    MYSQL_BIND params[5];
    bindStringParam(params[0], username);
    bindStringParam(params[1], passwordHash);
    bindStringParam(params[2], email);
    bindStringParam(params[3], nickname);
    bindIntParam(params[4], avatarSeed);
    const bool succeeded = executeBound(statement, params, 5);
    mysql_stmt_close(statement);
    return succeeded;
}

bool DbManager::findUser(const std::string& username, std::string& passwordHash) {
    MYSQL_STMT* statement = prepareStatement(mysql,
        "SELECT password FROM users WHERE username = ?;");
    if (!statement)
        return false;
    MYSQL_BIND param;
    bindStringParam(param, username);
    if (!executeBound(statement, &param, 1)) {
        mysql_stmt_close(statement);
        return false;
    }
    if (mysql_stmt_store_result(statement) != 0) {
        Log::error(std::string("mysql_stmt_store_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    char passwordBuffer[256] = {0};
    MYSQL_BIND result;
    bindStringResult(result, passwordBuffer, sizeof(passwordBuffer));
    if (mysql_stmt_bind_result(statement, &result) != 0) {
        Log::error(std::string("mysql_stmt_bind_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    bool found = false;
    if (mysql_stmt_fetch(statement) == 0) {
        passwordHash = passwordBuffer;
        found = true;
    }
    mysql_stmt_close(statement);
    return found;
}

bool DbManager::saveProfile(const std::string& username, const std::string& nickname, int avatarSeed) {
    MYSQL_STMT* statement = prepareStatement(mysql,
        "UPDATE users SET nickname = ?, avatar_seed = ? WHERE username = ?;");
    if (!statement)
        return false;
    MYSQL_BIND params[3];
    bindStringParam(params[0], nickname);
    bindIntParam(params[1], avatarSeed);
    bindStringParam(params[2], username);
    const bool succeeded = executeBound(statement, params, 3);
    mysql_stmt_close(statement);
    return succeeded;
}

bool DbManager::addContact(const std::string& ownerUsername, const std::string& contactUsername) {
    if (ownerUsername.empty() || contactUsername.empty() || ownerUsername == contactUsername)
        return false;
    MYSQL_STMT* statement = prepareStatement(mysql,
        "INSERT IGNORE INTO contacts (owner_username, contact_username) VALUES (?, ?);");
    if (!statement)
        return false;
    MYSQL_BIND params[2];
    bindStringParam(params[0], ownerUsername);
    bindStringParam(params[1], contactUsername);
    const bool succeeded = executeBound(statement, params, 2);
    mysql_stmt_close(statement);
    return succeeded;
}

bool DbManager::loadContacts(const std::string& ownerUsername, std::vector<ContactProfile>& contacts) {
    contacts.clear();
    MYSQL_STMT* statement = prepareStatement(mysql,
        "SELECT users.username, users.nickname, users.avatar_seed "
        "FROM contacts JOIN users ON contacts.contact_username = users.username "
        "WHERE contacts.owner_username = ? ORDER BY users.username ASC;");
    if (!statement)
        return false;
    MYSQL_BIND param;
    bindStringParam(param, ownerUsername);
    if (!executeBound(statement, &param, 1)) {
        mysql_stmt_close(statement);
        return false;
    }
    if (mysql_stmt_store_result(statement) != 0) {
        Log::error(std::string("mysql_stmt_store_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    char usernameBuffer[128] = {0};
    char nicknameBuffer[128] = {0};
    int avatarSeed = 0;
    MYSQL_BIND result[3];
    bindStringResult(result[0], usernameBuffer, sizeof(usernameBuffer));
    bindStringResult(result[1], nicknameBuffer, sizeof(nicknameBuffer));
    bindIntResult(result[2], avatarSeed);
    if (mysql_stmt_bind_result(statement, result) != 0) {
        Log::error(std::string("mysql_stmt_bind_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    while (mysql_stmt_fetch(statement) == 0) {
        ContactProfile profile;
        profile.username = usernameBuffer;
        profile.nickname = nicknameBuffer;
        profile.avatarSeed = avatarSeed;
        contacts.push_back(profile);
    }
    mysql_stmt_close(statement);
    return true;
}

bool DbManager::createFriendRequest(const std::string& senderUsername,
                                    const std::string& receiverUsername) {
    if (senderUsername.empty() || receiverUsername.empty() || senderUsername == receiverUsername)
        return false;

    MYSQL_STMT* checkStatement = prepareStatement(mysql,
        "SELECT 1 FROM friend_requests WHERE sender_username = ? AND receiver_username = ? "
        "AND status = 'pending' LIMIT 1;");
    if (!checkStatement)
        return false;
    MYSQL_BIND checkParams[2];
    bindStringParam(checkParams[0], senderUsername);
    bindStringParam(checkParams[1], receiverUsername);
    if (!executeBound(checkStatement, checkParams, 2)) {
        mysql_stmt_close(checkStatement);
        return false;
    }
    mysql_stmt_store_result(checkStatement);
    int dummy = 0;
    MYSQL_BIND checkResult;
    bindIntResult(checkResult, dummy);
    if (mysql_stmt_bind_result(checkStatement, &checkResult) != 0) {
        mysql_stmt_close(checkStatement);
        return false;
    }
    const bool alreadyPending = (mysql_stmt_fetch(checkStatement) == 0);
    mysql_stmt_close(checkStatement);
    if (alreadyPending)
        return false;

    MYSQL_STMT* statement = prepareStatement(mysql,
        "INSERT INTO friend_requests (sender_username, receiver_username, status) "
        "VALUES (?, ?, 'pending');");
    if (!statement)
        return false;
    MYSQL_BIND params[2];
    bindStringParam(params[0], senderUsername);
    bindStringParam(params[1], receiverUsername);
    const bool succeeded = executeBound(statement, params, 2);
    mysql_stmt_close(statement);
    return succeeded;
}

bool DbManager::loadPendingFriendRequests(const std::string& receiverUsername,
                                          std::vector<FriendRequest>& requests) {
    requests.clear();
    MYSQL_STMT* statement = prepareStatement(mysql,
        "SELECT friend_requests.sender_username, users.nickname, users.avatar_seed, "
        "friend_requests.created_at "
        "FROM friend_requests JOIN users ON friend_requests.sender_username = users.username "
        "WHERE friend_requests.receiver_username = ? AND friend_requests.status = 'pending' "
        "ORDER BY friend_requests.id ASC;");
    if (!statement)
        return false;
    MYSQL_BIND param;
    bindStringParam(param, receiverUsername);
    if (!executeBound(statement, &param, 1)) {
        mysql_stmt_close(statement);
        return false;
    }
    if (mysql_stmt_store_result(statement) != 0) {
        Log::error(std::string("mysql_stmt_store_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    char senderBuffer[128] = {0};
    char nicknameBuffer[128] = {0};
    int avatarSeed = 0;
    char createdAtBuffer[32] = {0};
    MYSQL_BIND result[4];
    bindStringResult(result[0], senderBuffer, sizeof(senderBuffer));
    bindStringResult(result[1], nicknameBuffer, sizeof(nicknameBuffer));
    bindIntResult(result[2], avatarSeed);
    bindStringResult(result[3], createdAtBuffer, sizeof(createdAtBuffer));
    if (mysql_stmt_bind_result(statement, result) != 0) {
        Log::error(std::string("mysql_stmt_bind_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    while (mysql_stmt_fetch(statement) == 0) {
        FriendRequest request;
        request.senderUsername = senderBuffer;
        request.nickname = nicknameBuffer;
        request.avatarSeed = avatarSeed;
        request.createdAt = createdAtBuffer;
        requests.push_back(request);
    }
    mysql_stmt_close(statement);
    return true;
}

bool DbManager::respondToFriendRequest(const std::string& receiverUsername,
                                       const std::string& senderUsername, bool accepted) {
    if (receiverUsername.empty() || senderUsername.empty())
        return false;
    if (mysql_query(mysql, "START TRANSACTION;") != 0) {
        Log::error(std::string("respondToFriendRequest BEGIN failed: ") + mysql_error(mysql));
        return false;
    }

    MYSQL_STMT* updateStatement = prepareStatement(mysql,
        "UPDATE friend_requests SET status = ? WHERE sender_username = ? "
        "AND receiver_username = ? AND status = 'pending';");
    bool succeeded = (updateStatement != nullptr);
    if (succeeded) {
        std::string statusText = accepted ? "accepted" : "rejected";
        MYSQL_BIND params[3];
        bindStringParam(params[0], statusText);
        bindStringParam(params[1], senderUsername);
        bindStringParam(params[2], receiverUsername);
        succeeded = executeBound(updateStatement, params, 3)
            && mysql_stmt_affected_rows(updateStatement) == 1;
    }
    if (updateStatement)
        mysql_stmt_close(updateStatement);

    if (succeeded && accepted)
        succeeded = addContact(senderUsername, receiverUsername)
            && addContact(receiverUsername, senderUsername);

    if (succeeded) {
        if (mysql_query(mysql, "COMMIT;") != 0) {
            Log::error(std::string("respondToFriendRequest COMMIT failed: ") + mysql_error(mysql));
            mysql_query(mysql, "ROLLBACK;");
            succeeded = false;
        }
    } else {
        mysql_query(mysql, "ROLLBACK;");
    }
    return succeeded;
}

bool DbManager::updatePassword(const std::string& username, const std::string& newPasswordHash) {
    MYSQL_STMT* statement = prepareStatement(mysql,
        "UPDATE users SET password = ? WHERE username = ?;");
    if (!statement)
        return false;
    MYSQL_BIND params[2];
    bindStringParam(params[0], newPasswordHash);
    bindStringParam(params[1], username);
    const bool succeeded = executeBound(statement, params, 2);
    mysql_stmt_close(statement);
    return succeeded;
}

// 保存聊天记录，并回填数据库生成的消息编号和发送时间。
bool DbManager::saveMessage(ChatMessage& message) {
    MYSQL_STMT* statement = prepareStatement(mysql,
        "INSERT INTO messages (sender, receiver, content) VALUES (?, ?, ?);");
    if (!statement)
        return false;
    MYSQL_BIND params[3];
    bindStringParam(params[0], message.sender);
    bindStringParam(params[1], message.receiver);
    bindStringParam(params[2], message.content);
    if (!executeBound(statement, params, 3)) {
        mysql_stmt_close(statement);
        return false;
    }
    message.id = static_cast<long long>(mysql_stmt_insert_id(statement));
    mysql_stmt_close(statement);

    MYSQL_STMT* timeStatement = prepareStatement(mysql,
        "SELECT sent_at FROM messages WHERE id = ?;");
    if (!timeStatement)
        return false;
    MYSQL_BIND idParam;
    bindInt64Param(idParam, message.id);
    if (!executeBound(timeStatement, &idParam, 1)) {
        mysql_stmt_close(timeStatement);
        return false;
    }
    mysql_stmt_store_result(timeStatement);
    char sentAtBuffer[32] = {0};
    MYSQL_BIND result;
    bindStringResult(result, sentAtBuffer, sizeof(sentAtBuffer));
    if (mysql_stmt_bind_result(timeStatement, &result) != 0) {
        mysql_stmt_close(timeStatement);
        return false;
    }
    if (mysql_stmt_fetch(timeStatement) == 0)
        message.sentAt = sentAtBuffer;
    mysql_stmt_close(timeStatement);
    return true;
}

// 查询当前账号与指定联系人的双向聊天记录。
bool DbManager::loadConversation(const std::string& username, const std::string& peer,
                                 std::vector<ChatMessage>& messages) {
    messages.clear();
    MYSQL_STMT* statement = prepareStatement(mysql,
        "SELECT id, sender, receiver, content, sent_at FROM messages "
        "WHERE (sender = ? AND receiver = ?) OR (sender = ? AND receiver = ?) "
        "ORDER BY id ASC;");
    if (!statement)
        return false;
    MYSQL_BIND params[4];
    bindStringParam(params[0], username);
    bindStringParam(params[1], peer);
    bindStringParam(params[2], peer);
    bindStringParam(params[3], username);
    if (!executeBound(statement, params, 4)) {
        mysql_stmt_close(statement);
        return false;
    }
    if (mysql_stmt_store_result(statement) != 0) {
        Log::error(std::string("mysql_stmt_store_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    std::int64_t id = 0;
    char senderBuffer[128] = {0};
    char receiverBuffer[128] = {0};
    // TEXT 列按 MySQL 最大长度 65535 绑定，避免客户端把长文本判定为截断。
    std::vector<char> contentBuffer(65536, 0);
    char sentAtBuffer[32] = {0};
    MYSQL_BIND result[5];
    bindInt64Result(result[0], id);
    bindStringResult(result[1], senderBuffer, sizeof(senderBuffer));
    bindStringResult(result[2], receiverBuffer, sizeof(receiverBuffer));
    bindStringResult(result[3], contentBuffer.data(), static_cast<unsigned long>(contentBuffer.size()));
    bindStringResult(result[4], sentAtBuffer, sizeof(sentAtBuffer));
    if (mysql_stmt_bind_result(statement, result) != 0) {
        Log::error(std::string("mysql_stmt_bind_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    while (mysql_stmt_fetch(statement) == 0) {
        ChatMessage message;
        message.id = id;
        message.sender = senderBuffer;
        message.receiver = receiverBuffer;
        message.content = contentBuffer.data();
        message.sentAt = sentAtBuffer;
        messages.push_back(message);
    }
    mysql_stmt_close(statement);
    return true;
}

bool DbManager::deleteConversation(const std::string& username, const std::string& peer) {
    MYSQL_STMT* statement = prepareStatement(mysql,
        "DELETE FROM messages WHERE (sender = ? AND receiver = ?) OR (sender = ? AND receiver = ?);");
    if (!statement)
        return false;
    MYSQL_BIND params[4];
    bindStringParam(params[0], username);
    bindStringParam(params[1], peer);
    bindStringParam(params[2], peer);
    bindStringParam(params[3], username);
    const bool succeeded = executeBound(statement, params, 4);
    mysql_stmt_close(statement);
    return succeeded;
}

bool DbManager::deleteAllMessages(const std::string& username) {
    MYSQL_STMT* statement = prepareStatement(mysql,
        "DELETE FROM messages WHERE sender = ? OR receiver = ?;");
    if (!statement)
        return false;
    MYSQL_BIND params[2];
    bindStringParam(params[0], username);
    bindStringParam(params[1], username);
    const bool succeeded = executeBound(statement, params, 2);
    mysql_stmt_close(statement);
    return succeeded;
}

bool DbManager::saveCallRecord(const CallRecord& record) {
    MYSQL_STMT* statement = prepareStatement(mysql,
        "INSERT INTO call_history "
        "(call_id, caller, callee, state, created_at, accepted_at, connected_at, "
        " ended_at, duration, end_reason) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "AS new ON DUPLICATE KEY UPDATE "
        "caller=new.caller, callee=new.callee, state=new.state, "
        "created_at=new.created_at, accepted_at=new.accepted_at, "
        "connected_at=new.connected_at, ended_at=new.ended_at, "
        "duration=new.duration, end_reason=new.end_reason;");
    if (!statement)
        return false;
    std::int64_t createdAt = record.createdAt;
    std::int64_t acceptedAt = record.acceptedAt;
    std::int64_t connectedAt = record.connectedAt;
    std::int64_t endedAt = record.endedAt;
    std::int64_t duration = record.duration;
    MYSQL_BIND params[10];
    bindStringParam(params[0], record.callId);
    bindStringParam(params[1], record.caller);
    bindStringParam(params[2], record.callee);
    bindStringParam(params[3], record.state);
    bindInt64Param(params[4], createdAt);
    bindInt64Param(params[5], acceptedAt);
    bindInt64Param(params[6], connectedAt);
    bindInt64Param(params[7], endedAt);
    bindInt64Param(params[8], duration);
    bindStringParam(params[9], record.endReason);
    const bool ok = executeBound(statement, params, 10);
    mysql_stmt_close(statement);
    return ok;
}

bool DbManager::loadCallRecords(const std::string& username, int limit,
                                std::vector<CallRecord>& records) {
    records.clear();
    MYSQL_STMT* statement = prepareStatement(mysql,
        "SELECT call_id, caller, callee, state, created_at, accepted_at, connected_at, "
        "ended_at, duration, end_reason FROM call_history "
        "WHERE caller = ? OR callee = ? "
        "ORDER BY ended_at DESC LIMIT ?;");
    if (!statement)
        return false;
    int effectiveLimit = limit > 0 ? limit : 50;
    MYSQL_BIND params[3];
    bindStringParam(params[0], username);
    bindStringParam(params[1], username);
    bindIntParam(params[2], effectiveLimit);
    if (!executeBound(statement, params, 3)) {
        mysql_stmt_close(statement);
        return false;
    }
    if (mysql_stmt_store_result(statement) != 0) {
        Log::error(std::string("mysql_stmt_store_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    char callIdBuffer[128] = {0};
    char callerBuffer[128] = {0};
    char calleeBuffer[128] = {0};
    char stateBuffer[32] = {0};
    std::int64_t createdAt = 0, acceptedAt = 0, connectedAt = 0, endedAt = 0, duration = 0;
    char endReasonBuffer[128] = {0};
    MYSQL_BIND result[10];
    bindStringResult(result[0], callIdBuffer, sizeof(callIdBuffer));
    bindStringResult(result[1], callerBuffer, sizeof(callerBuffer));
    bindStringResult(result[2], calleeBuffer, sizeof(calleeBuffer));
    bindStringResult(result[3], stateBuffer, sizeof(stateBuffer));
    bindInt64Result(result[4], createdAt);
    bindInt64Result(result[5], acceptedAt);
    bindInt64Result(result[6], connectedAt);
    bindInt64Result(result[7], endedAt);
    bindInt64Result(result[8], duration);
    bindStringResult(result[9], endReasonBuffer, sizeof(endReasonBuffer));
    if (mysql_stmt_bind_result(statement, result) != 0) {
        Log::error(std::string("mysql_stmt_bind_result failed: ") + mysql_stmt_error(statement));
        mysql_stmt_close(statement);
        return false;
    }
    while (mysql_stmt_fetch(statement) == 0) {
        CallRecord record;
        record.callId = callIdBuffer;
        record.caller = callerBuffer;
        record.callee = calleeBuffer;
        record.state = stateBuffer;
        record.createdAt = createdAt;
        record.acceptedAt = acceptedAt;
        record.connectedAt = connectedAt;
        record.endedAt = endedAt;
        record.duration = duration;
        record.endReason = endReasonBuffer;
        records.push_back(record);
    }
    mysql_stmt_close(statement);
    return true;
}

// ============================================================
//  loginlog — single-account single-login
// ============================================================

bool DbManager::resetAllLoginStatus() {
    if (mysql_query(mysql,
        "UPDATE loginlog SET status='下线', updated_at=NOW() WHERE status='登录';") != 0) {
        Log::error(std::string("resetAllLoginStatus failed: ") + mysql_error(mysql));
        return false;
    }
    const my_ulonglong changed = mysql_affected_rows(mysql);
    if (changed > 0)
        Log::info("resetAllLoginStatus: reset " + std::to_string(changed) + " login(s) to offline");
    return true;
}

bool DbManager::tryLogin(const std::string& username) {
    if (username.empty())
        return false;

    // MySQL 没有 BEGIN IMMEDIATE：用事务 + SELECT ... FOR UPDATE 行锁，
    // 保证同一账号的两个并发登录请求只有一个能成功。
    if (mysql_query(mysql, "START TRANSACTION;") != 0) {
        Log::error(std::string("tryLogin START TRANSACTION failed: ") + mysql_error(mysql));
        return false;
    }

    // Step 1: 锁定该账号 loginlog 行（不存在时锁间隙，后续 INSERT 由唯一键兜底）
    MYSQL_STMT* selectStatement = prepareStatement(mysql,
        "SELECT status FROM loginlog WHERE username = ? FOR UPDATE;");
    if (!selectStatement) {
        mysql_query(mysql, "ROLLBACK;");
        return false;
    }
    MYSQL_BIND selectParam;
    bindStringParam(selectParam, username);
    if (!executeBound(selectStatement, &selectParam, 1)) {
        mysql_stmt_close(selectStatement);
        mysql_query(mysql, "ROLLBACK;");
        return false;
    }
    mysql_stmt_store_result(selectStatement);
    char statusBuffer[32] = {0};
    MYSQL_BIND selectResult;
    bindStringResult(selectResult, statusBuffer, sizeof(statusBuffer));
    if (mysql_stmt_bind_result(selectStatement, &selectResult) != 0) {
        mysql_stmt_close(selectStatement);
        mysql_query(mysql, "ROLLBACK;");
        return false;
    }
    const bool exists = (mysql_stmt_fetch(selectStatement) == 0);
    mysql_stmt_close(selectStatement);

    if (exists && std::string(statusBuffer) == "登录") {
        mysql_query(mysql, "ROLLBACK;");
        Log::info("tryLogin: username=" + username + " already logged in, rejected");
        return false;
    }

    // Step 2: upsert 为“登录”
    bool ok = false;
    if (exists) {
        MYSQL_STMT* updateStatement = prepareStatement(mysql,
            "UPDATE loginlog SET status='登录', updated_at=NOW() WHERE username=?;");
        if (updateStatement) {
            MYSQL_BIND updateParam;
            bindStringParam(updateParam, username);
            ok = executeBound(updateStatement, &updateParam, 1);
            mysql_stmt_close(updateStatement);
        }
    } else {
        MYSQL_STMT* insertStatement = prepareStatement(mysql,
            "INSERT INTO loginlog (username, status, updated_at) VALUES (?, '登录', NOW());");
        if (insertStatement) {
            MYSQL_BIND insertParam;
            bindStringParam(insertParam, username);
            ok = executeBound(insertStatement, &insertParam, 1);
            mysql_stmt_close(insertStatement);
        }
    }

    if (ok) {
        if (mysql_query(mysql, "COMMIT;") == 0) {
            Log::info("tryLogin: username=" + username + " => 登录");
            return true;
        }
        Log::error(std::string("tryLogin COMMIT failed: ") + mysql_error(mysql));
        mysql_query(mysql, "ROLLBACK;");
        return false;
    }

    // 并发 INSERT 可能触发死锁/唯一键冲突，此时本请求失败、另一请求已成功。
    Log::error(std::string("tryLogin upsert failed: ") + mysql_error(mysql));
    mysql_query(mysql, "ROLLBACK;");
    return false;
}

bool DbManager::setOffline(const std::string& username) {
    if (username.empty())
        return false;

    MYSQL_STMT* statement = prepareStatement(mysql,
        "UPDATE loginlog SET status='下线', updated_at=NOW() WHERE username=?;");
    if (!statement) {
        Log::error("setOffline prepare failed");
        return false;
    }
    MYSQL_BIND param;
    bindStringParam(param, username);
    const bool ok = executeBound(statement, &param, 1);
    const my_ulonglong changed = mysql_stmt_affected_rows(statement);
    mysql_stmt_close(statement);

    if (changed > 0)
        Log::info("setOffline: username=" + username + " => 下线");
    else
        Log::info("setOffline: username=" + username + " no row updated (idempotent)");
    return ok;
}

bool DbManager::initLoginlogForUser(const std::string& username) {
    if (username.empty())
        return false;

    MYSQL_STMT* statement = prepareStatement(mysql,
        "INSERT IGNORE INTO loginlog (username, status, updated_at) VALUES (?, '下线', NOW());");
    if (!statement) {
        Log::error("initLoginlogForUser prepare failed");
        return false;
    }
    MYSQL_BIND param;
    bindStringParam(param, username);
    const bool ok = executeBound(statement, &param, 1);
    mysql_stmt_close(statement);

    if (ok)
        Log::info("initLoginlogForUser: username=" + username + " => 下线");
    return ok;
}

void DbManager::close() {
    if (mysql) {
        mysql_close(mysql);
        mysql = nullptr;
    }
}
