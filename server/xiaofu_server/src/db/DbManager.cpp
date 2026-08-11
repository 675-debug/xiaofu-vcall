#include "DbManager.h"
#include "../util/Log.h"

DbManager::DbManager() : db(nullptr) {}
DbManager::~DbManager() { close(); }

bool DbManager::open(const std::string& dbPath) {
    const int resultCode = sqlite3_open(dbPath.c_str(), &db);
    if (resultCode != SQLITE_OK) {
        Log::error(std::string("open db failed: ") + (db ? sqlite3_errmsg(db) : "unknown"));
        close();
        return false;
    }
    // 必须先设置忙等待，再让多个 worker 同时协商 WAL，避免第二个连接立即返回 SQLITE_BUSY。
    sqlite3_busy_timeout(db, 3000);
    const char* workerPragmas =
        "PRAGMA busy_timeout=3000;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA foreign_keys=ON;";
    char* errorMessage = nullptr;
    if (sqlite3_exec(db, workerPragmas, nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        Log::error(std::string("configure db failed: ")
                   + (errorMessage ? errorMessage : "unknown"));
        sqlite3_free(errorMessage);
        close();
        return false;
    }
    return true;
}

bool DbManager::createTables() {
    const char* userTableSql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password TEXT NOT NULL,"
        "email TEXT NOT NULL DEFAULT '',"
        "created_at TEXT DEFAULT (datetime('now')));";
    char* errorMessage = nullptr;
    int resultCode = sqlite3_exec(db, userTableSql, nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        Log::error(std::string("createTables failed: ") + (errorMessage ? errorMessage : "unknown"));
        sqlite3_free(errorMessage);
        return false;
    }
    // 兼容旧库：users 表缺少 email 列时补上，重复添加报错则忽略
    errorMessage = nullptr;
    resultCode = sqlite3_exec(db, "ALTER TABLE users ADD COLUMN email TEXT NOT NULL DEFAULT '';", nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        Log::info(std::string("email column already exists: ") + (errorMessage ? errorMessage : "unknown"));
        sqlite3_free(errorMessage);
    }
    errorMessage = nullptr;
    resultCode = sqlite3_exec(db, "ALTER TABLE users ADD COLUMN nickname TEXT NOT NULL DEFAULT '';", nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        sqlite3_free(errorMessage);
    }
    errorMessage = nullptr;
    resultCode = sqlite3_exec(db, "ALTER TABLE users ADD COLUMN avatar_seed INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        sqlite3_free(errorMessage);
    }

    const char* contactTableSql =
        "CREATE TABLE IF NOT EXISTS contacts ("
        "owner_username TEXT NOT NULL,"
        "contact_username TEXT NOT NULL,"
        "PRIMARY KEY(owner_username, contact_username));";
    errorMessage = nullptr;
    resultCode = sqlite3_exec(db, contactTableSql, nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        Log::error(std::string("create contacts failed: ") + (errorMessage ? errorMessage : "unknown"));
        sqlite3_free(errorMessage);
        return false;
    }

    const char* friendRequestTableSql =
        "CREATE TABLE IF NOT EXISTS friend_requests ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender_username TEXT NOT NULL,"
        "receiver_username TEXT NOT NULL,"
        "status TEXT NOT NULL DEFAULT 'pending',"
        "created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')));";
    errorMessage = nullptr;
    resultCode = sqlite3_exec(db, friendRequestTableSql, nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        Log::error(std::string("create friend requests failed: ") + (errorMessage ? errorMessage : "unknown"));
        sqlite3_free(errorMessage);
        return false;
    }

    const char* messageTableSql =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender TEXT NOT NULL,"
        "receiver TEXT NOT NULL,"
        "content TEXT NOT NULL,"
        "sent_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')));";
    errorMessage = nullptr;
    resultCode = sqlite3_exec(db, messageTableSql, nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        Log::error(std::string("create messages failed: ") + (errorMessage ? errorMessage : "unknown"));
        sqlite3_free(errorMessage);
        return false;
    }

    const char* callHistorySql =
        "CREATE TABLE IF NOT EXISTS call_history ("
        "call_id TEXT PRIMARY KEY,"
        "caller TEXT NOT NULL,"
        "callee TEXT NOT NULL,"
        "state TEXT NOT NULL,"
        "created_at INTEGER NOT NULL,"
        "accepted_at INTEGER NOT NULL DEFAULT 0,"
        "connected_at INTEGER NOT NULL DEFAULT 0,"
        "ended_at INTEGER NOT NULL,"
        "duration INTEGER NOT NULL DEFAULT 0,"
        "end_reason TEXT NOT NULL);";
    errorMessage = nullptr;
    resultCode = sqlite3_exec(db, callHistorySql, nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        Log::error(std::string("create call_history failed: ") + (errorMessage ? errorMessage : "unknown"));
        sqlite3_free(errorMessage);
        return false;
    }

    const char* loginlogSql =
        "CREATE TABLE IF NOT EXISTS loginlog ("
        "username TEXT PRIMARY KEY,"
        "status TEXT NOT NULL CHECK(status IN ('登录','下线')),"
        "updated_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));";
    errorMessage = nullptr;
    resultCode = sqlite3_exec(db, loginlogSql, nullptr, nullptr, &errorMessage);
    if (resultCode != SQLITE_OK) {
        Log::error(std::string("create loginlog failed: ") + (errorMessage ? errorMessage : "unknown"));
        sqlite3_free(errorMessage);
        return false;
    }
    return true;
}

bool DbManager::insertUser(const std::string& username, const std::string& passwordHash,
                           const std::string& email, const std::string& nickname, int avatarSeed) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO users (username, password, email, nickname, avatar_seed) VALUES (?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, email.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, nickname.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 5, avatarSeed);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool DbManager::saveProfile(const std::string& username, const std::string& nickname, int avatarSeed) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "UPDATE users SET nickname = ?, avatar_seed = ? WHERE username = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, nickname.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 2, avatarSeed);
    sqlite3_bind_text(statement, 3, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool DbManager::addContact(const std::string& ownerUsername, const std::string& contactUsername) {
    if (ownerUsername.empty() || contactUsername.empty() || ownerUsername == contactUsername) return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT OR IGNORE INTO contacts (owner_username, contact_username) VALUES (?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, ownerUsername.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, contactUsername.c_str(), -1, SQLITE_TRANSIENT);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool DbManager::loadContacts(const std::string& ownerUsername, std::vector<ContactProfile>& contacts) {
    contacts.clear();
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT users.username, users.nickname, users.avatar_seed "
        "FROM contacts JOIN users ON contacts.contact_username = users.username "
        "WHERE contacts.owner_username = ? ORDER BY users.username ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, ownerUsername.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        ContactProfile profile;
        const unsigned char* usernameText = sqlite3_column_text(statement, 0);
        const unsigned char* nicknameText = sqlite3_column_text(statement, 1);
        profile.username = usernameText ? reinterpret_cast<const char*>(usernameText) : "";
        profile.nickname = nicknameText ? reinterpret_cast<const char*>(nicknameText) : "";
        profile.avatarSeed = sqlite3_column_int(statement, 2);
        contacts.push_back(profile);
    }
    sqlite3_finalize(statement);
    return true;
}

bool DbManager::createFriendRequest(const std::string& senderUsername,
                                    const std::string& receiverUsername) {
    if (senderUsername.empty() || receiverUsername.empty() || senderUsername == receiverUsername)
        return false;

    sqlite3_stmt* checkStatement = nullptr;
    const char* checkSql =
        "SELECT 1 FROM friend_requests WHERE sender_username = ? AND receiver_username = ? "
        "AND status = 'pending' LIMIT 1;";
    if (sqlite3_prepare_v2(db, checkSql, -1, &checkStatement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(checkStatement, 1, senderUsername.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(checkStatement, 2, receiverUsername.c_str(), -1, SQLITE_TRANSIENT);
    const bool alreadyPending = sqlite3_step(checkStatement) == SQLITE_ROW;
    sqlite3_finalize(checkStatement);
    if (alreadyPending)
        return false;

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO friend_requests (sender_username, receiver_username, status) VALUES (?, ?, 'pending');";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(statement, 1, senderUsername.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, receiverUsername.c_str(), -1, SQLITE_TRANSIENT);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool DbManager::loadPendingFriendRequests(const std::string& receiverUsername,
                                          std::vector<FriendRequest>& requests) {
    requests.clear();
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT friend_requests.sender_username, users.nickname, users.avatar_seed, friend_requests.created_at "
        "FROM friend_requests JOIN users ON friend_requests.sender_username = users.username "
        "WHERE friend_requests.receiver_username = ? AND friend_requests.status = 'pending' "
        "ORDER BY friend_requests.id ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(statement, 1, receiverUsername.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        FriendRequest request;
        const unsigned char* senderText = sqlite3_column_text(statement, 0);
        const unsigned char* nicknameText = sqlite3_column_text(statement, 1);
        const unsigned char* createdAtText = sqlite3_column_text(statement, 3);
        request.senderUsername = senderText ? reinterpret_cast<const char*>(senderText) : "";
        request.nickname = nicknameText ? reinterpret_cast<const char*>(nicknameText) : request.senderUsername;
        request.avatarSeed = sqlite3_column_int(statement, 2);
        request.createdAt = createdAtText ? reinterpret_cast<const char*>(createdAtText) : "";
        requests.push_back(request);
    }
    sqlite3_finalize(statement);
    return true;
}

bool DbManager::respondToFriendRequest(const std::string& receiverUsername,
                                       const std::string& senderUsername, bool accepted) {
    if (receiverUsername.empty() || senderUsername.empty())
        return false;
    if (sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;

    sqlite3_stmt* updateStatement = nullptr;
    const char* updateSql =
        "UPDATE friend_requests SET status = ? WHERE sender_username = ? AND receiver_username = ? "
        "AND status = 'pending';";
    bool succeeded = sqlite3_prepare_v2(db, updateSql, -1, &updateStatement, nullptr) == SQLITE_OK;
    if (succeeded) {
        sqlite3_bind_text(updateStatement, 1, accepted ? "accepted" : "rejected", -1, SQLITE_STATIC);
        sqlite3_bind_text(updateStatement, 2, senderUsername.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(updateStatement, 3, receiverUsername.c_str(), -1, SQLITE_TRANSIENT);
        succeeded = sqlite3_step(updateStatement) == SQLITE_DONE && sqlite3_changes(db) == 1;
    }
    if (updateStatement)
        sqlite3_finalize(updateStatement);
    if (succeeded && accepted)
        succeeded = addContact(senderUsername, receiverUsername)
            && addContact(receiverUsername, senderUsername);

    sqlite3_exec(db, succeeded ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
    return succeeded;
}

bool DbManager::findUser(const std::string& username, std::string& passwordHash) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT password FROM users WHERE username = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* passwordText = sqlite3_column_text(statement, 0);
        if (passwordText) passwordHash = reinterpret_cast<const char*>(passwordText);
        found = true;
    }
    sqlite3_finalize(statement);
    return found;
}

bool DbManager::updatePassword(const std::string& username, const std::string& newPasswordHash) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "UPDATE users SET password = ? WHERE username = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, newPasswordHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

// 保存聊天记录，并回填数据库生成的消息编号和发送时间。
bool DbManager::saveMessage(ChatMessage& message) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO messages (sender, receiver, content) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, message.sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, message.receiver.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, message.content.c_str(), -1, SQLITE_TRANSIENT);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!succeeded) return false;

    message.id = sqlite3_last_insert_rowid(db);
    statement = nullptr;
    const char* timeSql = "SELECT sent_at FROM messages WHERE id = ?;";
    if (sqlite3_prepare_v2(db, timeSql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(statement, 1, message.id);
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* timeText = sqlite3_column_text(statement, 0);
        if (timeText) message.sentAt = reinterpret_cast<const char*>(timeText);
    }
    sqlite3_finalize(statement);
    return true;
}

// 查询当前账号与指定联系人的双向聊天记录。
bool DbManager::loadConversation(const std::string& username, const std::string& peer,
                                 std::vector<ChatMessage>& messages) {
    messages.clear();
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT id, sender, receiver, content, sent_at FROM messages "
        "WHERE (sender = ? AND receiver = ?) OR (sender = ? AND receiver = ?) "
        "ORDER BY id ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, peer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, peer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, username.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(statement) == SQLITE_ROW) {
        ChatMessage message;
        message.id = sqlite3_column_int64(statement, 0);
        const unsigned char* senderText = sqlite3_column_text(statement, 1);
        const unsigned char* receiverText = sqlite3_column_text(statement, 2);
        const unsigned char* contentText = sqlite3_column_text(statement, 3);
        const unsigned char* timeText = sqlite3_column_text(statement, 4);
        message.sender = senderText ? reinterpret_cast<const char*>(senderText) : "";
        message.receiver = receiverText ? reinterpret_cast<const char*>(receiverText) : "";
        message.content = contentText ? reinterpret_cast<const char*>(contentText) : "";
        message.sentAt = timeText ? reinterpret_cast<const char*>(timeText) : "";
        messages.push_back(message);
    }
    sqlite3_finalize(statement);
    return true;
}

bool DbManager::deleteConversation(const std::string& username, const std::string& peer) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "DELETE FROM messages WHERE (sender = ? AND receiver = ?) OR (sender = ? AND receiver = ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, peer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, peer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool DbManager::deleteAllMessages(const std::string& username) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "DELETE FROM messages WHERE sender = ? OR receiver = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    return succeeded;
}

bool DbManager::saveCallRecord(const CallRecord& record) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT OR REPLACE INTO call_history "
        "(call_id, caller, callee, state, created_at, accepted_at, connected_at, "
        " ended_at, duration, end_reason) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, record.callId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record.caller.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, record.callee.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, record.state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, record.createdAt);
    sqlite3_bind_int64(stmt, 6, record.acceptedAt);
    sqlite3_bind_int64(stmt, 7, record.connectedAt);
    sqlite3_bind_int64(stmt, 8, record.endedAt);
    sqlite3_bind_int64(stmt, 9, record.duration);
    sqlite3_bind_text(stmt, 10, record.endReason.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool DbManager::loadCallRecords(const std::string& username, int limit,
                                 std::vector<CallRecord>& records) {
    records.clear();
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT call_id, caller, callee, state, created_at, accepted_at, connected_at, "
        "ended_at, duration, end_reason FROM call_history "
        "WHERE caller = ? OR callee = ? "
        "ORDER BY ended_at DESC LIMIT ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit > 0 ? limit : 50);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CallRecord r;
        r.callId     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        r.caller     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.callee     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        r.state      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.createdAt  = sqlite3_column_int64(stmt, 4);
        r.acceptedAt = sqlite3_column_int64(stmt, 5);
        r.connectedAt= sqlite3_column_int64(stmt, 6);
        r.endedAt    = sqlite3_column_int64(stmt, 7);
        r.duration   = sqlite3_column_int64(stmt, 8);
        r.endReason  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        records.push_back(r);
    }
    sqlite3_finalize(stmt);
    return true;
}

// ============================================================
//  loginlog — single-account single-login
// ============================================================

bool DbManager::resetAllLoginStatus() {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE loginlog SET status='下线', updated_at=datetime('now','localtime') WHERE status='登录';";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Log::error("resetAllLoginStatus: prepare failed");
        return false;
    }
    sqlite3_step(stmt);
    const int changed = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    if (changed > 0)
        Log::info("resetAllLoginStatus: reset " + std::to_string(changed) + " login(s) to offline");
    return true;
}

