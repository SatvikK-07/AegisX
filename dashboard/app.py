"""Read-only inspection dashboard for artifacts produced by AegisX commands."""

from __future__ import annotations

import json
from pathlib import Path

import pandas as pd
import streamlit as st


def load_csv(run: Path, name: str) -> pd.DataFrame:
    path = run / name
    return pd.read_csv(path) if path.exists() else pd.DataFrame()


def load_json(run: Path, name: str) -> dict[str, object]:
    path = run / name
    return json.loads(path.read_text()) if path.exists() else {}


def display_metric(column, label: str, value: object) -> None:
    column.metric(label, "n/a" if value is None or pd.isna(value) else value)


st.set_page_config(page_title="AegisX artifacts", layout="wide")
st.title("AegisX — reproducible historical-run inspector")
st.caption("This dashboard is read-only: it visualizes prepared artifacts and never connects to a venue or submits orders.")
default_run = Path(__file__).parents[1] / "runs" / "demo"
run = Path(st.sidebar.text_input("Prepared run directory", str(default_run))).expanduser()
st.sidebar.caption("Create artifacts with `aegisx replay`, `aegisx simulate`, `aegisx risk-demo`, and `python/scripts/run_research.py`.")

if not run.exists():
    st.info("No prepared run exists at this path. Select a completed output directory to inspect it.")
    st.stop()

metadata = load_json(run, "metadata.json")
research_metadata = load_json(run / "research", "research_metadata.json")
snapshots = load_csv(run, "snapshots.csv")
execution = load_csv(run, "execution_comparison.csv")
children = load_csv(run, "child_orders.csv")
fills = load_csv(run, "execution_fills.csv")
risk = load_csv(run, "risk_rejections.csv")
model_metrics = load_csv(run / "research", "model_metrics.csv")
economic_metrics = load_csv(run / "research", "economic_metrics.csv")

overview, book, microstructure, execution_tab, risk_tab, research, provenance = st.tabs(
    ["Overview", "Order book", "Microstructure", "Execution", "Risk", "Research", "Provenance"]
)

with overview:
    last = snapshots.iloc[-1] if not snapshots.empty else pd.Series(dtype=object)
    first, second, third, fourth = st.columns(4)
    display_metric(first, "Processed events", metadata.get("processed_events"))
    display_metric(second, "Best bid (ticks)", last.get("best_bid_price_ticks"))
    display_metric(third, "Best ask (ticks)", last.get("best_ask_price_ticks"))
    display_metric(fourth, "Active orders", last.get("active_orders"))
    st.caption("All values are sourced from local output files; unavailable metrics are shown as n/a.")

with book:
    if snapshots.empty:
        st.info("No replay snapshots were found.")
    else:
        columns = [name for name in ["mid_price_ticks", "bid_depth", "ask_depth"] if name in snapshots]
        st.line_chart(snapshots.set_index("timestamp_ns")[columns])
        st.dataframe(snapshots.tail(50), use_container_width=True, hide_index=True)

with microstructure:
    if snapshots.empty:
        st.info("No snapshot artifact is available.")
    else:
        columns = [name for name in ["spread_ticks", "level_one_imbalance", "top_n_imbalance", "microprice_ticks"] if name in snapshots]
        if columns:
            st.line_chart(snapshots.set_index("timestamp_ns")[columns])
        else:
            st.info("This run has no microstructure columns.")

with execution_tab:
    if execution.empty:
        st.info("No execution comparison artifact is available.")
    else:
        st.dataframe(execution, use_container_width=True, hide_index=True)
        st.bar_chart(execution.set_index("strategy")[["fill_rate"]])
    if not children.empty:
        st.caption("Child order audit trail")
        st.dataframe(children, use_container_width=True, hide_index=True)
    if not fills.empty:
        st.caption("Simulated fill audit trail")
        st.dataframe(fills, use_container_width=True, hide_index=True)

with risk_tab:
    if risk.empty:
        st.info("No risk decision/rejection artifact is available.")
    else:
        st.dataframe(risk, use_container_width=True, hide_index=True)
        st.bar_chart(risk.assign(count=1).groupby("reject_reason")[["count"]].sum())

with research:
    if not research_metadata:
        st.info("No completed research artifact is available. The research pipeline only reports model metrics when simulator labels include both classes.")
    else:
        st.json(research_metadata)
    if not model_metrics.empty:
        st.dataframe(model_metrics, use_container_width=True, hide_index=True)
    if not economic_metrics.empty:
        st.dataframe(economic_metrics, use_container_width=True, hide_index=True)

with provenance:
    st.json(metadata or {"status": "metadata.json not found"})
    artifacts = sorted(path.relative_to(run).as_posix() for path in run.rglob("*") if path.is_file())
    st.caption("Files visible to this dashboard")
    st.code("\n".join(artifacts) or "No files", language="text")
