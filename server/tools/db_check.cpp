#include <sqlite3.h>
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 2) return 1;
    sqlite3* db = nullptr;
    if (sqlite3_open(argv[1], &db) != SQLITE_OK) { std::printf("open fail\n"); return 1; }
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