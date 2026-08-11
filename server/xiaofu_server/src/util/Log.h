#pragma once
#include <string>

class Log {
public:
    static void initFile(const std::string& path);
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);
};
