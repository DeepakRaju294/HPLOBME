#include "workload.hpp"

#include <algorithm>
#include <random>

namespace lob::bench {

const WorkloadMix kPassiveHeavy{"passive_heavy(70/25/5)", 0.70, 0.25, 0.05};
const WorkloadMix kBalanced{"balanced(50/30/20)", 0.50, 0.30, 0.20};
const WorkloadMix kMatchHeavy{"match_heavy(40/20/40)", 0.40, 0.20, 0.40};

namespace {

NewOrder make_new_order(OrderId id, Side side, Price price, Quantity qty, TimeInForce tif, Timestamp ts) {
    NewOrder cmd{};
    cmd.id = id;
    cmd.side = side;
    cmd.type = OrderType::Limit;
    cmd.time_in_force = tif;
    cmd.price = price;
    cmd.quantity = qty;
    cmd.timestamp = ts;
    return cmd;
}

WorkloadOp submit_op(OrderId id, Side side, Price price, Quantity qty, TimeInForce tif, Timestamp ts) {
    WorkloadOp op{};
    op.kind = OpKind::Submit;
    op.new_order = make_new_order(id, side, price, qty, tif, ts);
    return op;
}

std::size_t levels_for_target(std::size_t target_active_orders) {
    // ~20 resting orders per price level on average -- dense enough that
    // "add at existing level" is the common case, sparse enough that many
    // distinct levels exist for realistic multi-level structure.
    return std::max<std::size_t>(10, target_active_orders / 20);
}

// Seeds `target_active_orders` GTC limit orders split evenly across bid
// and ask sides, each side spread across `price_levels_per_side` levels
// on its own disjoint (non-crossing) price range. Returns the seeded
// order IDs (for cancel/replace workloads) and advances `next_id`.
struct SeededBook {
    std::vector<WorkloadOp> seed_ops;
    std::vector<OrderId> seeded_ids;
    std::vector<Price> seeded_prices; // parallel to seeded_ids
    Price bid_low{};
    Price bid_high{};
    Price ask_low{};
    Price ask_high{};
};

// `inner_margin` shifts both sides' price range outward, leaving a gap of
// that many ticks immediately around kMidPrice free for a caller to seed
// something else (e.g. a separate "sacrificial" pool) without any price
// overlap between the two.
SeededBook seed_two_sided_book(std::size_t target_active_orders, std::size_t price_levels_per_side,
                                Price inner_margin, OrderId& next_id, Timestamp& next_ts) {
    SeededBook seeded;
    const std::size_t per_side = target_active_orders / 2;
    const std::size_t orders_per_level = std::max<std::size_t>(1, per_side / price_levels_per_side);

    seeded.bid_high = kMidPrice - 1 - inner_margin;
    seeded.bid_low = seeded.bid_high - static_cast<Price>(price_levels_per_side) + 1;
    seeded.ask_low = kMidPrice + 1 + inner_margin;
    seeded.ask_high = seeded.ask_low + static_cast<Price>(price_levels_per_side) - 1;

    for (std::size_t level = 0; level < price_levels_per_side; ++level) {
        const Price bid_price = seeded.bid_high - static_cast<Price>(level);
        const Price ask_price = seeded.ask_low + static_cast<Price>(level);
        for (std::size_t k = 0; k < orders_per_level; ++k) {
            const OrderId buy_id = next_id++;
            seeded.seed_ops.push_back(
                submit_op(buy_id, Side::Buy, bid_price, 10, TimeInForce::GoodTillCancel, next_ts++));
            seeded.seeded_ids.push_back(buy_id);
            seeded.seeded_prices.push_back(bid_price);

            const OrderId sell_id = next_id++;
            seeded.seed_ops.push_back(
                submit_op(sell_id, Side::Sell, ask_price, 10, TimeInForce::GoodTillCancel, next_ts++));
            seeded.seeded_ids.push_back(sell_id);
            seeded.seeded_prices.push_back(ask_price);
        }
    }
    return seeded;
}

} // namespace

Workload generate_add_existing_level_workload(std::size_t target_active_orders, std::size_t timed_op_count,
                                                std::uint64_t seed) {
    Workload workload;
    workload.target_active_orders = target_active_orders;
    workload.price_levels_per_side = levels_for_target(target_active_orders);

    OrderId next_id = 1;
    Timestamp next_ts = 0;
    SeededBook seeded = seed_two_sided_book(target_active_orders, workload.price_levels_per_side, 0, next_id, next_ts);
    workload.seed_ops = std::move(seeded.seed_ops);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<Price> bid_price_dist(seeded.bid_low, seeded.bid_high);
    std::uniform_int_distribution<Price> ask_price_dist(seeded.ask_low, seeded.ask_high);
    std::bernoulli_distribution side_dist(0.5);

    for (std::size_t i = 0; i < timed_op_count; ++i) {
        const bool is_buy = side_dist(rng);
        const Price price = is_buy ? bid_price_dist(rng) : ask_price_dist(rng);
        workload.timed_ops.push_back(submit_op(next_id++, is_buy ? Side::Buy : Side::Sell, price, 10,
                                                TimeInForce::GoodTillCancel, next_ts++));
    }
    return workload;
}

Workload generate_add_new_level_workload(std::size_t target_active_orders, std::size_t timed_op_count,
                                          std::uint64_t /*seed*/) {
    Workload workload;
    workload.target_active_orders = target_active_orders;
    workload.price_levels_per_side = levels_for_target(target_active_orders);

    OrderId next_id = 1;
    Timestamp next_ts = 0;
    SeededBook seeded = seed_two_sided_book(target_active_orders, workload.price_levels_per_side, 0, next_id, next_ts);
    workload.seed_ops = std::move(seeded.seed_ops);

    // Each timed op alternates side and steps one tick further outside the
    // seeded range, guaranteeing a brand-new price level every time.
    for (std::size_t i = 0; i < timed_op_count; ++i) {
        const bool is_buy = (i % 2 == 0);
        const Price price =
            is_buy ? (seeded.bid_low - 1 - static_cast<Price>(i / 2)) : (seeded.ask_high + 1 + static_cast<Price>(i / 2));
        workload.timed_ops.push_back(
            submit_op(next_id++, is_buy ? Side::Buy : Side::Sell, price, 10, TimeInForce::GoodTillCancel, next_ts++));
    }
    return workload;
}

Workload generate_cancel_workload(std::size_t target_active_orders, std::size_t timed_op_count, std::uint64_t seed) {
    Workload workload;
    workload.target_active_orders = target_active_orders;
    workload.price_levels_per_side = levels_for_target(target_active_orders);

    OrderId next_id = 1;
    Timestamp next_ts = 0;
    SeededBook seeded = seed_two_sided_book(target_active_orders, workload.price_levels_per_side, 0, next_id, next_ts);
    workload.seed_ops = std::move(seeded.seed_ops);

    // Never cancel more than a quarter of the seeded book, so the
    // reported book size stays representative of the target throughout
    // the timed section.
    const std::size_t max_cancels = std::max<std::size_t>(1, seeded.seeded_ids.size() / 4);
    timed_op_count = std::min(timed_op_count, max_cancels);

    std::mt19937_64 rng(seed);
    std::shuffle(seeded.seeded_ids.begin(), seeded.seeded_ids.end(), rng);

    for (std::size_t i = 0; i < timed_op_count; ++i) {
        WorkloadOp op{};
        op.kind = OpKind::Cancel;
        op.cancel_order.id = seeded.seeded_ids[i];
        op.cancel_order.timestamp = next_ts++;
        workload.timed_ops.push_back(op);
    }
    return workload;
}

Workload generate_replace_workload(std::size_t target_active_orders, std::size_t timed_op_count, std::uint64_t seed) {
    Workload workload;
    workload.target_active_orders = target_active_orders;
    workload.price_levels_per_side = levels_for_target(target_active_orders);

    OrderId next_id = 1;
    Timestamp next_ts = 0;
    SeededBook seeded = seed_two_sided_book(target_active_orders, workload.price_levels_per_side, 0, next_id, next_ts);
    workload.seed_ops = std::move(seeded.seed_ops);

    const std::size_t max_replaces = std::max<std::size_t>(1, seeded.seeded_ids.size() / 4);
    timed_op_count = std::min(timed_op_count, max_replaces);

    // Shuffle a parallel index permutation (rather than the id/price
    // vectors directly) so each chosen order keeps its correct price.
    std::vector<std::size_t> order_indices(seeded.seeded_ids.size());
    for (std::size_t i = 0; i < order_indices.size(); ++i) {
        order_indices[i] = i;
    }
    std::mt19937_64 rng(seed);
    std::shuffle(order_indices.begin(), order_indices.end(), rng);

    // Priority-preserving replace: same price, quantity reduced -- the
    // cheapest and most common real-world replace (shrinking an order).
    // Each seeded order was submitted with quantity 10 (see
    // seed_two_sided_book); replacing down to 5 is always a valid,
    // priority-preserving reduction.
    for (std::size_t i = 0; i < timed_op_count; ++i) {
        const std::size_t idx = order_indices[i];
        WorkloadOp op{};
        op.kind = OpKind::Replace;
        op.replace_order.id = seeded.seeded_ids[idx];
        op.replace_order.new_price = seeded.seeded_prices[idx];
        op.replace_order.new_quantity = 5;
        op.replace_order.timestamp = next_ts++;
        workload.timed_ops.push_back(op);
    }
    return workload;
}

Workload generate_single_match_workload(std::size_t target_active_orders, std::size_t timed_op_count,
                                         std::uint64_t /*seed*/) {
    Workload workload;
    workload.target_active_orders = target_active_orders;
    workload.price_levels_per_side = target_active_orders; // one resting order per level, sell side only

    OrderId next_id = 1;
    Timestamp next_ts = 0;

    // One sell order per price level, ascending from kMidPrice+1: each
    // aggressive buy below consumes exactly the current best (single)
    // level, then the next-best level becomes best for the next op.
    for (std::size_t level = 0; level < target_active_orders; ++level) {
        const Price ask_price = kMidPrice + 1 + static_cast<Price>(level);
        workload.seed_ops.push_back(
            submit_op(next_id++, Side::Sell, ask_price, 1, TimeInForce::GoodTillCancel, next_ts++));
    }

    const std::size_t max_matches = std::max<std::size_t>(1, target_active_orders / 4);
    timed_op_count = std::min(timed_op_count, max_matches);

    // Marketable limit: guaranteed to be >= the best ask at every step.
    const Price marketable_price = kMidPrice + static_cast<Price>(target_active_orders) + 1000;
    for (std::size_t i = 0; i < timed_op_count; ++i) {
        workload.timed_ops.push_back(
            submit_op(next_id++, Side::Buy, marketable_price, 1, TimeInForce::ImmediateOrCancel, next_ts++));
    }
    return workload;
}

Workload generate_multi_level_sweep_workload(std::size_t target_active_orders, std::size_t timed_op_count,
                                              std::size_t levels_per_sweep, std::uint64_t /*seed*/) {
    Workload workload;
    workload.target_active_orders = target_active_orders;
    workload.price_levels_per_side = target_active_orders;

    OrderId next_id = 1;
    Timestamp next_ts = 0;

    for (std::size_t level = 0; level < target_active_orders; ++level) {
        const Price ask_price = kMidPrice + 1 + static_cast<Price>(level);
        workload.seed_ops.push_back(
            submit_op(next_id++, Side::Sell, ask_price, 1, TimeInForce::GoodTillCancel, next_ts++));
    }

    levels_per_sweep = std::max<std::size_t>(2, levels_per_sweep);
    const std::size_t max_sweeps = std::max<std::size_t>(1, target_active_orders / (4 * levels_per_sweep));
    timed_op_count = std::min(timed_op_count, max_sweeps);

    const Price marketable_price = kMidPrice + static_cast<Price>(target_active_orders) + 1000;
    for (std::size_t i = 0; i < timed_op_count; ++i) {
        workload.timed_ops.push_back(submit_op(next_id++, Side::Buy, marketable_price,
                                                static_cast<Quantity>(levels_per_sweep), TimeInForce::ImmediateOrCancel,
                                                next_ts++));
    }
    return workload;
}

Workload generate_mixed_workload(const WorkloadMix& mix, std::size_t target_active_orders,
                                  std::size_t timed_op_count, std::uint64_t seed) {
    Workload workload;
    workload.target_active_orders = target_active_orders;
    workload.price_levels_per_side = levels_for_target(target_active_orders);

    OrderId next_id = 1;
    Timestamp next_ts = 0;

    // "Live" pool: the book the Add/Cancel ops act on, at the outer price
    // range. "Sacrificial" pool: extra liquidity seeded at the inner
    // (best) price range purely so aggressive ops always have something
    // valid to consume without the generator having to simulate the full
    // matching engine to track exactly what an aggressive fill removes.
    // Aggressive orders only ever cross into the sacrificial range, so
    // the live pool's IDs (the only ones Cancel ever targets) are never
    // invalidated by a match. The two pools' price ranges must not
    // overlap, so the sacrificial size is computed first and the live
    // pool is seeded with an inner_margin that pushes it strictly outside
    // that range. See docs/benchmark_methodology.md.
    const std::size_t expected_aggressive_ops =
        static_cast<std::size_t>(static_cast<double>(timed_op_count) * mix.aggressive_fraction) + 1;
    const Quantity max_aggressive_qty = 3;
    const std::size_t sacrificial_count_per_side = expected_aggressive_ops * max_aggressive_qty * 4 + 16;

    SeededBook live = seed_two_sided_book(target_active_orders, workload.price_levels_per_side,
                                           static_cast<Price>(sacrificial_count_per_side), next_id, next_ts);
    workload.seed_ops = std::move(live.seed_ops);
    std::vector<OrderId> live_ids = std::move(live.seeded_ids);

    const Price sacrificial_ask_low = kMidPrice + 1;
    const Price sacrificial_ask_high = sacrificial_ask_low + static_cast<Price>(sacrificial_count_per_side) - 1;
    const Price sacrificial_bid_high = kMidPrice - 1;
    const Price sacrificial_bid_low = sacrificial_bid_high - static_cast<Price>(sacrificial_count_per_side) + 1;

    for (std::size_t i = 0; i < sacrificial_count_per_side; ++i) {
        workload.seed_ops.push_back(submit_op(next_id++, Side::Sell, sacrificial_ask_low + static_cast<Price>(i), 1,
                                               TimeInForce::GoodTillCancel, next_ts++));
        workload.seed_ops.push_back(submit_op(next_id++, Side::Buy, sacrificial_bid_high - static_cast<Price>(i), 1,
                                               TimeInForce::GoodTillCancel, next_ts++));
    }

    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> unit_dist(0.0, 1.0);
    std::uniform_int_distribution<Price> live_bid_price_dist(live.bid_low, live.bid_high);
    std::uniform_int_distribution<Price> live_ask_price_dist(live.ask_low, live.ask_high);
    std::bernoulli_distribution side_dist(0.5);
    std::uniform_int_distribution<Quantity> aggressive_qty_dist(1, max_aggressive_qty);

    const double add_cut = mix.add_fraction;
    const double cancel_cut = mix.add_fraction + mix.cancel_fraction;

    for (std::size_t i = 0; i < timed_op_count; ++i) {
        const double draw = unit_dist(rng);

        if (draw < add_cut || live_ids.empty()) {
            const bool is_buy = side_dist(rng);
            const Price price = is_buy ? live_bid_price_dist(rng) : live_ask_price_dist(rng);
            const OrderId id = next_id++;
            workload.timed_ops.push_back(submit_op(id, is_buy ? Side::Buy : Side::Sell, price, 10,
                                                    TimeInForce::GoodTillCancel, next_ts++));
            live_ids.push_back(id);
        } else if (draw < cancel_cut) {
            std::uniform_int_distribution<std::size_t> pick(0, live_ids.size() - 1);
            const std::size_t idx = pick(rng);
            WorkloadOp op{};
            op.kind = OpKind::Cancel;
            op.cancel_order.id = live_ids[idx];
            op.cancel_order.timestamp = next_ts++;
            workload.timed_ops.push_back(op);
            live_ids[idx] = live_ids.back();
            live_ids.pop_back();
        } else {
            const bool is_buy = side_dist(rng);
            const Price price = is_buy ? (sacrificial_ask_high + 1000) : (sacrificial_bid_low - 1000);
            workload.timed_ops.push_back(submit_op(next_id++, is_buy ? Side::Buy : Side::Sell, price,
                                                    aggressive_qty_dist(rng), TimeInForce::ImmediateOrCancel,
                                                    next_ts++));
        }
    }
    return workload;
}

} // namespace lob::bench
