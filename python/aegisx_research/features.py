"""Chronological, simulator-labelled microstructure feature construction."""

from __future__ import annotations

import numpy as np
import pandas as pd


_SNAPSHOT_COLUMNS = {
    "timestamp_ns",
    "mid_price_ticks",
    "bid_depth",
    "ask_depth",
    "adds",
    "cancels",
    "executions",
}


def _require(frame: pd.DataFrame, columns: set[str], name: str) -> None:
    missing = sorted(columns.difference(frame.columns))
    if missing:
        raise ValueError(f"{name} is missing required columns: {', '.join(missing)}")


def passive_fill_labels(child_orders: pd.DataFrame, fills: pd.DataFrame) -> pd.DataFrame:
    """Return labels directly from the execution simulator's child/fill artifacts.

    A label is attached only to passive (limit) children. The source quantities are
    simulator output, never inferred from a future market-price feature.
    """
    _require(child_orders, {"strategy", "child_id", "type", "side", "submitted", "submission_time"}, "child_orders")
    _require(fills, {"strategy", "child_id", "quantity", "liquidity_role"}, "execution_fills")
    passive = child_orders.loc[child_orders["type"].str.lower().eq("limit")].copy()
    maker = fills.loc[fills["liquidity_role"].str.lower().eq("maker")].copy()
    filled = maker.groupby(["strategy", "child_id"], as_index=False)["quantity"].sum()
    filled = filled.rename(columns={"quantity": "passive_filled_quantity"})
    decisions = passive.merge(filled, on=["strategy", "child_id"], how="left")
    decisions["passive_filled_quantity"] = decisions["passive_filled_quantity"].fillna(0).astype(float)
    decisions["submitted"] = decisions["submitted"].astype(float)
    if (decisions["submitted"] <= 0).any():
        raise ValueError("simulator emitted a non-positive submitted quantity")
    decisions["passive_fill_fraction"] = decisions["passive_filled_quantity"] / decisions["submitted"]
    decisions["passive_fill_label"] = (decisions["passive_fill_fraction"] > 0.0).astype(int)
    return decisions


def build_passive_fill_dataset(
    snapshots: pd.DataFrame,
    child_orders: pd.DataFrame,
    fills: pd.DataFrame,
    horizon_rows: int = 1,
) -> pd.DataFrame:
    """Join simulator labels to only contemporaneous/past book state.

    ``adverse_selection_ticks`` is a label based on the future midpoint. It is
    deliberately not part of ``FEATURE_COLUMNS`` and the leakage checks reject
    it if passed as a model feature.
    """
    if horizon_rows < 1:
        raise ValueError("horizon_rows must be at least one")
    _require(snapshots, _SNAPSHOT_COLUMNS, "snapshots")
    decisions = passive_fill_labels(child_orders, fills)
    if decisions.empty:
        raise ValueError("no passive simulator children; a research model cannot be fit")
    books = snapshots.copy().sort_values("timestamp_ns", kind="stable").reset_index(drop=True)
    snapshot_instrument_columns = [column for column in ("stock_locate", "symbol") if column in books.columns]
    if books["timestamp_ns"].duplicated().any():
        # Stable ordering is still deterministic, but an exact merge must not
        # pretend that two distinct instruments share one anonymous book stream.
        if not {"stock_locate", "symbol"}.intersection(books.columns):
            raise ValueError("duplicate snapshot timestamps need an instrument identifier")
    books["top_imbalance"] = (books["bid_depth"] - books["ask_depth"]) / (
        books["bid_depth"] + books["ask_depth"]
    ).replace(0, np.nan)
    books["order_flow_imbalance"] = (books["adds"] - books["cancels"] - books["executions"]) / (
        books["adds"] + books["cancels"] + books["executions"]
    ).replace(0, np.nan)
    if snapshot_instrument_columns:
        groups = books.groupby(snapshot_instrument_columns, sort=False)
        books["mid_return_lag1"] = groups["mid_price_ticks"].pct_change().replace([np.inf, -np.inf], np.nan)
        future_mid = groups["mid_price_ticks"].shift(-horizon_rows)
    else:
        books["mid_return_lag1"] = books["mid_price_ticks"].pct_change().replace([np.inf, -np.inf], np.nan)
        future_mid = books["mid_price_ticks"].shift(-horizon_rows)
    if "microprice_ticks" in books:
        books["microprice_deviation_ticks"] = books["microprice_ticks"] - books["mid_price_ticks"]
    else:
        books["microprice_deviation_ticks"] = np.nan
    books["future_mid_price_ticks"] = future_mid

    left = decisions.sort_values("submission_time", kind="stable")
    instrument_columns = [
        column for column in ("stock_locate", "symbol") if column in left.columns and column in books.columns
    ]
    joined = pd.merge_asof(
        left,
        books.sort_values("timestamp_ns", kind="stable"),
        left_on="submission_time",
        right_on="timestamp_ns",
        by=instrument_columns if instrument_columns else None,
        direction="backward",
    )
    signed_move = joined["future_mid_price_ticks"] - joined["mid_price_ticks"]
    joined["adverse_selection_ticks"] = np.where(joined["side"].str.lower().eq("buy"), -signed_move, signed_move)
    joined = joined.dropna(subset=["timestamp_ns", "future_mid_price_ticks"]).reset_index(drop=True)
    return joined


FEATURE_COLUMNS = (
    "spread_ticks",
    "bid_depth",
    "ask_depth",
    "top_imbalance",
    "order_flow_imbalance",
    "mid_return_lag1",
    "microprice_deviation_ticks",
)
