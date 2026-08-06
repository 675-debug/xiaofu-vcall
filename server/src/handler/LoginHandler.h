#pragma once
#include <string>

#include "../protocol/ResultCode.h"

class DbManager;

class LoginHandler {
public:
    explicit LoginHandler(DbManager* manager);
    ResultCode handleLogin(const std::string& username, const std::string& password);
private:
    DbManager* db;
};