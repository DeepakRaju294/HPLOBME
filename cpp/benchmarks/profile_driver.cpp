// Dedicated profiling entry point (Milestone 5, spec section 24 Phase 2).
//
// Deliberately separate from lob_benchmarks: that harness sweeps many
// configurations x repetitions, which is far too much to run under
// valgrind's callgrind/cachegrind instrumentation (10-50x overhead). This
// runs ONE representative, sizable workload exactly once -- no internal
// timing needed, since valgrind does the measurement externally.
//
// Usage: build in Release (still with debug symbols) and run under:
//   valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./lob_profile_driver [workload] [engine]
//   valgrind --tool=cachegrind --cachegrind-out-file=cachegrind.out ./lob_profile_driver [workload] [engine]
//
// workload: "mixed" (default) or "sweep" -- the latter reproduces the
//           single_match/multi_level_sweep benchmark shape, used to
//           understand why DenseMatchingEngine measured slower than
//           MatchingEngine there (see docs/performance_analysis.md).
// engine:   "map" (default, MatchingEngine) or "dense" (DenseMatchingEngine)
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>

#include "matching_engine.hpp"
#include "workload.hpp"

using namespace lob;
using namespace lob::bench;

namespace {

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
void run(const Workload& workload) {
    EngineType engine = make_engine<EngineType>();

    for (const auto& op : workload.seed_ops) {
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
        engine.drain_market_data();
    }

    // The profiled region: exactly the operations a benchmark/profile run
    // should attribute cost to.
    for (const auto& op : workload.timed_ops) {
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
        engine.drain_market_data();
    }

    // Force use of the result so nothing gets optimized away, and give a
    // sanity signal that the run actually did something.
    std::cout << "active_orders=" << engine.active_order_count() << " state_hash=" << engine.state_hash() << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const std::string workload_name = (argc > 1) ? argv[1] : "mixed";
    const std::string engine_name = (argc > 2) ? argv[2] : "map";

    constexpr std::size_t kTargetActiveOrders = 50'000;
    constexpr std::size_t kTimedOpCount = 20'000;
    constexpr std::uint64_t kSeed = 42;

    Workload workload;
    if (workload_name == "sweep") {
        // Reproduces the multi_level_sweep benchmark shape: one resting
        // order per tick, swept 10 levels at a time.
        workload = generate_multi_level_sweep_workload(kTargetActiveOrders, kTimedOpCount, 10, kSeed);
    } else {
        // match_heavy exercises matching (tree traversal / dense-array
        // scanning, order removal, book mutation) the most heavily of the
        // three required mixes.
        workload = generate_mixed_workload(kMatchHeavy, kTargetActiveOrders, kTimedOpCount, kSeed);
    }

    if (engine_name == "dense") {
        run<DenseMatchingEngine>(workload);
    } else {
        run<MatchingEngine>(workload);
    }
    return 0;
}
