"""Strict loading for versioned AegisX experiment configuration."""

from __future__ import annotations

import json
from pathlib import Path


class ConfigError(ValueError):
    """Raised when a reproducible experiment configuration is invalid."""


_REQUIRED = {
    "replay": {"input", "snapshot_every_events", "top_levels"},
    "execution": {"intervals", "max_child_quantity", "completion_policy", "latency_ns", "fees"},
    "risk": {"max_order_quantity", "max_parent_quantity", "max_open_orders", "max_position"},
    "research": {
        "horizon_rows",
        "embargo_rows",
        "features",
        "random_seed",
        "minimum_rows",
        "minimum_partition_rows",
    },
    "dashboard": {"default_run", "read_only"},
    "real_data": {"venue", "protocol", "symbol", "training_source", "evaluation_source"},
}


def load_config(path: str | Path, expected_kind: str | None = None) -> dict[str, object]:
    source = Path(path)
    try:
        value = json.loads(source.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigError(f"could not load {source}: {error}") from error
    if not isinstance(value, dict):
        raise ConfigError("configuration root must be an object")
    if value.get("schema_version") != 1:
        raise ConfigError("schema_version must be 1")
    kind = value.get("kind")
    if kind not in _REQUIRED:
        raise ConfigError(f"unknown configuration kind: {kind}")
    if expected_kind is not None and kind != expected_kind:
        raise ConfigError(f"expected {expected_kind} configuration, received {kind}")
    missing = sorted(_REQUIRED[str(kind)].difference(value))
    if missing:
        raise ConfigError(f"missing {kind} fields: {', '.join(missing)}")
    return value
