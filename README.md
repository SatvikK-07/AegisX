# AegisX

AegisX is a deterministic C++20 market-microstructure research platform for replaying a documented Nasdaq TotalView-ITCH 5.0 message subset, rebuilding per-instrument FIFO books, evaluating transparent execution policies, and testing pre-trade risk decisions. It is not connected to an exchange or broker, does not transmit orders, and makes no performance or profitability claim.

## Build and validate

The Catch2 test framework is vendored in `third_party/Catch2`; configuration does not download dependencies.

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
```

The project treats warnings as errors for AegisX targets with `-Wall`, `-Wextra`, `-Wpedantic`, `-Wconversion`, and `-Wshadow`. The CI workflow defines GCC and Clang Debug/Release jobs, a GCC ASan/UBSan job, formatting, clang-tidy, Python tests, and a dashboard syntax smoke test.

## Reproducible local run

```bash
./build/release/aegisx replay --input data/fixtures/itch_reference_segment.itch --output runs/demo --snapshot-every-events 1
./build/release/aegisx simulate --input data/fixtures/aegisx_itch_sample.itch --output runs/demo
./build/release/aegisx risk-demo --output runs/demo
./build/release/aegisx benchmark --input data/fixtures/itch_reference_segment.itch --output runs/demo
PYTHONPATH=python streamlit run dashboard/app.py
```

Replay accepts `--stock-locate`, `--symbol`, timestamp/event-range filters, `--snapshot-every-events`, `--snapshot-every-ns`, and `--top-levels`. Metadata records the source path, SHA-256, scale, decode counts, and logical and full-state checksums. `runs/` contains generated artifacts and is intentionally ignored.

For a sufficiently long run with both filled and unfilled passive simulator children, create a research artifact with:

```bash
PYTHONPATH=python python python/scripts/run_research.py --run runs/demo
```

The bundled fixtures are intentionally too short to support a credible fitted model; the command correctly stops rather than manufacturing model metrics when there are not two observed passive-fill classes.

## Documentation

- [Architecture](docs/architecture.md)
- [ITCH validation](docs/itch_validation.md)
- [Order-book validation](docs/order_book_validation.md)
- [Execution model](docs/execution_model.md)
- [Risk model](docs/risk_model.md)
- [Research methodology](docs/research_methodology.md)
- [Benchmark report](docs/benchmark_report.md)
- [Assumptions](docs/assumptions.md), [limitations](docs/limitations.md), and [demo script](docs/demo_script.md)

The small committed fixtures are synthetic and specification-derived. They are not exchange data and must not be treated as evidence of production latency, fill quality, capacity, or model performance.
