#include "ServerApp.h"
#include "process/Daemon.h"
#include "util/Log.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

struct ServerOptions {
    std::string listenAddress = "0.0.0.0";
    int port = 9000;
    std::size_t workerCount = 2;
    std::filesystem::path dataDirectory;
    bool daemonMode = false;
};

bool parsePositiveNumber(const char* text, long long& value)
{
    try {
        std::size_t parsedLength = 0;
        value = std::stoll(text, &parsedLength);
        return parsedLength == std::string(text).size() && value > 0;
    } catch (...) {
        return false;
    }
}

bool parseOptions(int argc, char* argv[], ServerOptions& options)
{
    if (const char* configuredDirectory = std::getenv("XIAOFU_DATA_DIR"))
        options.dataDirectory = configuredDirectory;
    else
        options.dataDirectory = std::filesystem::current_path() / "db";

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--daemon") {
            options.daemonMode = true;
        } else if (argument == "--host" && index + 1 < argc) {
            options.listenAddress = argv[++index];
        } else if (argument == "--data-dir" && index + 1 < argc) {
            options.dataDirectory = argv[++index];
        } else if ((argument == "--port" || argument == "--workers") && index + 1 < argc) {
            long long value = 0;
            if (!parsePositiveNumber(argv[++index], value))
                return false;
            if (argument == "--port") {
                if (value > 65535)
                    return false;
                options.port = static_cast<int>(value);
            } else {
                if (value > 64)
                    return false;
                options.workerCount = static_cast<std::size_t>(value);
            }
        } else {
            return false;
        }
    }
    options.dataDirectory = std::filesystem::absolute(options.dataDirectory).lexically_normal();
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    ServerOptions options;
    if (!parseOptions(argc, argv, options)) {
        Log::error("usage: xiaofu-server [--host IP] [--port PORT] "
                   "[--workers COUNT] [--data-dir PATH] [--daemon]");
        return 2;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(options.dataDirectory, directoryError);
    if (directoryError) {
        Log::error("cannot create data directory: " + options.dataDirectory.string());
        return 1;
    }

    Log::initFile((options.dataDirectory / "server.log").string());

    // systemd 下保持前台运行；只有手动传入 --daemon 才执行传统双 fork。
    if (options.daemonMode && !daemonizeProcess())
        return 1;

    const std::string databasePath = (options.dataDirectory / "xiaofu.db").string();
    ServerApp server(databasePath, options.workerCount);
    if (!server.start(options.listenAddress, options.port))
        return 1;
    server.run();
    return 0;
}
