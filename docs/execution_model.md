# Execution model

`ExecutionSimulator` is a deterministic historical-book simulator, not a live execution algorithm. Parent orders contain ID, stock locate, symbol, side, target, arrival, and end time. A fresh `MarketState` is reconstructed for each strategy.

- **TWAP** uses equal cumulative targets.
- **VWAP** uses caller-supplied non-negative interval weights.
- **POV** reacts only to observed printable trade messages.
- **Adaptive** selects a passive limit or aggressive market child from schedule deficit, urgency, and displayed spread.

Market children walk opposite-side displayed levels and record shadow-consumed liquidity, preventing repeated use of the same historical depth within a run. Passive children carry queue-ahead depth; executions reduce queue-ahead before a maker fill and cancels reduce it by a configured fraction. Decision/transmission latency delays activation. At a forced horizon, open children are cancelled and a final market attempt is made without inventing liquidity.

Artifacts include child/fill audit trails, fill rate, average price, side-aware arrival-midpoint shortfall, fees, opportunity cost, passive/aggressive ratios, child count, and depth consumed. These are model outputs conditional on documented assumptions, not venue measurements or trading recommendations.
