#include "RegisterHandler.h"
#include "../db/DbManager.h"
#include "../db/PasswordHasher.h"
#include "../util/Validator.h"

RegisterHandler::RegisterHandler(DbManager* manager) : db(manager) {}

ResultCode RegisterHandler::handleRegister(const std::string& username, const std::string& email, const std::string& password) {
    if (username.empty() || email.empty() || password.empty()) return ResultCode::Failed;
    if (!Validator::isValidEmail(email)) return ResultCode::InvalidEmail;
    if (!Validator::isValidPassword(password)) return ResultCode::InvalidPassword;
    std::string passwordHash;
    if (db->findUser(username, passwordHash)) return ResultCode::UserExists;
    return db->insertUser(username, PasswordHasher::sha256Hex(password), email) ? ResultCode::Ok : ResultCode::Failed;
}
