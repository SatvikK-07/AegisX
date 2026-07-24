# Build status

This document records reproducible verification of AegisX. Results are reported
as observed; no production or exchange-performance claim is implied.

## Real-data upgrade acceptance — 2026-07-24

### Official data and replay integrity

- Source: two complete Nasdaq BX TotalView-ITCH 5.0 sessions from the official
  Nasdaq EMI archive (`20190730.BX_ITCH_50.gz` and
  `20190830.BX_ITCH_50.gz`).
- Full compressed-feed SHA-256 checksums:
  - `a0a057010cc5172cfaf2d8a7b4d5f133557fa47b3a5837fcf985b6e5533e50eb`
  - `99b08ce5e0e860246e1d6716394513a06845ed632c8dc838b807d43f4c756fa0`
- Full-feed frames scanned: **62,701,676**.
- Derived AAPL segments: **198,850** and **319,100** framed messages, each
  bound to the source-feed checksum by a machine-readable provenance record.
- Strict evaluation replay: **314,151 decoded**, **4,949 recognized
  administrative skips**, **0 unknown messages**.
- Deterministic logical checksum: `5920404578634390906`.
- Deterministic full-state checksum: `6816335085916752388`.
- An independent fresh end-to-end pipeline produced byte-identical core replay,
  execution, risk, and research artifacts.

The raw exchange files and derived local artifacts are intentionally Git-ignored;
the downloader, preparation code, source URLs, expected sizes, checksums, and
reproduction command are version controlled.

### Compiler, tests, and static checks

- AppleClang 17 Debug and Release CTest: **4/4 PASS**.
- GCC 15.2 Debug and Release CTest: **4/4 PASS**.
- Native Catch2 coverage: **20 test cases / 245,910 assertions PASS**.
- Fresh AppleClang AddressSanitizer/UndefinedBehaviorSanitizer build:
  **4/4 PASS**.
- Pytest: **10/10 PASS**.
- C++ formatting check: **PASS**.
- Dashboard, research package, and root scripts bytecode smoke test: **PASS**.
- Local `clang-tidy` was unavailable; the version-controlled Linux CI job
  installs and runs it. The current hosted result will be recorded after push.

### Measured real-data performance

Canonical five-trial run on the local Apple Silicon host:

- ITCH parser median: **9.031973 million messages/second**.
- Stateful replay median: **1.8270785 million decoded events/second**.
- Risk engine, 1,000,000 approval/release iterations:
  - p50: **0.083 microseconds**
  - p95: **0.084 microseconds**
  - p99: **0.084 microseconds**
  - throughput: **8.88231 million checks/second**

### Execution and research evidence

- The July session supplies the volume profile; the August session is the
  strictly later out-of-sample execution session.
- Five completed AAPL buy size tests (100, 250, 500, 750, and 1,000 shares)
  produced **25.0390 bps mean** and **26.4087 bps median** adaptive-vs-TWAP
  simulated savings.
- The canonical 1,000-share adaptive parent completed with **30.1% passive**
  quantity and seven cancel/replace operations.
- These are simulator results for one symbol, venue, day, side, and parameter
  set—not observed broker fills or a general performance claim.
- The leakage-resistant research run used 153 passive-fill observations with
  chronological 91/30/30 train/validation/test partitions. No candidate model
  demonstrated credible predictive edge; the report states that negative
  result rather than promoting an unstable score.

Benchmark methodology, machine details, commands, and limitations are recorded
in `docs/PERFORMANCE.md`, `docs/experiment_results.md`, and
`docs/limitations.md`.

### Hosted CI

- The previous repository revision passed the complete seven-job Linux matrix.
- Current-revision GCC/Clang, sanitizer, formatting, clang-tidy, Python, and
  real-data workflow results are pending publication and will replace this
  provisional note.
