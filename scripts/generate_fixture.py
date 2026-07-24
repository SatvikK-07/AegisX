#!/usr/bin/env python3
"""Build a synthetic fixture using documented Nasdaq TotalView-ITCH 5.0 field layouts."""
from pathlib import Path
import hashlib
import struct

def frame(body: bytes) -> bytes:
    return struct.pack(">H", len(body)) + body

def common(kind: bytes, timestamp_ns: int) -> bytes:
    return kind + struct.pack(">HH", 1, 1) + timestamp_ns.to_bytes(6, "big")

def stock(symbol: str) -> bytes:
    return symbol.encode("ascii").ljust(8, b" ")

fixture = b"".join([
    frame(common(b"S", 1) + b"O"),
    frame(common(b"R", 1) + stock("AAPL") + b"Q" + b"N" + struct.pack(">I", 100) + b"Y" + b"A" + b"  " + b"P" + b"N" + b"N" + b"L" + b"Y" + struct.pack(">I", 1) + b"N"),
    frame(common(b"A", 2) + struct.pack(">Q c I 8s I", 100, b"B", 100, stock("AAPL"), 100_000)),
    frame(common(b"F", 3) + struct.pack(">Q c I 8s I 4s", 200, b"S", 120, stock("AAPL"), 100_200, b"MM01")),
    frame(common(b"E", 4) + struct.pack(">Q I Q", 100, 20, 1)),
    frame(common(b"X", 5) + struct.pack(">Q I", 200, 20)),
    frame(common(b"P", 6) + struct.pack(">Q c I 8s I Q", 300, b"B", 30, stock("AAPL"), 100_200, 2)),
    frame(common(b"U", 7) + struct.pack(">Q Q I I", 100, 101, 60, 99_800)),
    frame(common(b"C", 8) + struct.pack(">Q I Q c I", 200, 30, 3, b"Y", 100_200)),
    frame(common(b"D", 9) + struct.pack(">Q", 101)),
    frame(common(b"S", 10) + b"C"),
])
output = Path("data/fixtures/aegisx_itch_sample.itch")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_bytes(fixture)
print(f"{output}: {len(fixture)} bytes, sha256={hashlib.sha256(fixture).hexdigest()}")
