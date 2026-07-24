# Demo script

1. Configure and test the offline tree.

   ```bash
   cmake -S . -B build/demo -DCMAKE_BUILD_TYPE=Debug
   cmake --build build/demo --parallel
   ctest --test-dir build/demo --output-on-failure
   ```

2. Generate replay, execution, risk, and benchmark artifacts.

   ```bash
   ./build/demo/aegisx replay --input data/fixtures/itch_reference_segment.itch --output runs/demo
   ./build/demo/aegisx simulate --input data/fixtures/aegisx_itch_sample.itch --output runs/demo
   ./build/demo/aegisx risk-demo --output runs/demo
   ./build/demo/aegisx benchmark --input data/fixtures/itch_reference_segment.itch --output runs/demo
   ```

3. Open `PYTHONPATH=python streamlit run dashboard/app.py` and select `runs/demo`.

4. On a larger simulator-labelled run with both passive-fill classes, run `PYTHONPATH=python python python/scripts/run_research.py --run runs/demo`.

The final command intentionally refuses an inadequate dataset; that is expected for the tiny committed fixtures.
