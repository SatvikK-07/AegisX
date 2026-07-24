"""Build a leakage-checked research artifact from one completed AegisX run."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import pandas as pd

from aegisx_research.artifacts import write_artifact
from aegisx_research.config import load_config
from aegisx_research.experiments import chronological_split, economic_evaluation, run_fill_experiments
from aegisx_research.features import FEATURE_COLUMNS, build_passive_fill_dataset


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", type=Path, required=True, help="directory produced by replay and simulate")
    parser.add_argument(
        "--execution-run",
        type=Path,
        default=None,
        help="optional separate simulator output used to construct passive-fill labels",
    )
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--config", type=Path, default=Path("configs/research.json"))
    parser.add_argument("--require-real-data", action="store_true")
    args = parser.parse_args()
    config = load_config(args.config, "research")
    config_sha256 = hashlib.sha256(args.config.read_bytes()).hexdigest()
    output = args.output or args.run / "research"
    execution_run = args.execution_run or args.run
    provenance_path = args.run / "data_provenance.json"
    provenance = json.loads(provenance_path.read_text()) if provenance_path.exists() else {}
    replay_metadata_path = args.run / "metadata.json"
    execution_context_path = execution_run / "execution_context.json"
    replay_metadata = json.loads(replay_metadata_path.read_text()) if replay_metadata_path.exists() else {}
    execution_context = json.loads(execution_context_path.read_text()) if execution_context_path.exists() else {}
    if args.require_real_data and (
        provenance.get("data_classification") != "real_exchange_historical_sample"
        or provenance.get("coverage") != "complete_session"
    ):
        raise ValueError("research run does not contain complete-session real-exchange provenance")
    expected_segment_checksum = provenance.get("segment_sha256")
    if args.require_real_data and (
        not expected_segment_checksum
        or replay_metadata.get("input_sha256") != expected_segment_checksum
        or execution_context.get("input_sha256") != expected_segment_checksum
    ):
        raise ValueError("replay, execution, and real-data provenance checksums do not match")
    snapshots = pd.read_csv(args.run / "snapshots.csv")
    children = pd.read_csv(execution_run / "child_orders.csv")
    fills = pd.read_csv(execution_run / "execution_fills.csv")
    feature_columns = list(config["features"])
    unknown_features = sorted(set(feature_columns).difference(FEATURE_COLUMNS))
    if unknown_features:
        raise ValueError(f"unknown configured features: {', '.join(unknown_features)}")
    dataset = build_passive_fill_dataset(
        snapshots, children, fills, horizon_rows=int(config["horizon_rows"])
    )
    minimum_rows = int(config["minimum_rows"])
    if len(dataset) < minimum_rows:
        raise ValueError(f"research dataset has {len(dataset)} rows; at least {minimum_rows} are required")
    write_artifact(
        dataset,
        output,
        "passive_fill_dataset",
        {
            "feature_columns": feature_columns,
            "label": "passive_fill_label",
            "config": str(args.config),
            "config_sha256": config_sha256,
            "data_classification": provenance.get("data_classification", "unverified"),
            "venue": provenance.get("venue"),
            "session_date": provenance.get("session_date"),
            "source_segment_sha256": provenance.get("segment_sha256"),
            "execution_run": str(execution_run),
        },
    )
    split = chronological_split(dataset, embargo_rows=int(config["embargo_rows"]))
    minimum_partition_rows = int(config["minimum_partition_rows"])
    for name, partition in (
        ("train", split.train),
        ("validation", split.validation),
        ("test", split.test),
    ):
        if len(partition) < minimum_partition_rows:
            raise ValueError(
                f"{name} partition has {len(partition)} rows; at least {minimum_partition_rows} are required"
            )
        if partition["passive_fill_label"].nunique() < 2:
            raise ValueError(f"{name} partition has only one passive-fill class")
    metrics, predictions = run_fill_experiments(
        split, feature_columns, random_seed=int(config["random_seed"])
    )
    economics = economic_evaluation(predictions)
    metrics.to_csv(output / "model_metrics.csv", index=False)
    economics.to_csv(output / "economic_metrics.csv", index=False)
    (output / "research_metadata.json").write_text(
        json.dumps(
            {
                "rows": len(dataset),
                "train_rows": len(split.train),
                "validation_rows": len(split.validation),
                "test_rows": len(split.test),
                "feature_columns": feature_columns,
                "config": str(args.config),
                "config_sha256": config_sha256,
                "random_seed": int(config["random_seed"]),
                "data_classification": provenance.get("data_classification", "unverified"),
                "venue": provenance.get("venue"),
                "session_date": provenance.get("session_date"),
                "source_url": provenance.get("source_url"),
                "source_segment_sha256": provenance.get("segment_sha256"),
                "execution_run": str(execution_run),
            },
            indent=2,
        )
        + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
