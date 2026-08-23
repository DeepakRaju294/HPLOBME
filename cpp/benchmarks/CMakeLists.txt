include(FetchContent)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.0
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(lob_benchmarks
  sanity_benchmark.cpp
)
target_link_libraries(lob_benchmarks PRIVATE lob_core benchmark::benchmark)
