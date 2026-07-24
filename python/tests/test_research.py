import numpy as np
import pandas as pd
import pytest
from pathlib import Path

from aegisx_research.config import load_config
from aegisx_research.experiments import assert_no_leakage, chronological_split, economic_evaluation, run_fill_experiments
from aegisx_research.features import FEATURE_COLUMNS, build_passive_fill_dataset
from aegisx_research.risk import (
    component_risk_contributions,
    drawdown_series,
    liquidity_adjusted_notional,
    parametric_var_es,
    portfolio_stress_report,
    stress_notional,
)


def artifacts():
    timestamps = np.arange(100, 220, 2, dtype=np.int64)
    snapshots = pd.DataFrame({
        "timestamp_ns": timestamps,
        "mid_price_ticks": 100 + np.sin(np.arange(len(timestamps))) * 2 + np.arange(len(timestamps)) * 0.1,
        "spread_ticks": np.where(np.arange(len(timestamps)) % 2, 2, 4),
        "bid_depth": 10 + (np.arange(len(timestamps)) % 7),
        "ask_depth": 10 + ((np.arange(len(timestamps)) + 3) % 7),
        "adds": 3 + (np.arange(len(timestamps)) % 4),
        "cancels": 1 + (np.arange(len(timestamps)) % 3),
        "executions": 1 + (np.arange(len(timestamps)) % 2),
        "microprice_ticks": 100 + np.arange(len(timestamps)) * 0.1,
    })
    child_rows = []
    fill_rows = []
    for index, timestamp in enumerate(timestamps[:-2]):
        child_rows.append({"strategy": "adaptive", "child_id": index, "type": "limit", "side": "buy" if index % 2 else "sell",
                           "submitted": 10, "submission_time": timestamp})
        if index % 2:
            fill_rows.append({"strategy": "adaptive", "child_id": index, "quantity": 10, "liquidity_role": "maker"})
    return snapshots, pd.DataFrame(child_rows), pd.DataFrame(fill_rows)


def test_simulator_labelled_dataset_and_chronological_models():
    dataset = build_passive_fill_dataset(*artifacts())
    assert {"passive_fill_label", "adverse_selection_ticks", *FEATURE_COLUMNS}.issubset(dataset.columns)
    split = chronological_split(dataset, embargo_rows=1)
    metrics, predictions = run_fill_experiments(split, FEATURE_COLUMNS)
    assert set(metrics["model"]) == {
        "unconditional fill-rate baseline",
        "historical time-bucket baseline",
        "imbalance rule",
        "logistic regression",
        "boosted trees",
    }
    assert {"f1", "pr_auc", "calibration_error", "true_positive", "false_negative"}.issubset(metrics.columns)
    assert not economic_evaluation(predictions).empty


def test_leakage_guard_rejects_future_feature():
    dataset = build_passive_fill_dataset(*artifacts()).sort_values("timestamp_ns")
    with pytest.raises(ValueError, match="future"):
        assert_no_leakage(dataset, ["future_mid_price_ticks"], ["passive_fill_label"])


def test_multi_instrument_join_never_uses_other_book():
    snapshots = pd.DataFrame({
        "timestamp_ns": [1, 1, 2, 2, 3, 3], "stock_locate": [1, 2, 1, 2, 1, 2],
        "symbol": ["AAPL", "MSFT", "AAPL", "MSFT", "AAPL", "MSFT"],
        "mid_price_ticks": [100, 500, 101, 501, 102, 502], "spread_ticks": [2] * 6,
        "bid_depth": [10] * 6, "ask_depth": [10] * 6, "adds": [1] * 6, "cancels": [0] * 6,
        "executions": [0] * 6,
    })
    children = pd.DataFrame({"strategy": ["adaptive"], "stock_locate": [2], "symbol": ["MSFT"],
                             "child_id": [1], "type": ["limit"], "side": ["buy"], "submitted": [10],
                             "submission_time": [2]})
    fills = pd.DataFrame({"strategy": ["adaptive"], "stock_locate": [2], "symbol": ["MSFT"], "child_id": [1],
                          "quantity": [10], "liquidity_role": ["maker"]})
    dataset = build_passive_fill_dataset(snapshots, children, fills)
    assert dataset.iloc[0]["mid_price_ticks"] == 501
    assert dataset.iloc[0]["future_mid_price_ticks"] == 502


def test_portfolio_risk_helpers():
    assert drawdown_series([0, 5, 2])["maximum_drawdown"] == 3
    assert stress_notional({"quantity": [2, -1], "price_ticks": [100, 50]}, {"down": -0.1})["down"] == -15
    assert parametric_var_es(np.linspace(-0.02, 0.02, 100))["expected_shortfall"] > 0
    component = component_risk_contributions([100, 50], [[0.04, 0.01], [0.01, 0.09]])
    assert component["contribution_sum"] == pytest.approx(component["portfolio_volatility"])
    liquid = liquidity_adjusted_notional(
        {"quantity": [100], "price_ticks": [100], "spread_ticks": [2], "daily_volume": [10_000]}
    )
    assert liquid["liquidity_adjusted_notional_ticks"] > liquid["gross_notional_ticks"]
    stress = portfolio_stress_report(
        {"symbol": ["AAPL", "JPM"], "sector": ["tech", "bank"], "quantity": [10, -5], "price_ticks": [100, 80]},
        {"market_down": {"market": -0.1}, "tech_crash": {"sectors": {"tech": -0.2}}},
    )
    assert set(stress["scenarios"]["scenario"]) == {"market_down", "tech_crash"}


def test_adverse_selection_sign_is_positive_when_post_fill_move_is_worse():
    snapshots, children, fills = artifacts()
    snapshots["mid_price_ticks"] = np.arange(len(snapshots), dtype=float) + 100.0
    children = children.iloc[:2].copy()
    children.loc[:, "side"] = ["buy", "sell"]
    fills = pd.DataFrame(
        {
            "strategy": ["adaptive", "adaptive"],
            "child_id": [0, 1],
            "quantity": [10, 10],
            "liquidity_role": ["maker", "maker"],
        }
    )
    dataset = build_passive_fill_dataset(snapshots, children, fills)
    buy = dataset.loc[dataset["side"].eq("buy"), "adverse_selection_ticks"].iloc[0]
    sell = dataset.loc[dataset["side"].eq("sell"), "adverse_selection_ticks"].iloc[0]
    assert buy > 0
    assert sell < 0


def test_committed_configs_are_versioned_and_valid():
    root = Path(__file__).resolve().parents[2]
    for kind in ("replay", "execution", "risk", "research", "dashboard"):
        assert load_config(root / "configs" / f"{kind}.json", kind)["kind"] == kind
