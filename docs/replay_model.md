# Replay model and event semantics

AegisX replays framed Nasdaq TotalView-ITCH 5.0 messages in file order. Timestamps
are nanoseconds since midnight, but file order is the tie-breaker when messages
share a timestamp. The supported message table and unsupported-message policy
are documented in `itch_validation.md`.

`MarketState` owns one FIFO `OrderBook` per stock locate. A global order-ID route
prevents the same active ID from existing in two instruments. Every mutation
validates its complete precondition before changing state. Replay snapshots are
observations; they never mutate a book.

The execution simulator maintains three distinct concepts:

1. **Historical state** is the reconstructed feed and is never reduced by
   simulated orders.
2. **Visible state** is a separately replayed view delayed by configured
   market-data latency and is the only state used for strategy decisions.
3. **Shadow consumption** records displayed liquidity consumed by aggressive
   simulated fills. Historical adds at the same side and price restore shadow
   availability; a fill cannot consume the same displayed quantity twice.

At a parent arrival timestamp, messages are processed in recorded order until a
two-sided book first permits a strategy decision. The arrival benchmark is then
captured before that decision and remains immutable. It contains bid, ask, mid,
microprice, spread, and five-level displayed depth.

Reproducibility is checked with logical and full-state checksums. The independent
reference fixture SHA-256 is
`ccd343057636251f03de9a2986ec30d0ceadbe1aaa8bc70e8f3e301555021ffa`.

Known boundary: this is deterministic historical event replay, not a
packet-capture clock model. It has no exchange sequence-gap recovery or
cross-venue synchronization.
