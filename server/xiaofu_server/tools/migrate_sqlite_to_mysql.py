#!/usr/bin/env python3
"""SQLite -> MySQL 迁移/校验工具（只读 SQLite 源，写入 MySQL 目标）。

用法：
  python3 migrate_sqlite_to_mysql.py --help
  python3 migrate_sqlite_to_mysql.py --reset            # 清空目标库业务表并重建空 schema
  python3 migrate_sqlite_to_mysql.py --migrate          # 从 SQLite 快照迁移数据到 MySQL
  python3 migrate_sqlite_to_mysql.py --verify           # 校验行数与关键字段一致
  python3 migrate_sqlite_to_mysql.py --reset --migrate --verify   # 一条龙（测试环境）

连接参数来自环境变量 XIAOFU_MYSQL_HOST/PORT/USER/PASSWORD/DATABASE，
默认与 /etc/xiaofu-server.env 一致（127.0.0.1:3306, user=xiaofu, db=xiaofu）。
SQLite 源默认对生产库做一致快照（sqlite backup API，只读）；可用 --source 指定文件。
本脚本不输出任何密码。
"""
import argparse
import os
import sqlite3
import sys
import tempfile

sys.stdout.reconfigure(line_buffering=True)

PROD_DB = "/var/lib/xiaofu-vcall/xiaofu.db"

TABLES = ["users", "contacts", "friend_requests", "messages", "call_history", "loginlog"]

