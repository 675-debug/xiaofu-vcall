#include "ThreadPool.h"

#include "../db/DbManager.h"
#include "../util/Log.h"
#include <condition_variable>
#include <exception>

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
    if (!database.open(databasePath) || !database.createTables()) {
        Log::error("worker database initialization failed, index="
                   + std::to_string(workerIndex));
        return;
    }
    Log::info("database worker ready, index=" + std::to_string(workerIndex));

    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCondition.wait(lock, [this]() {
                return stopping.load() || !tasks.empty();
            });
            if (stopping.load() && tasks.empty())
                return;
            task = std::move(tasks.front());
            tasks.pop();
        }

        try {
            task(database);
        } catch (const std::exception& error) {
            Log::error(std::string("worker task exception: ") + error.what());
        } catch (...) {
            Log::error("worker task unknown exception");
        }
    }
}
