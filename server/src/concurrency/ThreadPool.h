#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class DbManager;

class ThreadPool {
public:
    using Task = std::function<void(DbManager&)>;

    ThreadPool(std::size_t workerCount, std::string databasePath);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool submit(Task task);
    std::size_t workerCount() const;

private:
    void workerLoop(std::size_t workerIndex);

    std::string databasePath;
    std::vector<std::thread> workers;
    std::queue<Task> tasks;
    mutable std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::atomic<bool> stopping{false};
};
