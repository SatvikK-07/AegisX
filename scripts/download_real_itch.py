#!/usr/bin/env python3
"""Resumable parallel range downloader for official Nasdaq ITCH archives."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import shutil
import time
from urllib.request import Request, urlopen


def download_range(url: str, destination: Path, start: int, end: int, retries: int) -> None:
    expected = end - start + 1
    if destination.exists() and destination.stat().st_size == expected:
        return
    temporary = destination.with_suffix(".partial")
    for attempt in range(1, retries + 1):
        try:
            request = Request(url, headers={"Range": f"bytes={start}-{end}", "User-Agent": "AegisX/1.0"})
            with urlopen(request, timeout=180) as response, temporary.open("wb") as sink:
                if response.status != 206:
                    raise RuntimeError(f"server did not honor byte range {start}-{end}: HTTP {response.status}")
                shutil.copyfileobj(response, sink, length=1024 * 1024)
            if temporary.stat().st_size != expected:
                raise RuntimeError(
                    f"range {start}-{end} produced {temporary.stat().st_size} bytes; expected {expected}"
                )
            temporary.replace(destination)
            return
        except Exception:
            temporary.unlink(missing_ok=True)
            if attempt == retries:
                raise
            time.sleep(float(attempt))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-bytes", type=int, required=True)
    parser.add_argument(
        "--download-bytes",
        type=int,
        default=0,
        help="Download this leading byte count; zero downloads the complete official object.",
    )
    parser.add_argument("--connections", type=int, default=24)
    parser.add_argument("--chunk-mib", type=int, default=2)
    parser.add_argument("--retries", type=int, default=5)
    args = parser.parse_args()

    total = args.expected_bytes if args.download_bytes == 0 else min(args.download_bytes, args.expected_bytes)
    if total <= 0 or args.connections <= 0 or args.chunk_mib <= 0 or args.retries <= 0:
        raise SystemExit("byte counts, connections, chunk size, and retries must be positive")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    parts = args.output.with_name(f".{args.output.name}.parts")
    parts.mkdir(parents=True, exist_ok=True)
    chunk_bytes = args.chunk_mib * 1024 * 1024
    ranges: list[tuple[int, int, Path]] = []
    for index, start in enumerate(range(0, total, chunk_bytes)):
        end = min(total - 1, start + chunk_bytes - 1)
        ranges.append((start, end, parts / f"part-{index:06d}"))

    with ThreadPoolExecutor(max_workers=args.connections) as executor:
        pending = {
            executor.submit(download_range, args.url, path, start, end, args.retries): (index, start, end)
            for index, (start, end, path) in enumerate(ranges)
        }
        completed = 0
        for future in as_completed(pending):
            future.result()
            completed += 1
            if completed == len(ranges) or completed % max(1, len(ranges) // 20) == 0:
                print(f"downloaded {completed}/{len(ranges)} ranges", flush=True)

    temporary = args.output.with_name(f"{args.output.name}.partial")
    with temporary.open("wb") as sink:
        for _, _, part in ranges:
            with part.open("rb") as source:
                shutil.copyfileobj(source, sink, length=1024 * 1024)
    if temporary.stat().st_size != total:
        raise SystemExit(f"assembled file has {temporary.stat().st_size} bytes; expected {total}")
    temporary.replace(args.output)
    shutil.rmtree(parts)
    print(f"wrote {args.output} ({total} bytes)", flush=True)


if __name__ == "__main__":
    main()
