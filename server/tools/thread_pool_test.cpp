#include "../src/concurrency/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}
}

int main() {
    std::atomic<int> completed{0};
    std::mutex waitMutex;
    std::condition_variable waitCondition;

    ThreadPool pool(2, ":memory:");
    for (int index = 0; index < 20; ++index) {
        check(pool.submit([&](DbManager&) {
            if (++completed == 20)
                waitCondition.notify_one();
        }), "submit task");
    }

    std::unique_lock<std::mutex> lock(waitMutex);
    const bool allCompleted = waitCondition.wait_for(
        lock, std::chrono::seconds(3), [&]() { return completed.load() == 20; });
    check(allCompleted, "two workers execute twenty tasks");

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
