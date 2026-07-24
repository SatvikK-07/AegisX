# Build status

## Local validation recorded during this upgrade

- Clean offline CMake configuration: passed with vendored Catch2.
- GCC 15.2 Debug and Release builds with warnings as errors: passed.
- Apple Clang 17 Debug and Release builds with warnings as errors: passed.
- GCC and Clang CTest suites: passed (C++ tests plus fixture smoke test).
- Apple Clang ASan/UBSan suite: passed. Leak detection is unavailable in the local macOS ASan runtime; hosted Linux CI enables it.
- Python research/portfolio tests: passed (7 tests).
- C++ formatting check with clang-format 22.1.8: passed.
- Dashboard and research Python bytecode compilation: passed.
- Reference fixture replay, execution simulation, risk-demo artifact generation, and smoke benchmark: passed.

## Defined but not locally claimable

The GitHub Actions workflow defines GCC/Clang Debug/Release, GCC ASan/UBSan, clang-format, clang-tidy, Python, and dashboard checks. Hosted CI has not been triggered from this local workspace, so this document does not claim a hosted status. Clang-tidy was not installed in the local toolchain at the time this document was written; local Homebrew GCC could not link its ASan runtime.

The independent specification-derived reference fixture SHA-256 is `ccd343057636251f03de9a2986ec30d0ceadbe1aaa8bc70e8f3e301555021ffa`.
