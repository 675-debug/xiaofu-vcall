from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/net/Connection.h"
SOURCE = ROOT / "src/net/Connection.cpp"


def main() -> None:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")

    required_header_tokens = (
        "onWritable",
        "hasPendingOutput",
        "outputBuffer",
        "connectionId",
    )
    required_source_tokens = (
        "MSG_NOSIGNAL",
        "isWouldBlockError",
        "outputOffset",
    )

    for token in required_header_tokens:
        if token not in header:
            raise SystemExit(f"Connection.h missing token: {token}")
    for token in required_source_tokens:
        if token not in source:
            raise SystemExit(f"Connection.cpp missing token: {token}")
    if "Sleep(" in source:
        raise SystemExit("Connection.cpp still contains blocking Sleep")

    print("Connection ET audit passed")


if __name__ == "__main__":
    main()
