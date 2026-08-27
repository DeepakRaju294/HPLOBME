# Custom benchmark harness (spec section 25 permits "Google Benchmark or
# custom harness"). A custom harness was chosen because the required
# measurements -- per-operation latency percentiles (p50/p95/p99/p99.9)
# and blended mixed-workload streams with weighted operation types -- map
# more directly onto manual timing of pre-generated command sequences than
# onto Google Benchmark's per-repetition statistics model. See
# docs/benchmark_methodology.md.
add_executable(lob_benchmarks
  main.cpp
  workload.cpp
  stats.cpp
)
target_link_libraries(lob_benchmarks PRIVATE lob_core)
