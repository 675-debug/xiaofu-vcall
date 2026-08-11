#pragma once

#include <functional>
#include <mutex>
#include <queue>

class CompletionDispatcher {
public:
    using Completion = std::function<void()>;

    CompletionDispatcher();
    ~CompletionDispatcher();

    CompletionDispatcher(const CompletionDispatcher&) = delete;
    CompletionDispatcher& operator=(const CompletionDispatcher&) = delete;

    bool valid() const;
    int fd() const;
    bool push(Completion completion);
    void drain();

private:
    int eventFileDescriptor = -1;
    std::mutex queueMutex;
    std::queue<Completion> completions;
};
