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
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--config", type=Path, default=Path("configs/research.json"))
    args = parser.parse_args()
    config = load_config(args.config, "research")
    config_sha256 = hashlib.sha256(args.config.read_bytes()).hexdigest()
    output = args.output or args.run / "research"
    snapshots = pd.read_csv(args.run / "snapshots.csv")
    children = pd.read_csv(args.run / "child_orders.csv")
    fills = pd.read_csv(args.run / "execution_fills.csv")
    feature_columns = list(config["features"])
    unknown_features = sorted(set(feature_columns).difference(FEATURE_COLUMNS))
    if unknown_features:
        raise ValueError(f"unknown configured features: {', '.join(unknown_features)}")
    dataset = build_passive_fill_dataset(
        snapshots, children, fills, horizon_rows=int(config["horizon_rows"])
    )
    write_artifact(
        dataset,
        output,
        "passive_fill_dataset",
        {
            "feature_columns": feature_columns,
            "label": "passive_fill_label",
            "config": str(args.config),
            "config_sha256": config_sha256,
        },
    )
    split = chronological_split(dataset, embargo_rows=int(config["embargo_rows"]))
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
            },
            indent=2,
        )
        + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
