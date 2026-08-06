#include "LoginHandler.h"
#include "../db/DbManager.h"
#include "../db/PasswordHasher.h"

LoginHandler::LoginHandler(DbManager* manager) : db(manager) {}

ResultCode LoginHandler::handleLogin(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) return ResultCode::Failed;
    std::string passwordHash;
    if (!db->findUser(username, passwordHash)) return ResultCode::UserNotFound;
    return passwordHash == PasswordHasher::sha256Hex(password)
        ? ResultCode::Ok : ResultCode::WrongPassword;
}
