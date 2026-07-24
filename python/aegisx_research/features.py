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
    join_keys = ["strategy", "child_id"]
    join_keys.extend(
        column
        for column in ("parent_id", "stock_locate", "symbol")
        if column in passive.columns and column in maker.columns
    )
    filled = maker.groupby(join_keys, as_index=False)["quantity"].sum()
    filled = filled.rename(columns={"quantity": "passive_filled_quantity"})
    decisions = passive.merge(filled, on=join_keys, how="left")
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
        future_timestamp = groups["timestamp_ns"].shift(-horizon_rows)
        books["rolling_mid_volatility"] = groups["mid_price_ticks"].pct_change().groupby(
            [books[column] for column in snapshot_instrument_columns], sort=False
        ).transform(lambda values: values.rolling(10, min_periods=2).std())
        books["trade_intensity"] = groups["executions"].transform(
            lambda values: values.rolling(10, min_periods=1).sum()
        )
    else:
        books["mid_return_lag1"] = books["mid_price_ticks"].pct_change().replace([np.inf, -np.inf], np.nan)
        future_mid = books["mid_price_ticks"].shift(-horizon_rows)
        future_timestamp = books["timestamp_ns"].shift(-horizon_rows)
        books["rolling_mid_volatility"] = books["mid_return_lag1"].rolling(10, min_periods=2).std()
        books["trade_intensity"] = books["executions"].rolling(10, min_periods=1).sum()
    if "microprice_ticks" in books:
        books["microprice_deviation_ticks"] = books["microprice_ticks"] - books["mid_price_ticks"]
    else:
        books["microprice_deviation_ticks"] = np.nan
    books["future_mid_price_ticks"] = future_mid
    books["label_end_timestamp_ns"] = future_timestamp
    books["depth_ratio"] = books["bid_depth"] / books["ask_depth"].replace(0, np.nan)
    books["log_displayed_depth"] = np.log1p(books["bid_depth"] + books["ask_depth"])
    day_fraction = (books["timestamp_ns"].astype(float) % 86_400_000_000_000) / 86_400_000_000_000
    books["time_of_day_sin"] = np.sin(2.0 * np.pi * day_fraction)
    books["time_of_day_cos"] = np.cos(2.0 * np.pi * day_fraction)

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
    # Convention: a post-fill price increase is adverse for a passive buy and
    # a decrease is adverse for a passive sell. Positive values are worse.
    joined["adverse_selection_ticks"] = np.where(joined["side"].str.lower().eq("buy"), signed_move, -signed_move)
    if "initial_queue_ahead" in joined:
        joined["queue_position_fraction"] = joined["initial_queue_ahead"] / (
            joined["initial_queue_ahead"] + joined["submitted"]
        ).replace(0, np.nan)
    else:
        joined["queue_position_fraction"] = np.nan
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
    "rolling_mid_volatility",
    "trade_intensity",
    "depth_ratio",
    "log_displayed_depth",
    "time_of_day_sin",
    "time_of_day_cos",
    "queue_position_fraction",
)
