#pragma once
#include <string>

class PasswordHasher {
public:
    static std::string sha256Hex(const std::string& text);
};
