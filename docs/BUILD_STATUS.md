# Build status

This document records reproducible verification of AegisX. Results are reported
as observed; no production or exchange-performance claim is implied.

## Final-upgrade acceptance — 2026-07-24

### Compiler and build

- Debug configure/build: **PASS**
- Release configure/build: **PASS**
- Compiler: AppleClang 17, C++20
- Warning policy: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`

### Native tests

- CTest Debug: **4/4 PASS**
- CTest Release: **4/4 PASS**
- Native Catch2 coverage: **19 test cases / 245,893 assertions PASS**
- Reference ITCH fixture checksum:
  `ccd343057636251f03de9a2986ec30d0ceadbe1aaa8bc70e8f3e301555021ffa`
- Deterministic replay, execution-strategy, risk, and randomized FIFO coverage
  are included in the Catch2 test executable.

### Sanitizers

- Fresh AppleClang AddressSanitizer/UndefinedBehaviorSanitizer build: **4/4 PASS**
- Final sanitized native test elapsed time: 14.53 seconds
- Leak detection is unavailable in the local macOS ASan runtime; hosted Linux
  CI enables it.

### Python

- Pytest: **9/9 PASS**
- Dashboard, research package, and scripts bytecode smoke test: **PASS**

### Hosted CI

- GitHub Actions run
  [`30109210015`](https://github.com/SatvikK-07/AegisX/actions/runs/30109210015)
  for commit `9e00593` completed **SUCCESS**.
- GCC Debug/Release: **PASS**
- Clang Debug/Release: **PASS**
- Linux GCC ASan/UBSan with leak detection: **PASS**
- clang-format and production-target clang-tidy: **PASS**
- Python research/portfolio tests and dashboard smoke: **PASS**
- Workflow actions use their Node 24 releases (`checkout@v5`,
  `setup-python@v6`).

### Current benchmark evidence

- Synthetic parser throughput: **10.280M events/second median**
- Synthetic replay throughput: **6.989M events/second median**
- Deterministic execution scenarios: **89.3276 bps mean adaptive-vs-TWAP
  savings across 6 synthetic regimes** (113.316 bps standard deviation)
- Synthetic risk benchmark: **0.125 microseconds p99 approval latency** and
  **7.760M approval/release checks/second across 1,000,000 checks**

Benchmark methodology, machine details, commands, and limitations are recorded
in `docs/PERFORMANCE.md`.

## Earlier local validation

- Clean offline CMake configuration with vendored Catch2: **PASS**
- GCC 15.2 Debug and Release builds with warnings as errors: **PASS**
- AppleClang 17 Debug and Release builds with warnings as errors: **PASS**
- C++ formatting check with clang-format 22.1.8: **PASS**
- Dashboard and research Python bytecode compilation: **PASS**
- Reference fixture replay, execution simulation, risk-demo artifact
  generation, and smoke benchmark: **PASS**
- Two independent replay/simulation/risk/experiment output directories:
  **byte-for-byte identical**
