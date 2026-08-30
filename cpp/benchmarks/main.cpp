#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "matching_engine.hpp"
#include "stats.hpp"
#include "workload.hpp"

using namespace lob;
using namespace lob::bench;

namespace {

constexpr int kWarmupRepetitions = 1;
constexpr int kMeasuredRepetitions = 5;

// DenseMatchingEngine needs an explicit [min_price, max_price]. Generous
// enough to cover every price any workload generator produces (the
// widest case is single_match/multi_level_sweep seeding one resting
// order per tick up to the 100,000-order target, plus the mixed
// workload's sacrificial-pool offset) -- see workload.cpp.
constexpr Price kDenseRangeMargin = 150'000;

template <typename EngineType>
EngineType make_engine() {
    if constexpr (std::is_same_v<EngineType, DenseMatchingEngine>) {
        return DenseMatchingEngine(kMidPrice - kDenseRangeMargin, kMidPrice + kDenseRangeMargin);
    } else {
        return EngineType{};
    }
}

template <typename EngineType>
void apply_ops_untimed(EngineType& engine, const std::vector<WorkloadOp>& ops) {
    for (const auto& op : ops) {
        switch (op.kind) {
            case OpKind::Submit:
                engine.submit(op.new_order);
                break;
            case OpKind::Cancel:
                engine.cancel(op.cancel_order);
                break;
            case OpKind::Replace:
                engine.replace(op.replace_order);
                break;
        }
        engine.drain_market_data(); // keep the queue from growing unbounded during seeding
    }
}

// Runs `workload.timed_ops` through a freshly-seeded engine, timing each
// operation individually with a monotonic clock. Warm-up repetitions are
// discarded; measured repetitions' per-op latencies are pooled together
// before computing percentiles, so p99.9 has enough samples behind it to
// mean something (spec section 23: warm up, multiple repetitions).
template <typename EngineType>
BenchmarkResult run_benchmark(const std::string& operation, const std::string& workload_config,
                               const Workload& workload) {
    std::vector<std::uint64_t> pooled_latencies_ns;
    pooled_latencies_ns.reserve(workload.timed_ops.size() * static_cast<std::size_t>(kMeasuredRepetitions));

    std::size_t final_active_orders = 0;
    std::size_t final_price_levels = 0;
    double total_elapsed_seconds = 0.0;

    for (int rep = 0; rep < kWarmupRepetitions + kMeasuredRepetitions; ++rep) {
        EngineType engine = make_engine<EngineType>();
        apply_ops_untimed(engine, workload.seed_ops);

        const bool measured = rep >= kWarmupRepetitions;
        std::vector<std::uint64_t> rep_latencies_ns;
        if (measured) {
            rep_latencies_ns.reserve(workload.timed_ops.size());
        }

        const auto rep_start = std::chrono::steady_clock::now();
        for (const auto& op : workload.timed_ops) {
            const auto op_start = std::chrono::steady_clock::now();
            switch (op.kind) {
                case OpKind::Submit:
                    engine.submit(op.new_order);
                    break;
                case OpKind::Cancel:
                    engine.cancel(op.cancel_order);
                    break;
                case OpKind::Replace:
                    engine.replace(op.replace_order);
                    break;
            }
            const auto op_end = std::chrono::steady_clock::now();
            if (measured) {
                rep_latencies_ns.push_back(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(op_end - op_start).count()));
            }
        }
        const auto rep_end = std::chrono::steady_clock::now();

        if (measured) {
            total_elapsed_seconds += std::chrono::duration<double>(rep_end - rep_start).count();
            pooled_latencies_ns.insert(pooled_latencies_ns.end(), rep_latencies_ns.begin(), rep_latencies_ns.end());
        }

        if (rep == kWarmupRepetitions + kMeasuredRepetitions - 1) {
            final_active_orders = engine.active_order_count();
            const auto snap = engine.snapshot(1'000'000);
            final_price_levels = snap.bids.size() + snap.asks.size();
        }
    }

    BenchmarkResult result;
    result.operation = operation;
    result.workload_config = workload_config;
    result.active_order_target = workload.target_active_orders;
    result.active_orders_actual = final_active_orders;
    result.price_levels = final_price_levels;
    result.stats = compute_latency_stats(pooled_latencies_ns, total_elapsed_seconds);
    return result;
}

// The 27-configuration suite (spec section 22): 6 required operations in
// isolation x 3 active-order targets, plus mixed-event-stream x 3
// required workload mixes x 3 targets. Identical for every engine type --
// only the template parameter changes which book representation is under
// test.
template <typename EngineType>
std::vector<BenchmarkResult> run_full_suite() {
    const std::vector<std::size_t> active_order_targets{1'000, 10'000, 100'000};
    constexpr std::uint64_t kSeed = 42; // deterministic: same seed -> same workload every run
    constexpr std::size_t kOpCount = 2000;

    std::vector<BenchmarkResult> results;

    for (std::size_t target : active_order_targets) {
        results.push_back(run_benchmark<EngineType>("add_existing_level", "n/a",
                                                      generate_add_existing_level_workload(target, kOpCount, kSeed)));
        results.push_back(run_benchmark<EngineType>("add_new_level", "n/a",
                                                      generate_add_new_level_workload(target, kOpCount, kSeed)));
        results.push_back(
            run_benchmark<EngineType>("cancel", "n/a", generate_cancel_workload(target, kOpCount, kSeed)));
        results.push_back(
            run_benchmark<EngineType>("replace", "n/a", generate_replace_workload(target, kOpCount, kSeed)));
        results.push_back(run_benchmark<EngineType>("single_match", "n/a",
                                                      generate_single_match_workload(target, kOpCount, kSeed)));
        results.push_back(run_benchmark<EngineType>(
            "multi_level_sweep", "n/a", generate_multi_level_sweep_workload(target, kOpCount, 10, kSeed)));
    }

    for (std::size_t target : active_order_targets) {
        for (const auto& mix : {kPassiveHeavy, kBalanced, kMatchHeavy}) {
            results.push_back(run_benchmark<EngineType>("mixed_event_stream", mix.name,
                                                          generate_mixed_workload(mix, target, kOpCount, kSeed)));
        }
    }

    return results;
}

} // namespace

// Runs the baseline benchmark suite against both book representations
// (spec section 8.2 requires comparing them). Must be run from the repo
// root so the "results/*.csv" relative paths resolve correctly -- see
// scripts/run_benchmarks.sh.
int main() {
    print_environment_info();

    std::cout << "\n=== MatchingEngine (std::map price levels + pooled order storage) ===\n\n";
    auto map_results = run_full_suite<MatchingEngine>();
    print_report(map_results);
    write_csv("results/optimized_map_pooled_benchmark.csv", map_results);
    std::cout << "\nWrote results/optimized_map_pooled_benchmark.csv\n";

    std::cout << "\n=== DenseMatchingEngine (dense tick-indexed price levels + pooled order storage) ===\n\n";
    auto dense_results = run_full_suite<DenseMatchingEngine>();
    print_report(dense_results);
    write_csv("results/optimized_dense_pooled_benchmark.csv", dense_results);
    std::cout << "\nWrote results/optimized_dense_pooled_benchmark.csv\n";

    std::cout << "\nSee results/baseline_benchmark.csv (Milestone 4: std::map, no pooling) for the\n"
                 "pre-optimization reference point, and docs/performance_analysis.md for the\n"
                 "full baseline -> profile -> optimize -> measure comparison.\n";
    return 0;
}
