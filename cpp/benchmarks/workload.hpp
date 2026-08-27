#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "order.hpp"

namespace lob::bench {

enum class OpKind { Submit, Cancel, Replace };

struct WorkloadOp {
    OpKind kind{};
    NewOrder new_order{};
    CancelOrder cancel_order{};
    ReplaceOrder replace_order{};
};

struct WorkloadMix {
    std::string name;
    double add_fraction;
    double cancel_fraction;
    double aggressive_fraction;
};

extern const WorkloadMix kPassiveHeavy;
extern const WorkloadMix kBalanced;
extern const WorkloadMix kMatchHeavy;

// `seed_ops` are applied to a fresh engine untimed, to build up realistic
// book state; `timed_ops` are the ones actually measured. Both are fully
// pre-generated (deterministic, given `seed`) before any timing starts,
// per spec section 23 ("avoid allocation in event generation during
// timed sections").
struct Workload {
    std::vector<WorkloadOp> seed_ops;
    std::vector<WorkloadOp> timed_ops;
    std::size_t target_active_orders{};
    std::size_t price_levels_per_side{};
};

// --- Required operations (spec section 22) ---

Workload generate_add_existing_level_workload(std::size_t target_active_orders, std::size_t timed_op_count,
                                                std::uint64_t seed);
Workload generate_add_new_level_workload(std::size_t target_active_orders, std::size_t timed_op_count,
                                          std::uint64_t seed);
Workload generate_cancel_workload(std::size_t target_active_orders, std::size_t timed_op_count, std::uint64_t seed);
Workload generate_replace_workload(std::size_t target_active_orders, std::size_t timed_op_count, std::uint64_t seed);
Workload generate_single_match_workload(std::size_t target_active_orders, std::size_t timed_op_count,
                                         std::uint64_t seed);
Workload generate_multi_level_sweep_workload(std::size_t target_active_orders, std::size_t timed_op_count,
                                              std::size_t levels_per_sweep, std::uint64_t seed);

// --- Blended workload (spec section 22 "Workloads") ---

Workload generate_mixed_workload(const WorkloadMix& mix, std::size_t target_active_orders,
                                  std::size_t timed_op_count, std::uint64_t seed);

} // namespace lob::bench
