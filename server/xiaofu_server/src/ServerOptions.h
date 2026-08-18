#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct ServerOptions {
    std::string listenAddress = "0.0.0.0";
    int port = 9000;
    std::size_t workerCount = 2;
    std::filesystem::path dataDirectory;
    bool daemonMode = false;
};

bool parseOptions(int argc, char* argv[], ServerOptions& options);
