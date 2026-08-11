#include <sqlite3.h>
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    if (argc < 2) return 1;
    sqlite3* db = nullptr;
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) { std::printf("open fail\n"); return 1; }
    if (argc >= 3 && std::string(argv[2]) == "--reset") {
        // 清空测试库：删除业务表，服务端下次启动会自动建表。
        const char* resetSql =
            "DROP TABLE IF EXISTS friend_requests;"
            "DROP TABLE IF EXISTS messages;"
            "DROP TABLE IF EXISTS contacts;"
            "DROP TABLE IF EXISTS users;";
        char* errorMessage = nullptr;
        const int resultCode = sqlite3_exec(db, resetSql, nullptr, nullptr, &errorMessage);
        if (resultCode != SQLITE_OK) {
            std::printf("reset fail: %s\n", errorMessage ? errorMessage : "unknown");
            sqlite3_free(errorMessage);
            sqlite3_close(db);
            return 1;
        }
        std::printf("database reset complete\n");
        sqlite3_close(db);
        return 0;
    }
    sqlite3_stmt* st = nullptr;
    const char* sql = "SELECT username, password, length(password) FROM users;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            std::printf("user=%s pass=%s len=%d\n",
                sqlite3_column_text(st, 0), sqlite3_column_text(st, 1), sqlite3_column_int(st, 2));
        }
        sqlite3_finalize(st);
    } else {
        std::printf("prepare fail\n");
    }
    sqlite3_close(db);
    return 0;
}
