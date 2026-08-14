#include "ThreadPool.h"

#include "../db/DbManager.h"
#include "../util/Log.h"
#include <chrono>
#include <condition_variable>
#include <exception>
#include <thread>

ThreadPool::ThreadPool(std::size_t workerCount, std::string databasePath)
    : databasePath(std::move(databasePath)) {
    if (workerCount == 0)
        workerCount = 1;
    workers.reserve(workerCount);
    for (std::size_t index = 0; index < workerCount; ++index)
        workers.emplace_back(std::thread(&ThreadPool::workerLoop, this, index));
}

ThreadPool::~ThreadPool() {
    stopping.store(true);
    queueCondition.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable())
            worker.join();
    }
}

bool ThreadPool::submit(Task task) {
    if (!task || stopping.load())
        return false;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (stopping.load())
            return false;
        tasks.push(std::move(task));
    }
    queueCondition.notify_one();
    return true;
}

std::size_t ThreadPool::workerCount() const {
    return workers.size();
}

void ThreadPool::workerLoop(std::size_t workerIndex) {
    DbManager database;

    // 初始连接失败时持续重试，直到 MySQL 可用或服务停止。
    while (!stopping.load()) {
        if (database.open(databasePath) && database.createTables())
            break;
        Log::error("worker database connection failed, retrying in 3s, index="
                   + std::to_string(workerIndex));
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    if (stopping.load())
        return;
    Log::info("database worker ready, index=" + std::to_string(workerIndex));

    while (true) {
        Task task;
        bool hasTask = false;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCondition.wait_for(lock, std::chrono::seconds(30), [this]() {
                return stopping.load() || !tasks.empty();
            });
            if (stopping.load() && tasks.empty())
                return;
            if (!tasks.empty()) {
                task = std::move(tasks.front());
                tasks.pop();
                hasTask = true;
            }
        }

        if (!hasTask) {
            // 空闲时周期性检查 MySQL 连接，断线则自动重连。
            if (!database.ping())
                Log::warn("mysql health check failed, will retry on next check");
            continue;
        }

        // 任务已经出队后不能静默丢弃。数据库短暂不可用时保留当前任务，
        // 等连接恢复后再执行；服务停止时才放弃尚未执行的任务。
        while (!stopping.load() && !database.ping()) {
            Log::error("mysql unavailable, retrying current DB task in 1s");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (stopping.load())
            return;

        try {
            task(database);
        } catch (const std::exception& error) {
            Log::error(std::string("worker task exception: ") + error.what());
        } catch (...) {
            Log::error("worker task unknown exception");
        }
    }
}
