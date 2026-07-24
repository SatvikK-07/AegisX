"""Streaming preparation helpers for externally sourced ITCH BinaryFILE data."""

from __future__ import annotations

from collections import Counter
from dataclasses import asdict, dataclass
import gzip
import hashlib
import json
from pathlib import Path
from typing import BinaryIO


@dataclass(frozen=True)
class PreparedItchSegment:
    schema_version: int
    data_classification: str
    venue: str
    protocol: str
    session_date: str
    symbol: str
    stock_locate: int
    source_url: str
    official_archive_expected_bytes: int
    downloaded_object_bytes: int
    downloaded_object_sha256: str
    source_archive_complete: bool
    coverage: str
    frames_scanned: int
    frames_emitted: int
    source_first_timestamp_ns: int | None
    source_last_timestamp_ns: int | None
    segment_first_timestamp_ns: int | None
    segment_last_timestamp_ns: int | None
    scanned_by_type: dict[str, int]
    emitted_by_type: dict[str, int]
    segment_bytes: int
    segment_sha256: str
    preparation_method: str


def sha256_file(path: Path, chunk_bytes: int = 1024 * 1024) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(chunk_bytes):
            digest.update(chunk)
    return digest.hexdigest()


def _timestamp_ns(body: bytes) -> int | None:
    return int.from_bytes(body[5:11], "big") if len(body) >= 11 else None


def _stock_locate(body: bytes) -> int | None:
    return int.from_bytes(body[1:3], "big") if len(body) >= 3 else None


def _directory_symbol(body: bytes) -> str | None:
    if len(body) != 39 or body[0:1] != b"R":
        return None
    return body[11:19].decode("ascii", errors="strict").rstrip()


def prepare_symbol_segment(
    archive: Path,
    output: Path,
    *,
    symbol: str,
    session_date: str,
    venue: str,
    source_url: str,
    official_archive_expected_bytes: int,
    allow_truncated_archive: bool = False,
) -> PreparedItchSegment:
    """Extract one symbol and global system events without loading the archive."""

    normalized_symbol = symbol.strip().upper()
    if not normalized_symbol or len(normalized_symbol) > 8:
        raise ValueError("symbol must contain between one and eight characters")
    if not archive.is_file():
        raise FileNotFoundError(f"ITCH archive does not exist: {archive}")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f"{output.name}.partial")
    scanned_by_type: Counter[str] = Counter()
    emitted_by_type: Counter[str] = Counter()
    frames_scanned = 0
    frames_emitted = 0
    locate: int | None = None
    source_first: int | None = None
    source_last: int | None = None
    segment_first: int | None = None
    segment_last: int | None = None
    archive_complete = True

    try:
        with archive.open("rb") as compressed, gzip.GzipFile(fileobj=compressed) as source, temporary.open("wb") as sink:
            while True:
                try:
                    header = source.read(2)
                    if not header:
                        break
                    if len(header) != 2:
                        raise ValueError("truncated ITCH BinaryFILE length header")
                    length = int.from_bytes(header, "big")
                    if length == 0:
                        raise ValueError("zero-length ITCH BinaryFILE frame")
                    body = source.read(length)
                    if len(body) != length:
                        raise ValueError("truncated ITCH BinaryFILE message")
                except EOFError:
                    if not allow_truncated_archive:
                        raise
                    archive_complete = False
                    break

                message_type = chr(body[0])
                timestamp = _timestamp_ns(body)
                frames_scanned += 1
                scanned_by_type[message_type] += 1
                if timestamp is not None:
                    source_first = timestamp if source_first is None else min(source_first, timestamp)
                    source_last = timestamp if source_last is None else max(source_last, timestamp)

                if message_type == "R" and _directory_symbol(body) == normalized_symbol:
                    locate = _stock_locate(body)

                emit = message_type == "S" or (locate is not None and _stock_locate(body) == locate)
                if not emit:
                    continue
                sink.write(header)
                sink.write(body)
                frames_emitted += 1
                emitted_by_type[message_type] += 1
                if timestamp is not None:
                    segment_first = timestamp if segment_first is None else min(segment_first, timestamp)
                    segment_last = timestamp if segment_last is None else max(segment_last, timestamp)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise

    if locate is None:
        temporary.unlink(missing_ok=True)
        raise ValueError(f"symbol {normalized_symbol} was not found in a Stock Directory message")
    if frames_emitted == 0:
        temporary.unlink(missing_ok=True)
        raise ValueError(f"no messages were emitted for {normalized_symbol}")

    temporary.replace(output)
    downloaded_bytes = archive.stat().st_size
    result = PreparedItchSegment(
        schema_version=1,
        data_classification="real_exchange_historical_sample",
        venue=venue,
        protocol="TotalView-ITCH 5.0 / BinaryFILE",
        session_date=session_date,
        symbol=normalized_symbol,
        stock_locate=locate,
        source_url=source_url,
        official_archive_expected_bytes=official_archive_expected_bytes,
        downloaded_object_bytes=downloaded_bytes,
        downloaded_object_sha256=sha256_file(archive),
        source_archive_complete=archive_complete and downloaded_bytes == official_archive_expected_bytes,
        coverage="complete_session"
        if archive_complete and downloaded_bytes == official_archive_expected_bytes
        else "downloaded_source_prefix",
        frames_scanned=frames_scanned,
        frames_emitted=frames_emitted,
        source_first_timestamp_ns=source_first,
        source_last_timestamp_ns=source_last,
        segment_first_timestamp_ns=segment_first,
        segment_last_timestamp_ns=segment_last,
        scanned_by_type=dict(sorted(scanned_by_type.items())),
        emitted_by_type=dict(sorted(emitted_by_type.items())),
        segment_bytes=output.stat().st_size,
        segment_sha256=sha256_file(output),
        preparation_method="streaming_symbol_extract_v1",
    )
    return result


def write_provenance(result: PreparedItchSegment, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(asdict(result), indent=2, sort_keys=True) + "\n")


def read_frame(stream: BinaryIO) -> bytes | None:
    """Read one framed ITCH message; exposed for deterministic tests."""

    header = stream.read(2)
    if not header:
        return None
    if len(header) != 2:
        raise ValueError("truncated ITCH BinaryFILE length header")
    length = int.from_bytes(header, "big")
    if length == 0:
        raise ValueError("zero-length ITCH BinaryFILE frame")
    body = stream.read(length)
    if len(body) != length:
        raise ValueError("truncated ITCH BinaryFILE message")
    return header + body
