# Portfolio and interview content

## Defensible résumé bullets

**AegisX | C++20, Python, Nasdaq ITCH, DuckDB, Streamlit**

- Built a C++20 TotalView-ITCH replay and FIFO limit-order-book engine,
  validating **62.7M messages across two complete official Nasdaq BX
  sessions**, deterministic checksums, sanitizers, and **40,000 randomized FIFO
  operations**; measured **1.827M real replay events/sec** and **9.032M parser
  messages/sec**.

- Implemented TWAP, earlier-session-profile VWAP, POV, and queue-aware adaptive
  execution with passive fills, latency stages, fees, adverse selection, and
  urgency cancel/replace; adaptive simulated **25.039 bps lower cost than TWAP
  across five fully completed 100–1,000 share AAPL size tests** on the held-later
  real Nasdaq BX session.

- Achieved **0.084 μs p99 approval latency at 8.882M approval/release
  checks/sec** over one million iterations with position, gross/net exposure,
  concentration, reservation, rate, stale-data, loss, drawdown, and kill-switch
  controls.

The second bullet must retain “simulated,” the one-session scope, and the tested
size range.

## Interview walkthrough

1. Show `docs/real_data.md`: official URLs, complete archive sizes, and
   full/derived SHA-256 checksums.
2. Explain why July supplies the profile and August remains later in time.
3. Trace BinaryFILE framing into strict decoding, stock-locate routing, FIFO
   books, and deterministic state checksums.
4. Compare the five completed execution size tests and explain the adaptive
   urgency cancel/replace transition.
5. Show the risk reservation and kill-switch audit lifecycle.
6. Present the negative real-data ML result and explain why it is more credible
   than manufacturing an impressive score from 153 observations.

## Claims not to make

- production exchange throughput or co-located latency;
- live fills, realized profit, or expected 25 bps savings;
- multi-regime robustness;
- a useful fill-prediction model;
- full ITCH semantic coverage or production risk-service readiness.
