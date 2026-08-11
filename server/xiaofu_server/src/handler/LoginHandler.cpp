#include "LoginHandler.h"
#include "../db/DbManager.h"
#include "../db/PasswordHasher.h"
#include "../util/Log.h"

LoginHandler::LoginHandler(DbManager* manager) : db(manager) {}

ResultCode LoginHandler::handleLogin(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) return ResultCode::Failed;
    std::string passwordHash;
    if (!db->findUser(username, passwordHash)) return ResultCode::UserNotFound;
    if (passwordHash != PasswordHasher::sha256Hex(password))
        return ResultCode::WrongPassword;

    // credential OK — atomically check and set loginlog
    if (!db->tryLogin(username)) {
        Log::info("login rejected (already logged in): username=" + username);
        return ResultCode::AccountAlreadyLoggedIn;
    }
    Log::info("login success: username=" + username);
    return ResultCode::Ok;
}
