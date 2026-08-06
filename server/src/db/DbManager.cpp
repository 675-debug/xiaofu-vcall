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
    return true;
}

bool DbManager::createTables() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password TEXT NOT NULL,"
        "email TEXT NOT NULL DEFAULT '',"
        "created_at TEXT DEFAULT (datetime('now')));";
    char* errorMessage = nullptr;
    int resultCode = sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage);
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
    return true;
}

bool DbManager::insertUser(const std::string& username, const std::string& passwordHash, const std::string& email) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "INSERT INTO users (username, password, email) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, passwordHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, email.c_str(), -1, SQLITE_TRANSIENT);
    const bool succeeded = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
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

void DbManager::close() {
    if (db) { sqlite3_close(db); db = nullptr; }
}
