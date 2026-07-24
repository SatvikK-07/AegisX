# Benchmark report

`scripts/generate_benchmark_fixture.py --pairs 500000` produces a deterministic 29.5 MB synthetic stream containing 1,000,001 framed events: one stock directory and 500,000 valid add/delete pairs. The `benchmark` command reports parser and replay wall-clock time; all replay mutations remain valid and the final book is empty.

On an Apple M3 with Homebrew GCC 15.2 Release, three runs produced parser/replay times of 111.214/150.610 ms, 108.890/149.413 ms, and 108.287/149.568 ms. The median replay result is **6.69M events/sec** and the median parser result is **9.18M events/sec**.

The `execution-benchmark` command runs three deterministic synthetic displayed-book scenarios. Adaptive execution averaged **139.0 bps** lower execution price than TWAP for a buy parent in those scenarios. `risk-benchmark --iterations 100000`, repeated three times on the same machine, recorded **26 μs p99 approval latency** and **74.9K approvals/sec**. It measures approval only; release occurs outside the timed interval.

These measurements are local synthetic benchmarks. They are not external-feed throughput, calibrated market-regime evidence, live execution quality, or production latency guarantees. Use licensed representative data, fixed hardware, warm-up, repetitions, and distributional reporting before making any such claim.
