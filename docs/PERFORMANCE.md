# Performance methodology

These are local historical-data and risk-engine measurements. They are not
co-located exchange capacity, production SLOs, live execution, or expected
profitability.

## Environment

- Host: Apple M3, arm64
- OS: macOS 26.2
- Compiler: AppleClang 17
- Build: CMake Release, C++20
- Date: 2026-07-24

## Real parser and replay

Input: the complete-session-derived August AAPL stream documented in
`real_data.md`, SHA-256
`e1c89702da15158c1c2b5514119c19dcdd05ddfb9ff1272c0d6f6a91f3a6638e`.
Each fresh-process trial parsed 319,100 framed messages and replayed 314,151
decoded events.

| Run | Parser ns | Parser M msg/s | Replay ns | Replay M events/s |
|---:|---:|---:|---:|---:|
| 1 | 35,847,500 | 8.902 | 176,219,125 | 1.783 |
| 2 | 35,065,333 | 9.100 | 174,429,375 | 1.801 |
| 3 | 35,270,708 | 9.047 | 171,237,333 | 1.835 |
| 4 | 35,568,875 | 8.971 | 171,941,708 | 1.827 |
| 5 | 35,330,042 | 9.032 | 171,729,125 | 1.829 |
| **Median** | **35,330,042** | **9.032** | **171,941,708** | **1.827** |

Parser timing includes file reading, framing, validation, decoding,
administrative counting, and event-vector construction. Replay timing starts
after parsing and includes FIFO book mutations and checksums.

## Real historical execution simulation

The held-later August session evaluates five AAPL buy parents of 100, 250, 500,
750, and 1,000 shares. TWAP and adaptive both completed every parent. Adaptive
uses urgency-driven passive cancel/replace before the deadline.

| Quantity | Adaptive savings vs TWAP (bps) | Adaptive passive ratio |
|---:|---:|---:|
| 100 | 26.895 | 0.460 |
| 250 | 26.599 | 0.456 |
| 500 | 26.409 | 0.414 |
| 750 | 24.660 | 0.375 |
| 1,000 | 20.632 | 0.301 |
| **Mean** | **25.039** | — |

Population standard deviation is 2.338 bps. This is a one-day, one-symbol,
one-venue, one-side size sensitivity—not five independent market regimes. The
canonical sweep uses zero configured fees and latency, so the reported savings
are gross model output rather than net implementation cost.

## Risk

`risk-benchmark --iterations 1000000` enables all documented limits and
releases each accepted reservation. Audit logging is disabled only in this
microbenchmark. Approval latency excludes release; throughput includes
approval plus release.

| Metric | Result |
|---|---:|
| p50 | 83 ns |
| p95 | 84 ns |
| p99 | 84 ns (0.084 μs) |
| maximum | 15,708 ns |
| approval/release checks per second | 8.882M |

The reproducible evidence is written under
`runs/real-bx-20190830-aapl/performance/` by
`scripts/benchmark_real_data.py`.
