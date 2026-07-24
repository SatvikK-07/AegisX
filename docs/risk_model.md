# Risk model

`RiskEngine` is a deterministic pre-trade guard with accepted-order
reservations. It checks child and parent quantity, notional, mark freshness,
price collar, projected position, per-symbol and fractional concentration,
gross/net exposure including reservations, open-order exposure and count,
monotonic sliding-window order rate, daily loss, drawdown, enabled state, and
kill switch. Decisions return a typed `RiskRejectReason`, limit name, observed
value, and configured limit without mutating state on rejection.

Approved requests reserve remaining quantity and exposure. Fills update
reservations, signed position, average open price across increases, reductions,
closures and reversals, realized P&L, fees, unrealized P&L, and total P&L. Marks
update high-water P&L and drawdown. Execution children call the same approval,
fill, and release lifecycle. The audit log records decisions, fills, effective
cancellations, and kill activation/release. Explicit checked
add/subtract/multiply helpers reject overflow rather than silently wrapping.

Python portfolio analytics add historical and parametric VaR/Expected
Shortfall, Euler component volatility, spread/impact-adjusted liquidation
notional, gross/net/concentration summaries, drawdown, and named
symbol/sector/market shocks.

The engine is synchronous and local to a simulation. It does not provide
persistence, exchange acknowledgements, account reconciliation, credit control,
or regulatory suitability checks.
