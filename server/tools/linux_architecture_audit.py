from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require_tokens(relative_path: str, tokens: tuple[str, ...]) -> None:
    path = ROOT / relative_path
    if not path.exists():
        raise SystemExit(f"missing: {relative_path}")

    content = path.read_text(encoding="utf-8")
    for token in tokens:
        if token not in content:
            raise SystemExit(f"{relative_path} missing token: {token}")


def main() -> None:
    require_tokens(
        "src/net/EpollLoop.cpp",
        ("epoll_create1", "epoll_ctl", "epoll_wait", "EPOLLET"),
    )
    require_tokens(
        "src/concurrency/ThreadPool.cpp",
        ("std::thread", "condition_variable"),
    )
    require_tokens(
        "src/concurrency/CompletionDispatcher.cpp",
        ("eventfd",),
    )
    require_tokens(
        "src/process/Daemon.cpp",
        ("fork", "setsid"),
    )

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    for token in (
        "CXX_STANDARD 17",
        "find_package(Threads REQUIRED)",
        "find_package(SQLite3 REQUIRED)",
    ):
        if token not in cmake:
            raise SystemExit(f"CMake missing token: {token}")

    print("Linux architecture audit passed")


if __name__ == "__main__":
    main()
