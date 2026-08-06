#pragma once
#include <string>

#include "../protocol/ResultCode.h"

class DbManager;

class ForgotHandler {
public:
    explicit ForgotHandler(DbManager* manager);
    ResultCode handleForgot(const std::string& username, const std::string& newPassword);
private:
    DbManager* db;
};