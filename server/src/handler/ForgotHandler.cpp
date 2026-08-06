#include "ForgotHandler.h"
#include "../db/DbManager.h"
#include "../db/PasswordHasher.h"
#include "../util/Validator.h"

ForgotHandler::ForgotHandler(DbManager* manager) : db(manager) {}

ResultCode ForgotHandler::handleForgot(const std::string& username, const std::string& newPassword) {
    if (username.empty() || newPassword.empty()) return ResultCode::Failed;
    if (!Validator::isValidPassword(newPassword)) return ResultCode::InvalidPassword;
    std::string passwordHash;
    if (!db->findUser(username, passwordHash)) return ResultCode::UserNotFound;
    return db->updatePassword(username, PasswordHasher::sha256Hex(newPassword)) ? ResultCode::Ok : ResultCode::Failed;
}
