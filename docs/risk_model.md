# Risk model

`RiskEngine` is a deterministic pre-trade guard with accepted-order reservations. It checks order quantity/notional, mark freshness, price collar, projected position, per-symbol concentration, gross/net exposure, open-order exposure, order-rate window, daily loss, drawdown, enabled state, and kill switch. Decisions return a typed `RiskRejectReason`, limit name, observed value, and configured limit.

Approved requests reserve remaining quantity and exposure. Fills update reservations, signed position, average open price, realized P&L, fees, and marked unrealized P&L. Marks update high-water P&L and drawdown. Explicit checked add/subtract/multiply helpers reject overflow rather than silently wrapping.

The engine is synchronous and local to a simulation. It does not provide persistence, exchange acknowledgements, account reconciliation, credit control, or regulatory suitability checks.
