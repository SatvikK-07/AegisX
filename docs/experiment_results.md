# Experiment results

## Execution

The final deterministic run covers six synthetic regimes and four strategies.
All 24 strategy/scenario combinations completed their ten-share parent orders.
The adaptive policy used passive fills in five regimes and became fully
aggressive in thin liquidity. Relative to TWAP, adaptive savings averaged
**89.3276 bps** with a **113.316 bps** population standard deviation.

The high-volatility scenario contributes most of the mean. That sensitivity is
visible in the reported standard deviation and is why this result must not be
generalized beyond the fixture. Exact rows are generated in
`execution_benchmark.csv`.

## Risk

The deterministic risk demonstration emits approved/rejected decisions and an
audit lifecycle for:

- valid approval;
- order quantity and notional;
- projected position;
- gross, net, and symbol concentration;
- open-order reservation exposure;
- order rate;
- price collar and stale marks;
- daily loss and drawdown;
- kill switch.

The one-million-check latency result is reported in `PERFORMANCE.md`.

## Research

The research pipeline is verified on deterministic simulator-shaped fixtures.
It builds passive-fill labels, positive-is-worse adverse-selection labels,
lagged/contemporaneous features, purged chronological partitions, two
baselines, logistic regression, boosted trees, classification/calibration
metrics, and economic metrics.

No model score is claimed for the bundled ITCH fixtures. They do not contain
enough independent passive decisions or sessions for a defensible out-of-sample
estimate. The runner intentionally stops on one-class or undersized data rather
than publishing manufactured results.
