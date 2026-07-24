# Upgrade record

The upgrade moved the project to an offline CMake build with vendored Catch2, strict GCC/Clang-compatible warning flags, a table-driven streaming ITCH subset parser, global order-ID routing, strong replacement behavior, state checksums, deterministic randomized FIFO validation, replay provenance, and a specification-derived independent fixture.

Execution now has parent/child/fill ledgers, distinct TWAP/VWAP/POV/adaptive decisions, latency, shadow liquidity, queue-ahead passive handling, cancellation assumptions, fees, benchmarks, and auditable output files. Risk now reserves accepted exposure and evaluates portfolio-level limits, P&L, drawdown, rate limiting, and kill state. Python adds chronology-safe simulator-labelled research, Parquet manifests, economic evaluation, and portfolio helpers. The dashboard reads prepared artifacts without mutating them.

Remaining work belongs to future scope rather than an incomplete implementation claim: validation against licensed official feed data, empirical queue/impact calibration, durable multi-account risk infrastructure, and statistically defensible research on a representative dataset. The current synthetic fixtures remain intentionally unsuitable for production or financial-performance claims.
