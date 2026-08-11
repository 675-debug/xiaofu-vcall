#include "Log.h"
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {
std::mutex logMutex;
FILE* logFile = nullptr;

void print(const char* level, const std::string& message) {
    const std::time_t currentTime = std::time(nullptr);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", std::localtime(&currentTime));
    char line[4096];
    const int len = std::snprintf(line, sizeof(line), "[%s] %s %s\n",
                                   level, timestamp, message.c_str());

    std::lock_guard<std::mutex> lock(logMutex);
    // always write to stdout (visible when running foreground)
    std::fwrite(line, 1, static_cast<std::size_t>(len), stdout);
    std::fflush(stdout);
    // also write to log file (survives daemon stdout redirect)
    if (logFile) {
        std::fwrite(line, 1, static_cast<std::size_t>(len), logFile);
        std::fflush(logFile);
    }
}
} // namespace

void Log::initFile(const std::string& path) {
    logFile = std::fopen(path.c_str(), "a");
    if (logFile)
        std::setbuf(logFile, nullptr); // unbuffered for real-time tail -f
}

void Log::debug(const std::string& message) { print("DEBUG", message); }
void Log::info(const std::string& message)  { print("INFO", message); }
void Log::warn(const std::string& message)  { print("WARN", message); }
void Log::error(const std::string& message) { print("ERROR", message); }
