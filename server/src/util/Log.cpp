#include "Log.h"
#include <cstdio>
#include <ctime>

namespace {
void print(const char* level, const std::string& message) {
    const std::time_t currentTime = std::time(nullptr);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", std::localtime(&currentTime));
    std::printf("[%s] %s %s\n", level, timestamp, message.c_str());
    std::fflush(stdout);
}
} // namespace

void Log::info(const std::string& message) { print("INFO", message); }
void Log::error(const std::string& message) { print("ERROR", message); }
