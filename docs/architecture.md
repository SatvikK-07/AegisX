# Architecture

```mermaid
flowchart LR
  Feed[ITCH bytes] --> Parser[Bounds-checked parser]
  Parser --> Replay[Streaming replay]
  Replay --> State[MarketState]
  State --> Books[Per-instrument FIFO books]
  Parser --> Execution[Historical + visible execution state]
  Execution --> Risk[Risk approvals and reservations]
  State --> Artifacts[CSV and JSON artifacts]
  Execution --> Artifacts
  Risk[RiskEngine] --> Artifacts
  Artifacts --> Research[Python and Parquet]
  Artifacts --> Dashboard[Read-only dashboard]
```

`NasdaqItchParser` owns structural framing, exact known-type validation, strict/permissive unknown handling, statistics, and callback delivery. `MarketState` routes every active order ID globally to exactly one stock locate; an `OrderBook` owns price levels, FIFO queues, and iterator locators. Mutations validate all preconditions before they change state.

Replay consumes parser callbacks and does not retain the full file. Every
execution strategy rebuilds fresh historical and latency-delayed visible state,
so no strategy can mutate another. Aggressive simulated consumption is kept in
a third shadow ledger. Generated CSV, JSON, and Parquet artifacts are the only
interface to the read-only dashboard and research code. No component opens a
network connection or submits an order.
