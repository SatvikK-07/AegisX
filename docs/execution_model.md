# Execution model

`ExecutionSimulator` is a deterministic historical-book simulator, not a live
execution algorithm. Parent orders contain ID, stock locate, symbol, side,
target, arrival, and end time. A fresh historical and latency-delayed visible
`MarketState` is reconstructed for each strategy.

- **TWAP** uses equal cumulative targets.
- **VWAP** uses a prior-session profile with named training/evaluation sessions
  and source checksum; those sessions must be distinct.
- **POV** targets cumulative participation in the parent instrument's printable
  volume with a minimum actionable child.
- **Adaptive** emits a decision trace and selects wait, passive, or aggressive
  action from schedule deficit, urgency, spread, depth imbalance, and remaining
  time. In the real-data workflow it cancels remaining passive exposure during
  the urgent final quarter and replaces it with bounded aggressive children.

Market children walk a bounded number of opposite-side displayed levels, obey an
optional worst price, and record side/price shadow consumption. Passive children
join behind historical depth and earlier simulated children. An execution or
eligible cancellation is a shared flow quantity processed once in FIFO order;
it cannot be reused by every child. Queue evolution is retained per child.
Market-data, decision, transmission, and exchange-ack latency have distinct
timestamps. At a forced horizon, open children are cancelled, risk reservations
are released, and a final bounded market attempt is made without inventing
liquidity.

The canonical VWAP profile is built from the complete July 30, 2019 AAPL
Nasdaq BX stream and evaluated on August 30, 2019. Five real-session size tests
compare 100–1,000 share buy parents. Synthetic execution scenarios remain only
as deterministic regression tests.

`ParentOrderState` enforces
`target = filled + open + remaining_unsubmitted + terminal_unfilled` after each
transition. Artifacts include decision, child, queue and fill audit trails;
average price; arrival shortfall in ticks/bps/currency; market VWAP; spread and
opportunity cost; adverse selection; an impact proxy; fees; passive/aggressive
ratios; depth consumed; completion; and maximum schedule deviation. These are
model outputs conditional on documented assumptions, not venue measurements or
trading recommendations.
