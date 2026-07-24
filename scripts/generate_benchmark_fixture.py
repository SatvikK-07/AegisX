#!/usr/bin/env python3
"""Generate a large deterministic synthetic ITCH stream for local throughput measurement."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct


def frame(body: bytes) -> bytes:
    return struct.pack(">H", len(body)) + body


def common(kind: bytes, timestamp: int) -> bytes:
    return kind + struct.pack(">HH", 1, 1) + timestamp.to_bytes(6, "big")


def stock(symbol: str) -> bytes:
    return symbol.encode("ascii").ljust(8, b" ")


def directory() -> bytes:
    fields = stock("AAPL") + b"Q" + b"N" + struct.pack(">I", 100) + b"Y" + b"A" + b"  " + b"P" + b"N" + b"N" + b"1" + b"N" + struct.pack(">I", 1) + b"N"
    return frame(common(b"R", 1) + fields)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pairs", type=int, default=500_000, help="add/delete pairs; total events are 1 + 2*pairs")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.pairs < 1:
        raise ValueError("pairs must be positive")
    with args.output.open("wb") as destination:
        destination.write(directory())
        for index in range(args.pairs):
            order_id = index + 1
            timestamp = index * 2 + 2
            add = common(b"A", timestamp) + struct.pack(">Q c I 8s I", order_id, b"B", 1, stock("AAPL"), 100_000)
            delete = common(b"D", timestamp + 1) + struct.pack(">Q", order_id)
            destination.write(frame(add))
            destination.write(frame(delete))
    print(f"{args.output}: {1 + 2 * args.pairs} synthetic framed events")


if __name__ == "__main__":
    main()
