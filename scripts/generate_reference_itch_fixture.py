#!/usr/bin/env python3
"""Independent, specification-derived encoder for the parser validation fixture.

This script deliberately uses Python's binary packing primitives and a field table
separate from the C++ decoder. It is not used by the parser at runtime.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import struct


def frame(body: bytes) -> bytes:
    return struct.pack(">H", len(body)) + body


def common(kind: bytes, locate: int, tracking: int, timestamp: int) -> bytes:
    return kind + struct.pack(">HH", locate, tracking) + timestamp.to_bytes(6, "big")


def stock(symbol: str) -> bytes:
    return symbol.encode("ascii").ljust(8, b" ")


def directory(locate: int, symbol: str, timestamp: int) -> bytes:
    # ITCH 5.0 R: stock, market category, financial status, round lot size,
    # round-lots-only, issue class/subtype, authenticity, short-sale, IPO,
    # LULD tier, ETP flag/leverage and inverse indicator.
    fields = (
        stock(symbol)
        + b"Q" + b"N" + struct.pack(">I", 100) + b"Y"
        + b"A" + b"  " + b"P" + b"N" + b"N" + b"1" + b"N"
        + struct.pack(">I", 1) + b"N"
    )
    return common(b"R", locate, 1, timestamp) + fields


def main() -> None:
    messages = [
        frame(common(b"S", 0, 1, 1) + b"O"),
        frame(directory(11, "AAPL", 2)),
        frame(common(b"H", 11, 2, 3) + stock("AAPL") + b"T" + b" " + b"TEST"),
        frame(directory(12, "MSFT", 4)),
        frame(common(b"A", 11, 3, 5) + struct.pack(">Q c I 8s I", 1_001, b"B", 200, stock("AAPL"), 101_000)),
        frame(common(b"F", 12, 4, 6) + struct.pack(">Q c I 8s I 4s", 2_001, b"S", 300, stock("MSFT"), 250_100, b"ABCD")),
        frame(common(b"P", 11, 5, 7) + struct.pack(">Q c I 8s I Q", 9_001, b"B", 25, stock("AAPL"), 101_100, 77)),
        frame(common(b"Q", 11, 6, 8) + struct.pack(">Q 8s I Q c", 500, stock("AAPL"), 101_200, 78, b"O")),
        frame(common(b"B", 11, 7, 9) + struct.pack(">Q", 77)),
        # Known-but-unused Reg SHO Restriction message, correct 20-byte body.
        frame(common(b"Y", 11, 8, 10) + stock("AAPL") + b"0"),
        frame(common(b"S", 0, 9, 11) + b"C"),
    ]
    destination = Path("data/fixtures/itch_reference_segment.itch")
    destination.write_bytes(b"".join(messages))
    data = destination.read_bytes()
    print(f"{destination}: {len(data)} bytes sha256={hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main()
