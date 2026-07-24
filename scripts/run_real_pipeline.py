#!/usr/bin/env python3
"""Acquire, prepare, replay, simulate, and research real Nasdaq ITCH sessions."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).parents[1] / "python"))

from aegisx_research.config import load_config


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    environment = os.environ.copy()
    python_path = str(Path.cwd() / "python")
    environment["PYTHONPATH"] = (
        python_path
        if not environment.get("PYTHONPATH")
        else f"{python_path}{os.pathsep}{environment['PYTHONPATH']}"
    )
    subprocess.run(command, check=True, env=environment)


def source_paths(source: dict[str, object]) -> tuple[Path, Path, Path]:
    archive = Path(str(source["archive"]))
    segment = Path(str(source["segment"]))
    provenance = segment.with_suffix(f"{segment.suffix}.provenance.json")
    return archive, segment, provenance


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=Path("configs/real_data.json"))
    parser.add_argument("--binary", type=Path, default=Path("build/release/aegisx"))
    parser.add_argument("--run", type=Path, default=Path("runs/real-bx-20190830-aapl"))
    parser.add_argument("--skip-download", action="store_true")
    parser.add_argument("--connections", type=int, default=24)
    args = parser.parse_args()
    config = load_config(args.config, "real_data")
    training = dict(config["training_source"])
    evaluation = dict(config["evaluation_source"])

    for source in (training, evaluation):
        archive, segment, provenance = source_paths(source)
        expected_bytes = int(source["expected_bytes"])
        if not args.skip_download and (not archive.exists() or archive.stat().st_size != expected_bytes):
            run(
                [
                    sys.executable,
                    "scripts/download_real_itch.py",
                    "--url",
                    str(source["url"]),
                    "--output",
                    str(archive),
                    "--expected-bytes",
                    str(expected_bytes),
                    "--connections",
                    str(args.connections),
                ]
            )
        if not archive.exists() or archive.stat().st_size != expected_bytes:
            raise SystemExit(f"missing complete official archive: {archive}")
        run(
            [
                sys.executable,
                "scripts/prepare_real_itch.py",
                "--archive",
                str(archive),
                "--output",
                str(segment),
                "--symbol",
                str(config["symbol"]),
                "--session-date",
                str(source["session_date"]),
                "--venue",
                str(config["venue"]),
                "--source-url",
                str(source["url"]),
                "--official-archive-expected-bytes",
                str(expected_bytes),
                "--provenance",
                str(provenance),
            ]
        )

    training_archive, training_segment, _ = source_paths(training)
    evaluation_archive, evaluation_segment, evaluation_provenance = source_paths(evaluation)
    profile = Path("data/processed/real-aapl-vwap-profile.txt")
    run(
        [
            sys.executable,
            "scripts/build_real_vwap_profile.py",
            "--input",
            str(training_segment),
            "--output",
            str(profile),
            "--training-session",
            str(training["session_date"]),
            "--evaluation-session",
            str(evaluation["session_date"]),
        ]
    )
    run(
        [
            str(args.binary),
            "replay",
            "--input",
            str(evaluation_segment),
            "--output",
            str(args.run),
            "--symbol",
            str(config["symbol"]),
            "--snapshot-every-events",
            "100",
            "--top-levels",
            "5",
            "--provenance",
            str(evaluation_provenance),
        ]
    )
    run(
        [
            sys.executable,
            "scripts/benchmark_real_data.py",
            "--binary",
            str(args.binary),
            "--input",
            str(evaluation_segment),
            "--output",
            str(args.run / "performance"),
        ]
    )
    run(
        [
            str(args.binary),
            "simulate",
            "--input",
            str(evaluation_segment),
            "--output",
            str(args.run),
            "--symbol",
            str(config["symbol"]),
            "--quantity",
            "1000",
            "--max-child-quantity",
            "100",
            "--vwap-profile",
            str(profile),
            "--provenance",
            str(evaluation_provenance),
        ]
    )
    run(
        [
            sys.executable,
            "scripts/run_real_execution_sweep.py",
            "--binary",
            str(args.binary),
            "--input",
            str(evaluation_segment),
            "--output",
            str(args.run / "execution_size_sweep"),
            "--provenance",
            str(evaluation_provenance),
            "--vwap-profile",
            str(profile),
            "--symbol",
            str(config["symbol"]),
        ]
    )
    research_execution = args.run / "research_input"
    run(
        [
            str(args.binary),
            "simulate",
            "--input",
            str(evaluation_segment),
            "--output",
            str(research_execution),
            "--symbol",
            str(config["symbol"]),
            "--quantity",
            "100",
            "--parent-count",
            "52",
            "--parent-duration-ns",
            "900000000000",
            "--alternate-sides",
            "--intervals",
            "4",
            "--max-child-quantity",
            "25",
            "--provenance",
            str(evaluation_provenance),
        ]
    )
    run([str(args.binary), "risk-demo", "--output", str(args.run)])
    run(
        [
            sys.executable,
            "python/scripts/run_research.py",
            "--run",
            str(args.run),
            "--execution-run",
            str(research_execution),
            "--require-real-data",
        ]
    )

    summary = {
        "schema_version": 1,
        "data_classification": "real_exchange_historical_sample",
        "venue": config["venue"],
        "symbol": config["symbol"],
        "training_session": training["session_date"],
        "training_archive": str(training_archive),
        "evaluation_session": evaluation["session_date"],
        "evaluation_archive": str(evaluation_archive),
        "run": str(args.run),
        "status": "complete",
    }
    (args.run / "real_pipeline_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
