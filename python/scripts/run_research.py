"""Build a leakage-checked research artifact from one completed AegisX run."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import pandas as pd

from aegisx_research.artifacts import write_artifact
from aegisx_research.experiments import chronological_split, economic_evaluation, run_fill_experiments
from aegisx_research.features import FEATURE_COLUMNS, build_passive_fill_dataset


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", type=Path, required=True, help="directory produced by replay and simulate")
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()
    output = args.output or args.run / "research"
    snapshots = pd.read_csv(args.run / "snapshots.csv")
    children = pd.read_csv(args.run / "child_orders.csv")
    fills = pd.read_csv(args.run / "execution_fills.csv")
    dataset = build_passive_fill_dataset(snapshots, children, fills)
    write_artifact(dataset, output, "passive_fill_dataset", {"feature_columns": list(FEATURE_COLUMNS), "label": "passive_fill_label"})
    split = chronological_split(dataset)
    metrics, predictions = run_fill_experiments(split, list(FEATURE_COLUMNS))
    economics = economic_evaluation(predictions)
    metrics.to_csv(output / "model_metrics.csv", index=False)
    economics.to_csv(output / "economic_metrics.csv", index=False)
    (output / "research_metadata.json").write_text(
        json.dumps({"rows": len(dataset), "train_rows": len(split.train), "validation_rows": len(split.validation),
                    "test_rows": len(split.test), "feature_columns": list(FEATURE_COLUMNS)}, indent=2) + "\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
