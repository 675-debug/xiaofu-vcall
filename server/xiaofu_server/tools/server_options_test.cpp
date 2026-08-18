#include "ServerOptions.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class ScopedEnvironment {
public:
    explicit ScopedEnvironment(std::vector<std::string> names)
    {
        for (const std::string& name : names) {
            const char* value = std::getenv(name.c_str());
            originalValues_.emplace_back(name,
                value ? std::optional<std::string>(value) : std::nullopt);
        }
    }

    ~ScopedEnvironment()
    {
        for (const auto& [name, value] : originalValues_) {
            if (value)
                set(name, *value);
            else
                unset(name);
        }
    }

    static void set(const std::string& name, const std::string& value)
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    static void unset(const std::string& name)
    {
#ifdef _WIN32
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }

private:
    std::vector<std::pair<std::string, std::optional<std::string>>> originalValues_;
};

bool expect(bool condition, const std::string& message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool parse(const std::vector<std::string>& arguments, ServerOptions& options)
{
    std::vector<std::string> storage = arguments;
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (std::string& argument : storage)
        argv.push_back(argument.data());
    return parseOptions(static_cast<int>(argv.size()), argv.data(), options);
}

bool testDefaults()
{
    ScopedEnvironment environment({"XIAOFU_SERVER_HOST", "XIAOFU_SERVER_PORT",
        "XIAOFU_SERVER_WORKERS", "XIAOFU_DATA_DIR"});
    ScopedEnvironment::unset("XIAOFU_SERVER_HOST");
    ScopedEnvironment::unset("XIAOFU_SERVER_PORT");
    ScopedEnvironment::unset("XIAOFU_SERVER_WORKERS");
    ScopedEnvironment::unset("XIAOFU_DATA_DIR");

    ServerOptions options;
    return expect(parse({"xiaofu-server"}, options), "defaults should parse")
        && expect(options.listenAddress == "0.0.0.0", "default host")
        && expect(options.port == 9000, "default port")
        && expect(options.workerCount == 2, "default workers")
        && expect(options.dataDirectory ==
                (std::filesystem::current_path() / "db").lexically_normal(),
            "default data directory")
        && expect(!options.daemonMode, "daemon mode defaults to false");
}

bool testEnvironment()
{
    ScopedEnvironment environment({"XIAOFU_SERVER_HOST", "XIAOFU_SERVER_PORT",
        "XIAOFU_SERVER_WORKERS", "XIAOFU_DATA_DIR"});
    ScopedEnvironment::set("XIAOFU_SERVER_HOST", "127.0.0.2");
    ScopedEnvironment::set("XIAOFU_SERVER_PORT", "9100");
    ScopedEnvironment::set("XIAOFU_SERVER_WORKERS", "12");
    ScopedEnvironment::set("XIAOFU_DATA_DIR", "env-data");

    ServerOptions options;
    return expect(parse({"xiaofu-server"}, options), "environment should parse")
        && expect(options.listenAddress == "127.0.0.2", "environment host")
        && expect(options.port == 9100, "environment port")
        && expect(options.workerCount == 12, "environment workers")
        && expect(options.dataDirectory ==
                std::filesystem::absolute("env-data").lexically_normal(),
            "environment data directory");
}

bool testCommandLineOverridesEnvironment()
{
    ScopedEnvironment environment({"XIAOFU_SERVER_HOST", "XIAOFU_SERVER_PORT",
        "XIAOFU_SERVER_WORKERS", "XIAOFU_DATA_DIR"});
    ScopedEnvironment::set("XIAOFU_SERVER_HOST", "127.0.0.2");
    ScopedEnvironment::set("XIAOFU_SERVER_PORT", "9100");
    ScopedEnvironment::set("XIAOFU_SERVER_WORKERS", "12");
    ScopedEnvironment::set("XIAOFU_DATA_DIR", "env-data");

    ServerOptions options;
    return expect(parse({"xiaofu-server", "--host", "::1", "--port", "9200",
                            "--workers", "256", "--data-dir", "cli-data", "--daemon"},
                      options),
               "command line should parse")
        && expect(options.listenAddress == "::1", "command-line host")
        && expect(options.port == 9200, "command-line port")
        && expect(options.workerCount == 256, "command-line workers")
        && expect(options.dataDirectory ==
                std::filesystem::absolute("cli-data").lexically_normal(),
            "command-line data directory")
        && expect(options.daemonMode, "command-line daemon mode");
}

bool testInvalidNumbers()
{
    ScopedEnvironment environment({"XIAOFU_SERVER_PORT", "XIAOFU_SERVER_WORKERS"});
    ScopedEnvironment::unset("XIAOFU_SERVER_PORT");
    ScopedEnvironment::unset("XIAOFU_SERVER_WORKERS");

    const std::vector<std::vector<std::string>> invalidArguments = {
        {"xiaofu-server", "--port", "0"},
        {"xiaofu-server", "--port", "65536"},
        {"xiaofu-server", "--port", "9000x"},
        {"xiaofu-server", "--workers", "0"},
        {"xiaofu-server", "--workers", "257"},
        {"xiaofu-server", "--workers", "2x"},
    };
    for (const auto& arguments : invalidArguments) {
        ServerOptions options;
        if (!expect(!parse(arguments, options), "invalid command-line number should fail"))
            return false;
    }

    ScopedEnvironment::set("XIAOFU_SERVER_PORT", "9000x");
    ServerOptions options;
    if (!expect(!parse({"xiaofu-server"}, options), "invalid environment port should fail"))
        return false;

    ScopedEnvironment::set("XIAOFU_SERVER_PORT", "9000");
    ScopedEnvironment::set("XIAOFU_SERVER_WORKERS", "257");
    return expect(!parse({"xiaofu-server"}, options),
        "out-of-range environment workers should fail");
}

} // namespace

int main()
{
    if (!testDefaults() || !testEnvironment() || !testCommandLineOverridesEnvironment()
        || !testInvalidNumbers())
        return 1;
    std::cout << "server_options_test passed\n";
    return 0;
}
