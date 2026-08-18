#include "ServerOptions.h"

#include <charconv>
#include <cstdlib>
#include <string>
#include <system_error>

namespace {

const char* environmentValue(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

bool parseBoundedNumber(const std::string& text, unsigned int maximum,
    unsigned int& value)
{
    unsigned int parsedValue = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsedValue);
    if (result.ec != std::errc() || result.ptr != end || parsedValue == 0
        || parsedValue > maximum)
        return false;
    value = parsedValue;
    return true;
}

} // namespace

bool parseOptions(int argc, char* argv[], ServerOptions& options)
{
    std::string port = environmentValue("XIAOFU_SERVER_PORT", "9000");
    std::string workers = environmentValue("XIAOFU_SERVER_WORKERS", "2");

    options.listenAddress = environmentValue("XIAOFU_SERVER_HOST", "0.0.0.0");
    options.dataDirectory = environmentValue("XIAOFU_DATA_DIR", "");
    if (std::getenv("XIAOFU_DATA_DIR") == nullptr)
        options.dataDirectory = std::filesystem::current_path() / "db";
    options.daemonMode = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--daemon") {
            options.daemonMode = true;
        } else if (argument == "--host" && index + 1 < argc) {
            options.listenAddress = argv[++index];
        } else if (argument == "--port" && index + 1 < argc) {
            port = argv[++index];
        } else if (argument == "--workers" && index + 1 < argc) {
            workers = argv[++index];
        } else if (argument == "--data-dir" && index + 1 < argc) {
            options.dataDirectory = argv[++index];
        } else {
            return false;
        }
    }

    unsigned int parsedPort = 0;
    unsigned int parsedWorkers = 0;
    if (!parseBoundedNumber(port, 65535, parsedPort)
        || !parseBoundedNumber(workers, 256, parsedWorkers))
        return false;

    options.port = static_cast<int>(parsedPort);
    options.workerCount = static_cast<std::size_t>(parsedWorkers);
    options.dataDirectory = std::filesystem::absolute(options.dataDirectory).lexically_normal();
    return true;
}
