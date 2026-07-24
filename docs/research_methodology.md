# Research methodology

The Python pipeline consumes generated `snapshots.csv`, `child_orders.csv`, and `execution_fills.csv`. Passive-fill labels are derived only from the simulator's maker fills and submitted quantities. Contemporaneous or lagged features include spread, depth, top imbalance, order-flow imbalance, lagged midpoint return, and microprice deviation. Future midpoint movement is an adverse-selection **label**, never a model feature.

Rows are sorted chronologically and split into train, validation, and test periods with an embargo gap. `assert_no_leakage` rejects labels and future-midpoint fields passed as model inputs. Experiments compare a fill-rate baseline, an imbalance rule, logistic regression, and deterministic histogram-gradient boosting. The artifact writer stores Parquet data plus a SHA-256 manifest; model and economic metrics are separate CSV outputs.

Economic evaluation reports realized fill rate, adverse selection, and net ticks after configurable fees/rebates for selected decisions. It is deliberately not a Sharpe ratio or profitability claim. The bundled fixture has insufficient independent passive decisions for a credible fitted model, so the runner stops rather than generating fabricated scores.
