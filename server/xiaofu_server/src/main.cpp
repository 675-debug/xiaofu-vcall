#include "ServerApp.h"
#include "ServerOptions.h"
#include "process/Daemon.h"
#include "util/Log.h"

#include <filesystem>
#include <string>

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

    // DbManager 当前使用 MySQL；该值仅作为连接重试时的实例标识保留。
    ServerApp server(options.dataDirectory.string(), options.workerCount);
    if (!server.start(options.listenAddress, options.port))
        return 1;
    server.run();
    return 0;
}
