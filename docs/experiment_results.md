# Experiment results

## Data

The pipeline scanned 28,734,686 messages from July 30, 2019 and 33,966,990
messages from August 30, 2019—both complete official Nasdaq BX ITCH 5.0
archives. July supplies the volume profile; August is the later evaluation
session. See `real_data.md` for checksums.

## Execution

The canonical size sweep uses AAPL buy parents of 100, 250, 500, 750, and
1,000 shares. TWAP and adaptive completed all five. Adaptive simulated savings
versus TWAP were 26.895, 26.599, 26.409, 24.660, and 20.632 bps respectively:
**25.039 bps mean**, **26.409 bps median**, and **2.338 bps population standard
deviation**.

For the 1,000-share parent:

| Strategy | Fill rate | Average price ticks | Shortfall bps | Passive ratio |
|---|---:|---:|---:|---:|
| TWAP | 1.000 | 2,083,187.9 | -83.127 | 0.000 |
| VWAP | 1.000 | 2,111,982.7 | 53.949 | 0.000 |
| POV | 1.000 | 2,099,779.6 | -4.143 | 0.000 |
| Adaptive | 1.000 | 2,078,889.9 | -103.587 | 0.301 |

The result comes from one historical venue/symbol/day/side. It establishes
reproducible simulator behavior on observed messages, not expected future
savings.

## Research

Fifty-two staggered, alternating-side real-session parent simulations produced
153 passive child observations. Hard gates require at least 100 total rows, 20
rows in each chronological partition, both fill classes in train/validation/
test, purged forward-label boundaries, and matching replay/execution/provenance
checksums.

The final split is 91 train, 30 validation, and 30 test rows. Results do not
show predictive edge:

| Model | ROC AUC | PR AUC | F1 |
|---|---:|---:|---:|
| Unconditional baseline | 0.500 | 0.367 | 0.000 |
| Time-bucket baseline | 0.500 | 0.367 | 0.000 |
| Imbalance rule | 0.519 | 0.379 | 0.154 |
| Logistic regression | 0.500 | 0.381 | 0.000 |
| Boosted trees | 0.371 | 0.348 | 0.000 |

The repository therefore claims no alpha or useful fill predictor. The
negative result is retained instead of tuning on the held-out partition.

## Risk

The risk demonstration emits decisions and lifecycle audit rows for quantity,
notional, position, gross/net/symbol exposure, reservations, open orders, rate,
collar, stale market data, daily loss, drawdown, and kill switch. The
one-million-check measurement is reported in `PERFORMANCE.md`.
