# Research methodology

## Inputs and provenance

Research features come from the August real-session replay. Labels come from a
separate set of 52 staggered, alternating-side parent simulations over that
same session. Replay and execution must match the attached official-data
segment SHA-256; `--require-real-data` also requires complete-session
provenance.

## Labels and features

Passive-fill labels are derived from simulated maker fills and submitted child
quantities, never inferred from future prices. Features are contemporaneous or
lagged: spread, depth, top imbalance, order-flow imbalance, lagged midpoint
return, microprice deviation, rolling volatility, trade intensity, depth
ratios, log depth, time of day, and simulated queue position.

Future midpoint movement is an adverse-selection label and is forbidden as a
feature. Positive is worse: midpoint up after a passive buy or down after a
passive sell.

## Chronology and minimum evidence

Rows are sorted and divided into train, validation, and test periods with an
embargo. Forward-label windows crossing a boundary are purged. The canonical
configuration requires:

- at least 100 passive observations;
- at least 20 rows in each partition after purge/embargo;
- both fill classes in every partition;
- distinct source/evaluation sessions for the VWAP profile;
- matching replay, execution, and provenance checksums.

The final real dataset has 153 rows split 91/30/30.

## Models and result

Experiments compare unconditional and time-bucket baselines, an imbalance rule,
logistic regression, and deterministic histogram-gradient boosting. Reports
include ROC AUC, PR AUC, F1, Brier score, calibration error, and the complete
confusion matrix.

The best ROC AUC in the canonical test is 0.519 from the transparent imbalance
rule; the boosted model records 0.371. This is a negative result. No predictive
edge is claimed, and the held-out partition is not used for retuning.

Economic output reports selected decisions, realized simulated fill rate,
adverse selection, and net ticks. It is not a Sharpe ratio or profitability
claim. Parquet artifacts and configuration files carry SHA-256 manifests.