bool DbManager::tryLogin(const std::string& username) {
    if (username.empty()) return false;

    // BEGIN IMMEDIATE prevents concurrent writes for the same row
    char* errMsg = nullptr;
    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Log::error(std::string("tryLogin BEGIN failed: ") + (errMsg ? errMsg : "unknown"));
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    // Step 1: check current status
    sqlite3_stmt* stmt = nullptr;
    const char* selectSql = "SELECT status FROM loginlog WHERE username = ?;";
    if (sqlite3_prepare_v2(db, selectSql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        Log::error("tryLogin select prepare failed");
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    bool exists = false;
    std::string currentStatus;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = true;
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) currentStatus = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);

    if (exists && currentStatus == "登录") {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        Log::info("tryLogin: username=" + username + " already logged in, rejected");
        return false;
    }

    // Step 2: upsert to "登录"
    if (exists) {
        const char* updateSql =
            "UPDATE loginlog SET status='登录', updated_at=datetime('now','localtime') WHERE username=?;";
        if (sqlite3_prepare_v2(db, updateSql, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            Log::error("tryLogin update prepare failed");
            return false;
        }
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        const char* insertSql =
            "INSERT INTO loginlog (username, status, updated_at) VALUES (?, '登录', datetime('now','localtime'));";
        if (sqlite3_prepare_v2(db, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            Log::error("tryLogin insert prepare failed");
            return false;
        }
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    }

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (ok) {
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        Log::info("tryLogin: username=" + username + " => 登录");
    } else {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        Log::error("tryLogin: username=" + username + " upsert failed");
    }
    return ok;
}

bool DbManager::setOffline(const std::string& username) {
    if (username.empty()) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE loginlog SET status='下线', updated_at=datetime('now','localtime') WHERE username=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Log::error("setOffline prepare failed");
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    const int changed = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (changed > 0)
        Log::info("setOffline: username=" + username + " => 下线");
    else
        Log::info("setOffline: username=" + username + " no row updated (idempotent)");
    return ok;
}

bool DbManager::initLoginlogForUser(const std::string& username) {
    if (username.empty()) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT OR IGNORE INTO loginlog (username, status, updated_at) VALUES (?, '下线', datetime('now','localtime'));";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Log::error("initLoginlogForUser prepare failed");
        return false;
    }
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);

    if (ok)
        Log::info("initLoginlogForUser: username=" + username + " => 下线");
    return ok;
}

void DbManager::close() {
    if (db) { sqlite3_close(db); db = nullptr; }
}
