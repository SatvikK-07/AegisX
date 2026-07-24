"""Leakage-safe chronological experiments and simple economic evaluation."""

from __future__ import annotations

from dataclasses import dataclass
import os

import numpy as np
import pandas as pd

# Some constrained environments do not expose physical-core metadata. This
# preserves deterministic single-process model fitting without a noisy warning.
os.environ.setdefault("LOKY_MAX_CPU_COUNT", "1")

from sklearn.ensemble import HistGradientBoostingClassifier
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import accuracy_score, brier_score_loss, precision_score, recall_score, roc_auc_score
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler


@dataclass(frozen=True)
class ChronologicalSplit:
    train: pd.DataFrame
    validation: pd.DataFrame
    test: pd.DataFrame


def chronological_split(
    frame: pd.DataFrame, train_fraction: float = 0.60, validation_fraction: float = 0.20, embargo_rows: int = 1
) -> ChronologicalSplit:
    if not 0.0 < train_fraction < 1.0 or not 0.0 < validation_fraction < 1.0 or train_fraction + validation_fraction >= 1.0:
        raise ValueError("invalid chronological split fractions")
    if embargo_rows < 0:
        raise ValueError("embargo_rows must be non-negative")
    ordered = frame.sort_values("timestamp_ns", kind="stable").reset_index(drop=True)
    train_end = int(len(ordered) * train_fraction)
    validation_end = int(len(ordered) * (train_fraction + validation_fraction))
    validation_start = train_end + embargo_rows
    test_start = validation_end + embargo_rows
    if train_end == 0 or validation_start >= validation_end or test_start >= len(ordered):
        raise ValueError("not enough chronological observations after embargo")
    return ChronologicalSplit(
        train=ordered.iloc[:train_end].copy(),
        validation=ordered.iloc[validation_start:validation_end].copy(),
        test=ordered.iloc[test_start:].copy(),
    )


def assert_no_leakage(frame: pd.DataFrame, feature_columns: list[str] | tuple[str, ...], label_columns: list[str]) -> None:
    if frame.empty:
        raise ValueError("empty research frame")
    if not frame["timestamp_ns"].is_monotonic_increasing:
        raise ValueError("research frame is not chronological")
    overlap = set(feature_columns).intersection(label_columns)
    if overlap:
        raise ValueError(f"labels used as features: {', '.join(sorted(overlap))}")
    forbidden = {"future_mid_price_ticks", "adverse_selection_ticks", "passive_fill_label", "passive_fill_fraction"}
    leaked = set(feature_columns).intersection(forbidden)
    if leaked:
        raise ValueError(f"future or label-derived fields used as features: {', '.join(sorted(leaked))}")


def _metric_row(name: str, actual: pd.Series, probability: np.ndarray) -> dict[str, float | str]:
    prediction = (probability >= 0.5).astype(int)
    auc = float("nan") if actual.nunique() < 2 else float(roc_auc_score(actual, probability))
    return {
        "model": name,
        "roc_auc": auc,
        "brier": float(brier_score_loss(actual, probability)),
        "accuracy": float(accuracy_score(actual, prediction)),
        "precision": float(precision_score(actual, prediction, zero_division=0)),
        "recall": float(recall_score(actual, prediction, zero_division=0)),
    }


def run_fill_experiments(split: ChronologicalSplit, feature_columns: list[str] | tuple[str, ...]) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Compare a baseline, a transparent rule, logistic model and boosted model."""
    label = "passive_fill_label"
    assert_no_leakage(split.train, feature_columns, [label, "adverse_selection_ticks"])
    if split.train[label].nunique() < 2:
        raise ValueError("training period has only one passive-fill class")
    train_x = split.train.loc[:, feature_columns].fillna(0.0)
    test_x = split.test.loc[:, feature_columns].fillna(0.0)
    actual = split.test[label].astype(int)
    baseline = np.full(len(split.test), split.train[label].mean(), dtype=float)
    rule = (split.test["top_imbalance"].fillna(0.0).to_numpy() > 0.0).astype(float)
    logistic = make_pipeline(StandardScaler(), LogisticRegression(max_iter=1_000, random_state=7))
    boosted = HistGradientBoostingClassifier(max_depth=3, learning_rate=0.08, random_state=7)
    models = {"fill-rate baseline": baseline, "imbalance rule": rule}
    for name, model in {"logistic regression": logistic, "boosted trees": boosted}.items():
        model.fit(train_x, split.train[label].astype(int))
        models[name] = model.predict_proba(test_x)[:, 1]
    metrics = pd.DataFrame([_metric_row(name, actual, probability) for name, probability in models.items()])
    predictions = split.test.loc[:, ["timestamp_ns", label, "adverse_selection_ticks"]].copy()
    for name, probability in models.items():
        predictions[name] = probability
    return metrics, predictions


def economic_evaluation(predictions: pd.DataFrame, maker_rebate_ticks: float = 0.0, taker_fee_ticks: float = 0.0) -> pd.DataFrame:
    """Estimate realised passive economics from labels, not classification metrics alone."""
    rows: list[dict[str, float | str]] = []
    for column in predictions.columns:
        if column in {"timestamp_ns", "passive_fill_label", "adverse_selection_ticks"}:
            continue
        traded = predictions[column] >= 0.5
        fill = predictions.loc[traded, "passive_fill_label"].astype(float)
        adverse = predictions.loc[traded, "adverse_selection_ticks"].astype(float)
        net_ticks = fill * (maker_rebate_ticks - taker_fee_ticks) - fill * adverse
        rows.append(
            {
                "model": column,
                "decisions": int(traded.sum()),
                "realized_fill_rate": float(fill.mean()) if len(fill) else float("nan"),
                "mean_adverse_selection_ticks": float(adverse.mean()) if len(adverse) else float("nan"),
                "net_ticks_per_decision": float(net_ticks.mean()) if len(net_ticks) else float("nan"),
                "total_net_ticks": float(net_ticks.sum()),
            }
        )
    return pd.DataFrame(rows)
