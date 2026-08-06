#pragma once
#include <string>

// 字段正则校验工具
class Validator {
public:
    // 密码：长度 >= 6，且同时包含大写字母和小写字母
    static bool isValidPassword(const std::string& password);
    // 邮箱：标准邮箱格式
    static bool isValidEmail(const std::string& email);
};
