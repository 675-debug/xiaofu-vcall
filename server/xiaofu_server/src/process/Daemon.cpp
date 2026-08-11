#include "Daemon.h"

#include "../util/Log.h"
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool redirectStandardDescriptors() {
    const int nullFd = ::open("/dev/null", O_RDWR);
    if (nullFd < 0)
        return false;
    const bool succeeded = ::dup2(nullFd, STDIN_FILENO) >= 0
        && ::dup2(nullFd, STDOUT_FILENO) >= 0
        && ::dup2(nullFd, STDERR_FILENO) >= 0;
    if (nullFd > STDERR_FILENO)
        ::close(nullFd);
    return succeeded;
}
}

bool daemonizeProcess() {
    const pid_t firstChild = ::fork();
    if (firstChild < 0)
        return false;
    if (firstChild > 0)
        _exit(EXIT_SUCCESS);

    if (::setsid() < 0)
        return false;
    std::signal(SIGHUP, SIG_IGN);

    const pid_t secondChild = ::fork();
    if (secondChild < 0)
        return false;
    if (secondChild > 0)
        _exit(EXIT_SUCCESS);

    ::umask(0);
    if (::chdir("/") != 0)
        return false;
    return redirectStandardDescriptors();
}
