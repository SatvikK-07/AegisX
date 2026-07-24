# Benchmark report

The final local benchmark evidence, input checksum, individual runs, timing
boundaries, compiler, machine, and limitations are recorded in
[`PERFORMANCE.md`](PERFORMANCE.md).

Headline results from the 2026-07-24 Apple M3 / AppleClang 17 Release run:

- parser: **10.280M synthetic events/second median**;
- replay: **6.989M synthetic events/second median**;
- execution: **89.3276 bps mean adaptive-vs-TWAP savings** across six
  deterministic synthetic regimes, 113.316 bps standard deviation;
- risk approval: **0.125 μs p99** over one million checks;
- risk end-to-end approval plus reservation release: **7.760M checks/second**.

These are synthetic local measurements, not external-feed, live execution,
profitability, production-capacity, or SLO claims.
