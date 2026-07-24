# Research methodology

The Python pipeline consumes generated `snapshots.csv`, `child_orders.csv`, and
`execution_fills.csv`. Passive-fill labels are derived only from the simulator's
maker fills and submitted quantities. Contemporaneous or lagged features include
spread, depth, imbalance, order flow, lagged return, microprice deviation,
rolling volatility, trade intensity, depth ratios, log depth, time-of-day, and
simulated queue position. Future midpoint movement is an adverse-selection
**label**, never a model feature. Positive adverse selection means worse:
midpoint up after a passive buy or down after a passive sell.

Rows are sorted chronologically and split into train, validation, and test
periods with an embargo. Training/validation rows whose forward label window
crosses the next boundary are purged. `assert_no_leakage` rejects labels and
future-midpoint fields passed as model inputs. Experiments compare unconditional
and historical time-bucket baselines, an imbalance rule, logistic regression,
and deterministic histogram-gradient boosting. Metrics include ROC AUC, PR AUC,
accuracy, precision, recall, F1, Brier score, calibration error, and the complete
confusion matrix. The artifact writer stores Parquet data plus a SHA-256
manifest; statistical and economic metrics are separate CSV outputs.

Economic evaluation reports realized fill rate, adverse selection, and net ticks after configurable fees/rebates for selected decisions. It is deliberately not a Sharpe ratio or profitability claim. The bundled fixture has insufficient independent passive decisions for a credible fitted model, so the runner stops rather than generating fabricated scores.
