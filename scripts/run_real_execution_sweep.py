#!/usr/bin/env python3
"""Run a reproducible real-session execution size sensitivity experiment."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import statistics
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, default=Path("build/release/aegisx"))
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    parser.add_argument("--vwap-profile", type=Path, required=True)
    parser.add_argument("--symbol", default="AAPL")
    parser.add_argument("--side", choices=("buy", "sell"), default="buy")
    parser.add_argument("--sizes", type=int, nargs="+", default=[100, 250, 500, 750, 1000])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, object]] = []
    for quantity in args.sizes:
        if quantity <= 0:
            raise SystemExit("all size-sweep quantities must be positive")
        run = args.output / f"quantity-{quantity}"
        subprocess.run(
            [
                str(args.binary),
                "simulate",
                "--input",
                str(args.input),
                "--output",
                str(run),
                "--symbol",
                args.symbol,
                "--side",
                args.side,
                "--quantity",
                str(quantity),
                "--max-child-quantity",
                "100",
                "--vwap-profile",
                str(args.vwap_profile),
                "--provenance",
                str(args.provenance),
            ],
            check=True,
        )
        with (run / "execution_comparison.csv").open(newline="") as stream:
            strategies = {row["strategy"]: row for row in csv.DictReader(stream)}
        twap = strategies["twap"]
        adaptive = strategies["adaptive"]
        twap_price = float(twap["average_execution_price_ticks"])
        adaptive_price = float(adaptive["average_execution_price_ticks"])
        signed_improvement = twap_price - adaptive_price if args.side == "buy" else adaptive_price - twap_price
        rows.append(
            {
                "quantity": quantity,
                "side": args.side,
                "twap_fill_rate": float(twap["fill_rate"]),
                "adaptive_fill_rate": float(adaptive["fill_rate"]),
                "twap_average_price_ticks": twap_price,
                "adaptive_average_price_ticks": adaptive_price,
                "adaptive_savings_vs_twap_bps": signed_improvement / twap_price * 10_000.0,
                "adaptive_passive_fill_ratio": float(adaptive["passive_fill_ratio"]),
                "adaptive_cancel_count": int(adaptive["cancel_count"]),
            }
        )

    with (args.output / "size_sweep.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    completed = [
        row
        for row in rows
        if row["twap_fill_rate"] == 1.0 and row["adaptive_fill_rate"] == 1.0
    ]
    savings = [float(row["adaptive_savings_vs_twap_bps"]) for row in completed]
    summary = {
        "schema_version": 1,
        "data_classification": "real_exchange_historical_sample",
        "input": str(args.input),
        "provenance": str(args.provenance),
        "symbol": args.symbol,
        "side": args.side,
        "tested_sizes": args.sizes,
        "fully_completed_comparisons": len(completed),
        "mean_adaptive_savings_vs_twap_bps": statistics.mean(savings) if savings else None,
        "median_adaptive_savings_vs_twap_bps": statistics.median(savings) if savings else None,
        "population_stddev_bps": statistics.pstdev(savings) if savings else None,
        "minimum_savings_bps": min(savings) if savings else None,
        "maximum_savings_bps": max(savings) if savings else None,
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