CREATE_SQL = {
    "users": (
        "CREATE TABLE IF NOT EXISTS users ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "username VARCHAR(64) UNIQUE NOT NULL,"
        "password VARCHAR(128) NOT NULL,"
        "email VARCHAR(255) NOT NULL DEFAULT '',"
        "created_at DATETIME NOT NULL DEFAULT NOW(),"
        "nickname VARCHAR(64) NOT NULL DEFAULT '',"
        "avatar_seed INT NOT NULL DEFAULT 0"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    ),
    "contacts": (
        "CREATE TABLE IF NOT EXISTS contacts ("
        "owner_username VARCHAR(64) NOT NULL,"
        "contact_username VARCHAR(64) NOT NULL,"
        "PRIMARY KEY(owner_username, contact_username)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    ),
    "friend_requests": (
        "CREATE TABLE IF NOT EXISTS friend_requests ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "sender_username VARCHAR(64) NOT NULL,"
        "receiver_username VARCHAR(64) NOT NULL,"
        "status VARCHAR(16) NOT NULL DEFAULT 'pending',"
        "created_at DATETIME NOT NULL DEFAULT NOW()"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    ),
    "messages": (
        "CREATE TABLE IF NOT EXISTS messages ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "sender VARCHAR(64) NOT NULL,"
        "receiver VARCHAR(64) NOT NULL,"
        "content TEXT NOT NULL,"
        "sent_at DATETIME NOT NULL DEFAULT NOW()"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    ),
    "call_history": (
        "CREATE TABLE IF NOT EXISTS call_history ("
        "call_id VARCHAR(64) PRIMARY KEY,"
        "caller VARCHAR(64) NOT NULL,"
        "callee VARCHAR(64) NOT NULL,"
        "state VARCHAR(16) NOT NULL,"
        "created_at BIGINT NOT NULL,"
        "accepted_at BIGINT NOT NULL DEFAULT 0,"
        "connected_at BIGINT NOT NULL DEFAULT 0,"
        "ended_at BIGINT NOT NULL,"
        "duration BIGINT NOT NULL DEFAULT 0,"
        "end_reason VARCHAR(64) NOT NULL"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    ),
    "loginlog": (
        "CREATE TABLE IF NOT EXISTS loginlog ("
        "username VARCHAR(64) PRIMARY KEY,"
        "status VARCHAR(16) NOT NULL,"
        "updated_at DATETIME NOT NULL DEFAULT NOW()"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    ),
}

# 需要保留原 id 的表（AUTO_INCREMENT 主键）
ID_PRESERVE = {"users", "friend_requests", "messages"}


def env(name, default):
    return os.environ.get(name) or default


def mysql_connect():
    import pymysql
    return pymysql.connect(
        host=env("XIAOFU_MYSQL_HOST", "127.0.0.1"),
        port=int(env("XIAOFU_MYSQL_PORT", "3306")),
        user=env("XIAOFU_MYSQL_USER", "xiaofu"),
        password=env("XIAOFU_MYSQL_PASSWORD", ""),
        database=env("XIAOFU_MYSQL_DATABASE", "xiaofu"),
        charset="utf8mb4",
        autocommit=True,
    )


def sqlite_source(path=None):
    """返回一个一致的单文件 SQLite 快照路径（用 backup API，源只读）。"""
    if path:
        if not os.path.exists(path):
            sys.exit(f"error: source sqlite not found: {path}")
        return path
    fd, snapshot = tempfile.mkstemp(prefix="xiaofu-migrate-", suffix=".db")
    os.close(fd)
    src = sqlite3.connect("file:%s?mode=ro" % PROD_DB, uri=True)
    dst = sqlite3.connect(snapshot)
    try:
        src.backup(dst)
    finally:
        dst.close()
        src.close()
    print(f"sqlite snapshot: {snapshot} (consistent copy of {PROD_DB})")
    return snapshot


def reset(mysql):
    with mysql.cursor() as cur:
        for table in TABLES:
            cur.execute(f"DROP TABLE IF EXISTS {table}")
    print("mysql: dropped business tables")


def create_schema(mysql):
    with mysql.cursor() as cur:
        for table, sql in CREATE_SQL.items():
            cur.execute(sql)
    print("mysql: schema created")


def row_counts(cur, table):
    cur.execute(f"SELECT COUNT(*) FROM {table}")
    return cur.fetchone()[0]


def migrate(sqlite_path, mysql):
    src = sqlite3.connect("file:%s?mode=ro" % sqlite_path, uri=True)
    src.row_factory = sqlite3.Row
    sc = src.cursor()
    try:
        with mysql.cursor() as cur:
            for table in TABLES:
                columns = [r[1] for r in sc.execute(f"PRAGMA table_info({table})").fetchall()]
                col_list = ",".join(columns)
                placeholders = ",".join(["%s"] * len(columns))
                sql = f"INSERT INTO {table} ({col_list}) VALUES ({placeholders})"
                for row in sc.execute(f"SELECT * FROM {table}"):
                    values = [row[c] for c in columns]
                    cur.execute(sql, values)
                print(f"migrated {table}: {row_counts(cur, table)} rows")
    finally:
        src.close()


def verify(sqlite_path, mysql):
    src = sqlite3.connect("file:%s?mode=ro" % sqlite_path, uri=True)
    sc = src.cursor()
    try:
        with mysql.cursor() as cur:
            all_ok = True
            for table in TABLES:
                s_count = sc.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
                m_count = row_counts(cur, table)
                ok = s_count == m_count
                all_ok = all_ok and ok
                print(f"verify {table}: sqlite={s_count} mysql={m_count} {'OK' if ok else 'MISMATCH'}")
            # 关键字段一致性
            for table, key_cols in (
                ("users", ("username", "password")),
                ("messages", ("id", "sender", "receiver", "content")),
                ("call_history", ("call_id", "caller", "callee", "ended_at", "end_reason")),
                ("loginlog", ("username", "status")),
            ):
                cols = ",".join(key_cols)
                s_rows = {tuple(r) for r in sc.execute(f"SELECT {cols} FROM {table} ORDER BY 1")}
                cur.execute(f"SELECT {cols} FROM {table} ORDER BY 1")
                m_rows = {tuple(r) for r in cur.fetchall()}
                ok = s_rows == m_rows
                all_ok = all_ok and ok
                print(f"verify {table} key fields: {'OK' if ok else 'MISMATCH'}")
            if not all_ok:
                sys.exit("verify failed")
            print("verify: ALL OK")
    finally:
        src.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default=None, help="sqlite 文件；缺省自动对生产库做一致快照")
    parser.add_argument("--reset", action="store_true", help="先清空并重建目标库 schema")
    parser.add_argument("--migrate", action="store_true", help="执行数据迁移")
    parser.add_argument("--verify", action="store_true", help="校验行数与关键字段")
    args = parser.parse_args()

    if not (args.reset or args.migrate or args.verify):
        parser.print_help()
        sys.exit(1)

    mysql = mysql_connect()
    print(f"mysql connected: {env('XIAOFU_MYSQL_DATABASE', 'xiaofu')}@{env('XIAOFU_MYSQL_HOST', '127.0.0.1')}")

    if args.reset:
        reset(mysql)
        create_schema(mysql)

    sqlite_path = sqlite_source(args.source)
    if args.migrate:
        migrate(sqlite_path, mysql)
    if args.verify:
        verify(sqlite_path, mysql)
    mysql.close()
    if args.source is None and sqlite_path.startswith(tempfile.gettempdir()):
        os.unlink(sqlite_path)


if __name__ == "__main__":
    main()
