#include "CompletionDispatcher.h"

#include "../util/Log.h"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>

CompletionDispatcher::CompletionDispatcher()
    : eventFileDescriptor(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {
    if (eventFileDescriptor < 0)
        Log::error(std::string("eventfd failed: ") + std::strerror(errno));
}

CompletionDispatcher::~CompletionDispatcher() {
    if (eventFileDescriptor >= 0)
        ::close(eventFileDescriptor);
}

bool CompletionDispatcher::valid() const {
    return eventFileDescriptor >= 0;
}

int CompletionDispatcher::fd() const {
    return eventFileDescriptor;
}

bool CompletionDispatcher::push(Completion completion) {
    if (!valid() || !completion)
        return false;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        completions.push(std::move(completion));
    }

    const std::uint64_t wakeValue = 1;
    const ssize_t written = ::write(eventFileDescriptor, &wakeValue, sizeof(wakeValue));
    if (written == static_cast<ssize_t>(sizeof(wakeValue)) || errno == EAGAIN)
        return true;
    Log::error(std::string("eventfd write failed: ") + std::strerror(errno));
    return false;
}

void CompletionDispatcher::drain() {
    if (!valid())
        return;

    std::uint64_t wakeValue = 0;
    while (::read(eventFileDescriptor, &wakeValue, sizeof(wakeValue)) > 0) {
    }
    if (errno != EAGAIN)
        Log::error(std::string("eventfd read failed: ") + std::strerror(errno));

    std::queue<Completion> ready;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        ready.swap(completions);
    }
    while (!ready.empty()) {
        Completion completion = std::move(ready.front());
        ready.pop();
        completion();
    }
}
