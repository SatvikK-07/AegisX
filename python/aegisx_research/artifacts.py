"""Versioned Parquet artifact helpers for reproducible research runs."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pandas as pd


def write_artifact(frame: pd.DataFrame, output: Path, name: str, metadata: dict[str, object]) -> Path:
    output.mkdir(parents=True, exist_ok=True)
    artifact = output / f"{name}.parquet"
    frame.to_parquet(artifact, index=False)
    digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
    manifest = output / f"{name}.manifest.json"
    manifest.write_text(json.dumps({"artifact": artifact.name, "sha256": digest, **metadata}, indent=2) + "\n")
    return artifact

