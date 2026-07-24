# Demo script

## First run

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
ctest --test-dir build/release --output-on-failure
python -m pip install ./python
python scripts/run_real_pipeline.py
```

The data step downloads approximately 841 MB directly from Nasdaq. Subsequent
runs can use:

```bash
python scripts/run_real_pipeline.py --skip-download
```

## Show the evidence

```bash
PYTHONPATH=python streamlit run dashboard/app.py
```

The dashboard defaults to `runs/real-bx-20190830-aapl`.

Walk through:

1. complete official archive sizes and SHA-256 provenance;
2. 62.7M full-feed frames scanned and zero unknown AAPL message types;
3. deterministic replay checksums and FIFO state;
4. July-trained/August-evaluated VWAP profile;
5. the five completed execution size comparisons;
6. adaptive urgency cancellation and aggressive replacement;
7. risk rejection and reservation lifecycle;
8. the 91/30/30 research split and negative out-of-sample model result.

Synthetic fixture commands belong only in the test demonstration, not in the
market-evidence walkthrough.
