#pragma once
#include <string>

#include "../protocol/ResultCode.h"

class DbManager;

class RegisterHandler {
public:
    explicit RegisterHandler(DbManager* manager);
    ResultCode handleRegister(const std::string& username, const std::string& email,
                              const std::string& password, const std::string& nickname, int avatarSeed);
private:
    DbManager* db;
};
