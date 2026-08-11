// 单账号并发登录测试：多个独立 MySQL 连接同时 tryLogin 同一账号，
// 必须恰好一个成功（MySQL 事务 + SELECT ... FOR UPDATE 行锁）。
#include "../src/db/DbManager.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* name) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++failures;
}

std::string uniqueSuffix() {
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
}

int main() {
    const std::string userWithRow = "concurrent_login_a_" + uniqueSuffix();
    const std::string userNoRow = "concurrent_login_b_" + uniqueSuffix();

    DbManager setup;
    if (!setup.open("login-concurrency")) {
        std::printf("FAIL: cannot open database (set XIAOFU_MYSQL_* env)\n");
        return 1;
    }
    setup.createTables();
    check(setup.initLoginlogForUser(userWithRow), "init loginlog row (offline)");

    auto runConcurrent = [](const std::string& username, int threadCount) -> int {
        std::atomic<int> succeeded{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < threadCount; ++i) {
            threads.emplace_back([&]() {
                DbManager db;
                if (!db.open("login-concurrency"))
                    return;
                if (db.tryLogin(username))
                    succeeded.fetch_add(1);
            });
        }
        for (auto& t : threads)
            t.join();
        return succeeded.load();
    };

    // 场景 1：loginlog 行已存在（注册路径），8 个并发登录只能成功 1 个
    const int successWithRow = runConcurrent(userWithRow, 8);
    check(successWithRow == 1,
          "concurrent login with existing row: exactly one succeeds");

    // 场景 2：loginlog 行不存在（存量账号首次登录），4 个并发也只能成功 1 个
    const int successWithoutRow = runConcurrent(userNoRow, 4);
    check(successWithoutRow == 1,
          "concurrent login without row: exactly one succeeds");

    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
