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

# Separate, single-pass driver for external profilers (valgrind
# callgrind/cachegrind) -- see profile_driver.cpp for why this isn't just
# lob_benchmarks. Always built with debug info, even in Release, so
# profiler output has symbol names.
add_executable(lob_profile_driver
  profile_driver.cpp
  workload.cpp
)
target_link_libraries(lob_profile_driver PRIVATE lob_core)
target_compile_options(lob_profile_driver PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-g>)
