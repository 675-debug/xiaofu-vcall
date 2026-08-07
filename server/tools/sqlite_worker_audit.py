from pathlib import Path


SOURCE = Path(__file__).resolve().parents[1] / "src/db/DbManager.cpp"


def main() -> None:
    content = SOURCE.read_text(encoding="utf-8")
    for token in (
        "PRAGMA journal_mode=WAL",
        "PRAGMA synchronous=NORMAL",
        "PRAGMA busy_timeout=3000",
        "PRAGMA foreign_keys=ON",
    ):
        if token not in content:
            raise SystemExit(f"DbManager.cpp missing token: {token}")
    print("SQLite worker audit passed")


if __name__ == "__main__":
    main()
