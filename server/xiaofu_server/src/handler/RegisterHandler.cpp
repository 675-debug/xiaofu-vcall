#include "RegisterHandler.h"
#include "../db/DbManager.h"
#include "../db/PasswordHasher.h"
#include "../util/Validator.h"

RegisterHandler::RegisterHandler(DbManager* manager) : db(manager) {}

ResultCode RegisterHandler::handleRegister(const std::string& username, const std::string& email,
                                           const std::string& password, const std::string& nickname, int avatarSeed) {
    if (username.empty() || email.empty() || password.empty()) return ResultCode::Failed;
    if (!Validator::isValidEmail(email)) return ResultCode::InvalidEmail;
    if (!Validator::isValidPassword(password)) return ResultCode::InvalidPassword;
    std::string passwordHash;
    if (db->findUser(username, passwordHash)) return ResultCode::UserExists;
    const std::string displayName = nickname.empty() ? username : nickname;
    if (!db->insertUser(username, PasswordHasher::sha256Hex(password), email, displayName, avatarSeed))
        return ResultCode::Failed;
    // Initialize loginlog: new user starts offline
    db->initLoginlogForUser(username);
    return ResultCode::Ok;
}
