#!/usr/bin/env python3
"""Measure replay throughput on real ITCH and risk latency on the same build."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=Path("build/release/aegisx"))
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--risk-iterations", type=int, default=1_000_000)
    args = parser.parse_args()
    if args.runs < 3:
        raise SystemExit("at least three replay benchmark runs are required")
    args.output.mkdir(parents=True, exist_ok=True)

    trials: list[dict[str, float | int]] = []
    for index in range(args.runs):
        trial = args.output / f"trial-{index + 1}"
        subprocess.run(
            [str(args.binary), "benchmark", "--input", str(args.input), "--output", str(trial)],
            check=True,
        )
        value = json.loads((trial / "benchmark.json").read_text())
        trials.append(
            {
                **value,
                "parser_events_per_second": value["framed_messages"]
                * 1_000_000_000.0
                / value["parser_nanoseconds"],
                "replay_events_per_second": value["replay_events"]
                * 1_000_000_000.0
                / value["replay_nanoseconds"],
            }
        )

    with (args.output / "replay_trials.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(trials[0]))
        writer.writeheader()
        writer.writerows(trials)
    risk_output = args.output / "risk"
    subprocess.run(
        [
            str(args.binary),
            "risk-benchmark",
            "--iterations",
            str(args.risk_iterations),
            "--output",
            str(risk_output),
        ],
        check=True,
    )
    risk = json.loads((risk_output / "risk_benchmark.json").read_text())
    summary = {
        "schema_version": 1,
        "data_classification": "real_exchange_historical_sample",
        "input": str(args.input),
        "input_sha256": sha256_file(args.input),
        "host": platform.platform(),
        "machine": platform.machine(),
        "runs": args.runs,
        "framed_messages_per_run": trials[0]["framed_messages"],
        "decoded_replay_events_per_run": trials[0]["replay_events"],
        "median_parser_events_per_second": statistics.median(
            float(trial["parser_events_per_second"]) for trial in trials
        ),
        "median_replay_events_per_second": statistics.median(
            float(trial["replay_events_per_second"]) for trial in trials
        ),
        "risk": risk,
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
