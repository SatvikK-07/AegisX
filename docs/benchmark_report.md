# Benchmark report

The benchmark command records parser and replay wall-clock durations for one named input. It is a smoke benchmark, not a capacity study: the committed reference segment is only 348 bytes and contains 11 framed messages (10 decoded and one skipped).

On the local GCC 15 Debug validation run, the command reported 85,000 ns parser time and 50,000 ns replay time for that fixture. These figures are highly sensitive to machine, build type, filesystem cache, timer resolution, and the tiny sample size. They must not be compared to production-feed throughput or quoted as a latency guarantee.

Use `aegisx benchmark --input INPUT --output DIR` with a licensed, representative dataset and record compiler, build type, hardware, source checksum, configuration, repetitions, and distributional statistics before drawing any performance conclusion.
