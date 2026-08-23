#include <benchmark/benchmark.h>

#include "order.hpp"

// Placeholder to confirm the benchmark toolchain wires up end to end.
// Real operation benchmarks (add/cancel/replace/match, mixed workloads,
// percentile reporting) are added starting Milestone 4.
static void BM_Sanity(benchmark::State& state) {
    for (auto _ : state) {
        lob::Order order{};
        order.quantity = 100;
        benchmark::DoNotOptimize(order);
    }
}
BENCHMARK(BM_Sanity);

BENCHMARK_MAIN();
