#!/usr/bin/env python3
"""Build an interval volume profile from an earlier real ITCH session."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parents[1] / "python"))

from aegisx_research.itch_data import read_frame, sha256_file


def execution_quantity(body: bytes) -> int:
    message_type = chr(body[0])
    if message_type in {"E", "C"} and len(body) >= 23:
        return int.from_bytes(body[19:23], "big")
    if message_type == "P" and len(body) >= 24:
        return int.from_bytes(body[20:24], "big")
    return 0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--training-session", required=True)
    parser.add_argument("--evaluation-session", required=True)
    parser.add_argument("--intervals", type=int, default=13)
    parser.add_argument("--start-timestamp", type=int, default=34_200_000_000_000)
    parser.add_argument("--end-timestamp", type=int, default=57_600_000_000_000)
    args = parser.parse_args()
    if args.intervals <= 0 or args.end_timestamp <= args.start_timestamp:
        raise SystemExit("invalid profile interval configuration")
    if args.training_session == args.evaluation_session:
        raise SystemExit("training and evaluation sessions must be distinct")

    volumes = [0] * args.intervals
    duration = args.end_timestamp - args.start_timestamp
    with args.input.open("rb") as stream:
        while frame := read_frame(stream):
            body = frame[2:]
            timestamp = int.from_bytes(body[5:11], "big")
            if timestamp < args.start_timestamp or timestamp > args.end_timestamp:
                continue
            quantity = execution_quantity(body)
            if quantity == 0:
                continue
            elapsed = min(timestamp - args.start_timestamp, duration - 1)
            index = min(args.intervals - 1, elapsed * args.intervals // duration)
            volumes[index] += quantity

    total = sum(volumes)
    if total <= 0:
        raise SystemExit("training segment has no regular-session execution volume")
    weights = [volume / total for volume in volumes]
    checksum = sha256_file(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        f"training_session={args.training_session}\n"
        f"evaluation_session={args.evaluation_session}\n"
        f"source_checksum={checksum}\n"
        f"weights={','.join(f'{weight:.12f}' for weight in weights)}\n"
    )
    audit = {
        "schema_version": 1,
        "data_classification": "real_exchange_historical_sample",
        "training_session": args.training_session,
        "evaluation_session": args.evaluation_session,
        "source_segment": str(args.input),
        "source_checksum": checksum,
        "start_timestamp_ns": args.start_timestamp,
        "end_timestamp_ns": args.end_timestamp,
        "intervals": args.intervals,
        "interval_execution_volume": volumes,
        "interval_weights": weights,
        "total_execution_volume": total,
        "included_message_types": ["E", "C", "P"],
    }
    args.output.with_suffix(f"{args.output.suffix}.json").write_text(
        json.dumps(audit, indent=2, sort_keys=True) + "\n"
    )
    print(json.dumps(audit, indent=2))


if __name__ == "__main__":
    main()
