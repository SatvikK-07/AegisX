# AegisX

AegisX is a C++20 and Python market-microstructure research platform that
downloads official historical Nasdaq TotalView-ITCH 5.0 samples, validates
their provenance, reconstructs FIFO limit-order books, and evaluates
queue-aware execution and pre-trade risk controls. Synthetic messages remain
only as small deterministic unit-test fixtures.

AegisX is not connected to an exchange or broker and never transmits orders.
Execution results are historical-replay simulations, not live fills or
profitability claims.

## Real-data workflow

The canonical experiment uses complete Nasdaq BX sessions from July 30 and
August 30, 2019. July trains the VWAP volume profile; August is held later in
time for replay and evaluation. Both archives are downloaded directly from the
[official Nasdaq EMI archive](https://emi.nasdaq.com/ITCH/Nasdaq%20BX%20ITCH/).

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
python -m pip install ./python
python scripts/run_real_pipeline.py
```

The first run downloads approximately 841 MB of compressed source data. Raw and
derived market data are deliberately ignored by Git. If both archives already
exist at their configured sizes, use:

```bash
python scripts/run_real_pipeline.py --skip-download
```

The pipeline:

1. downloads both official archives with resumable verified byte ranges;
2. validates complete gzip streams and computes SHA-256 checksums;
3. scans every framed exchange message and extracts provenance-tracked AAPL
   streams;
4. builds a July volume profile without using August observations;
5. runs strict replay, execution, risk, performance, and research experiments;
6. refuses research output unless provenance checksums match and minimum
   chronological sample/class gates pass.

See [real-data provenance](docs/real_data.md) for source URLs and checksums.

## Measured local evidence

Release measurements on the documented Apple M3 environment:

- official messages scanned: **62,701,676** across two complete sessions;
- August AAPL stream: **319,100 framed / 314,151 decoded messages**, with zero
  unknown message types;
- median real-data parser throughput: **9.032M messages/second**;
- median real-data replay throughput: **1.827M events/second**;
- adaptive execution: **25.039 bps average simulated savings versus TWAP**
  across five fully completed AAPL buy-size tests from 100 to 1,000 shares in
  the August session;
- risk: **0.084 μs p99 approval latency** and **8.882M approval/release
  checks/second** over one million iterations.

The five execution observations are a size-sensitivity experiment on one
venue, symbol, side, and day. They are not evidence of expected savings in
other markets. The real passive-fill research set contains 153 observations;
its models do not beat the baselines convincingly, so no predictive-edge claim
is made.

## Build and test

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
PYTHONPATH=python python -m pytest -q
```

Catch2 is vendored, and CMake configuration is offline. Production targets use
warnings as errors. Normal CI runs GCC and Clang Debug/Release, ASan/UBSan,
formatting, clang-tidy, Python tests, and dashboard compilation. The separate
manual real-data workflow downloads both full sessions and uploads only
derived evidence—not raw exchange archives.

## Dashboard

```bash
PYTHONPATH=python streamlit run dashboard/app.py
```

The default run is `runs/real-bx-20190830-aapl`. The dashboard displays
external-source provenance, checksum consistency, replay state, execution
size sensitivity, risk evidence, research metrics, and benchmark results.

## Deterministic fixtures

`data/fixtures/` remains committed for fast edge-case and CI tests. These files
are generated and must never be presented as market evidence.

## Documentation

- [Real data and provenance](docs/real_data.md)
- [Architecture](docs/architecture.md)
- [ITCH validation](docs/itch_validation.md)
- [Replay model](docs/replay_model.md)
- [Execution model](docs/execution_model.md)
- [Risk model](docs/risk_model.md)
- [Research methodology](docs/research_methodology.md)
- [Performance methodology](docs/PERFORMANCE.md)
- [Experiment results](docs/experiment_results.md)
- [Requirement audit](docs/requirements_audit.md)
- [Limitations](docs/limitations.md)
- [Portfolio content](docs/portfolio_content.md)
