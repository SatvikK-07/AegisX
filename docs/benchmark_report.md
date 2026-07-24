# Benchmark report

The canonical real-data results are:

- parser: **9.032M official ITCH messages/second median**;
- replay: **1.827M decoded events/second median**;
- execution: **25.039 bps mean adaptive-vs-TWAP simulated savings** across
  five fully completed AAPL size tests in the held-later August session;
- risk approval: **0.084 μs p99** over one million checks;
- risk approval plus reservation release: **8.882M checks/second**.

Input checksums, timing boundaries, individual trials, machine details, and
scope limits are recorded in [`PERFORMANCE.md`](PERFORMANCE.md). These are
historical-replay and local-compute measurements, not live execution or
production-capacity claims.
