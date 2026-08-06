#include "Validator.h"
#include <regex>

bool Validator::isValidPassword(const std::string& password) {
    static const std::regex pattern(R"(^(?=.*[a-z])(?=.*[A-Z]).{6,}$)");
    return std::regex_match(password, pattern);
}

bool Validator::isValidEmail(const std::string& email) {
    static const std::regex pattern(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)");
    return std::regex_match(email, pattern);
}
