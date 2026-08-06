#pragma once
#include <string>
#include <sqlite3.h>

class DbManager {
public:
    DbManager();
    ~DbManager();

    bool open(const std::string& dbPath);
    bool createTables();
    bool insertUser(const std::string& username, const std::string& passwordHash, const std::string& email);
    bool findUser(const std::string& username, std::string& passwordHash);
    bool updatePassword(const std::string& username, const std::string& newPasswordHash);
    void close();

private:
    sqlite3* db;
};