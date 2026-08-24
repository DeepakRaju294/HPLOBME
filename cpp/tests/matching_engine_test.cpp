#include <gtest/gtest.h>

#include <random>
#include <variant>
#include <vector>

#include "matching_engine.hpp"

using namespace lob;

namespace {

NewOrder make_new_order(OrderId id, Side side, Price price, Quantity qty,
                         TimeInForce tif = TimeInForce::GoodTillCancel,
                         OrderType type = OrderType::Limit,
                         Timestamp ts = 0) {
    NewOrder cmd{};
    cmd.id = id;
    cmd.side = side;
    cmd.type = type;
    cmd.time_in_force = tif;
    cmd.price = price;
    cmd.quantity = qty;
    cmd.timestamp = ts;
    return cmd;
}

template <typename T>
const T* event_as(const EngineEvent& event) {
    return std::get_if<T>(&event);
}

int count_trades(const std::vector<EngineEvent>& events) {
    int count = 0;
    for (const auto& event : events) {
        if (event_as<TradeExecuted>(event) != nullptr) {
            ++count;
        }
    }
    return count;
}

} // namespace

// --- Matching ---

TEST(MatchingEngine, FullFillAgainstSingleRestingOrder) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 10));

    auto events = engine.submit(make_new_order(2, Side::Buy, 60, 10));

    // Accepted, TradeExecuted, OrderFilled(maker), OrderFilled(taker).
    ASSERT_EQ(events.size(), 4u);
    ASSERT_TRUE(event_as<OrderAccepted>(events[0]));
    const auto* trade = event_as<TradeExecuted>(events[1]);
    ASSERT_TRUE(trade);
    EXPECT_EQ(trade->trade.quantity, 10u);
    EXPECT_EQ(trade->trade.price, 50); // executes at the resting (maker) price
    ASSERT_TRUE(event_as<OrderFilled>(events[2])); // maker (order 1)
    ASSERT_TRUE(event_as<OrderFilled>(events[3])); // taker (order 2)
    EXPECT_EQ(engine.best_bid().has_value(), false);
    EXPECT_EQ(engine.best_ask().has_value(), false);
    EXPECT_TRUE(engine.validate_invariants());
}

TEST(MatchingEngine, PartialFillLeavesResidualOnMaker) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 10));

    auto events = engine.submit(make_new_order(2, Side::Buy, 60, 4));

    bool saw_maker_partial = false;
    for (const auto& event : events) {
        if (const auto* pf = event_as<OrderPartiallyFilled>(event)) {
            if (pf->order_id == 1) {
                saw_maker_partial = true;
                EXPECT_EQ(pf->filled_quantity, 4u);
                EXPECT_EQ(pf->remaining_quantity, 6u);
            }
        }
    }
    EXPECT_TRUE(saw_maker_partial);
    EXPECT_EQ(engine.quantity_at_price(Side::Sell, 50), 6u);
    EXPECT_TRUE(engine.validate_invariants());
}

TEST(MatchingEngine, MultiOrderSweepRespectsFifoWithinLevel) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 5));
    engine.submit(make_new_order(2, Side::Sell, 50, 5));
    engine.submit(make_new_order(3, Side::Sell, 50, 5));

    auto events = engine.submit(make_new_order(4, Side::Buy, 60, 12));

    std::vector<OrderId> makers_hit;
    for (const auto& event : events) {
        if (const auto* trade = event_as<TradeExecuted>(event)) {
            makers_hit.push_back(trade->trade.resting_order_id);
        }
    }
    ASSERT_EQ(makers_hit.size(), 3u);
    EXPECT_EQ(makers_hit, (std::vector<OrderId>{1, 2, 3}));
    EXPECT_EQ(engine.quantity_at_price(Side::Sell, 50), 3u); // order 3 left with 2 of 5
    EXPECT_TRUE(engine.validate_invariants());
}

TEST(MatchingEngine, MultiLevelSweepConsumesBestPricesFirst) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 5));
    engine.submit(make_new_order(2, Side::Sell, 51, 5));
    engine.submit(make_new_order(3, Side::Sell, 52, 5));

    auto events = engine.submit(make_new_order(4, Side::Buy, 100, 15));

    std::vector<Price> prices_hit;
    for (const auto& event : events) {
        if (const auto* trade = event_as<TradeExecuted>(event)) {
            prices_hit.push_back(trade->trade.price);
        }
    }
    EXPECT_EQ(prices_hit, (std::vector<Price>{50, 51, 52}));
    EXPECT_TRUE(engine.validate_invariants());
}

TEST(MatchingEngine, TradeExecutesAtMakerPriceNotTakerLimit) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 105, 10)); // resting bid

    auto events = engine.submit(make_new_order(2, Side::Sell, 95, 10)); // aggressive sell

    const auto* trade = event_as<TradeExecuted>(events[1]);
    ASSERT_TRUE(trade);
    EXPECT_EQ(trade->trade.price, 105); // maker's price, not the taker's 95 limit
}

// --- Lifecycle ---

TEST(MatchingEngine, CancelUnknownIdIsRejected) {
    MatchingEngine engine;
    CancelOrder cmd{};
    cmd.id = 999;
    auto events = engine.cancel(cmd);
    ASSERT_EQ(events.size(), 1u);
    const auto* rejected = event_as<OrderRejected>(events[0]);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->reason, RejectReason::UnknownOrderId);
}

TEST(MatchingEngine, RepeatedCancelRejectsSecondAttempt) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));

    CancelOrder cmd{};
    cmd.id = 1;
    auto first = engine.cancel(cmd);
    ASSERT_TRUE(event_as<OrderCancelled>(first[0]));

    auto second = engine.cancel(cmd);
    ASSERT_TRUE(event_as<OrderRejected>(second[0]));
}

TEST(MatchingEngine, PriorityPreservingReplaceKeepsFifoPosition) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    engine.submit(make_new_order(2, Side::Buy, 50, 20));

    ReplaceOrder replace_cmd{};
    replace_cmd.id = 1;
    replace_cmd.new_price = 50;
    replace_cmd.new_quantity = 5;
    auto replace_events = engine.replace(replace_cmd);
    ASSERT_EQ(replace_events.size(), 1u);
    const auto* replaced = event_as<OrderReplaced>(replace_events[0]);
    ASSERT_TRUE(replaced);
    EXPECT_TRUE(replaced->priority_preserved);

    // Order 1 must still be ahead of order 2 despite order 2 having more
    // quantity: an aggressive sell for exactly order 1's new size should
    // match order 1, not order 2.
    auto sweep = engine.submit(make_new_order(3, Side::Sell, 50, 5, TimeInForce::ImmediateOrCancel));
    const auto* trade = event_as<TradeExecuted>(sweep[1]);
    ASSERT_TRUE(trade);
    EXPECT_EQ(trade->trade.resting_order_id, 1u);
    EXPECT_TRUE(engine.validate_invariants());
}

TEST(MatchingEngine, PriorityLosingReplaceOnQuantityIncreaseMovesToBack) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    engine.submit(make_new_order(2, Side::Buy, 50, 5));

    ReplaceOrder replace_cmd{};
    replace_cmd.id = 1;
    replace_cmd.new_price = 50; // unchanged
    replace_cmd.new_quantity = 15; // increased -> loses priority
    auto replace_events = engine.replace(replace_cmd);
    const auto* replaced = event_as<OrderReplaced>(replace_events[0]);
    ASSERT_TRUE(replaced);
    EXPECT_FALSE(replaced->priority_preserved);

    // Order 2 now has priority over order 1.
    auto sweep = engine.submit(make_new_order(3, Side::Sell, 50, 5, TimeInForce::ImmediateOrCancel));
    const auto* trade = event_as<TradeExecuted>(sweep[1]);
    ASSERT_TRUE(trade);
    EXPECT_EQ(trade->trade.resting_order_id, 2u);
    EXPECT_TRUE(engine.validate_invariants());
}

TEST(MatchingEngine, CrossingReplaceExecutesImmediately) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    engine.submit(make_new_order(2, Side::Sell, 70, 10));

    ReplaceOrder replace_cmd{};
    replace_cmd.id = 1;
    replace_cmd.new_price = 80; // now crosses the resting ask at 70
    replace_cmd.new_quantity = 10;
    auto events = engine.replace(replace_cmd);

    ASSERT_TRUE(event_as<OrderReplaced>(events[0]));
    ASSERT_EQ(count_trades(events), 1);
    const auto* trade = event_as<TradeExecuted>(events[1]);
    ASSERT_TRUE(trade);
    EXPECT_EQ(trade->trade.price, 70); // maker's price
    EXPECT_TRUE(event_as<OrderFilled>(events.back()));
    EXPECT_TRUE(engine.validate_invariants());
}

TEST(MatchingEngine, ReplaceAfterFillIsRejectedAsUnknown) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 5));
    engine.submit(make_new_order(2, Side::Sell, 50, 5, TimeInForce::ImmediateOrCancel)); // fully fills & removes order 1

    ReplaceOrder replace_cmd{};
    replace_cmd.id = 1;
    replace_cmd.new_price = 60;
    replace_cmd.new_quantity = 10;
    auto events = engine.replace(replace_cmd);
    ASSERT_EQ(events.size(), 1u);
    const auto* rejected = event_as<OrderRejected>(events[0]);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->reason, RejectReason::UnknownOrderId);
}

// --- Time in force ---

TEST(MatchingEngine, IocCancelsUnfilledResidual) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 5));

    auto events = engine.submit(make_new_order(2, Side::Buy, 60, 10, TimeInForce::ImmediateOrCancel));

    ASSERT_TRUE(event_as<OrderAccepted>(events[0]));
    ASSERT_TRUE(event_as<TradeExecuted>(events[1]));
    const auto* partial = event_as<OrderPartiallyFilled>(events[3]);
    ASSERT_TRUE(partial);
    EXPECT_EQ(partial->filled_quantity, 5u);
    ASSERT_TRUE(event_as<OrderCancelled>(events[4]));
    EXPECT_FALSE(engine.best_bid().has_value()); // never rested
}

TEST(MatchingEngine, IocAgainstEmptyBookCancelsImmediately) {
    MatchingEngine engine;
    auto events = engine.submit(make_new_order(1, Side::Buy, 50, 10, TimeInForce::ImmediateOrCancel));
    ASSERT_EQ(events.size(), 2u); // Accepted, Cancelled -- no fill occurred
    ASSERT_TRUE(event_as<OrderAccepted>(events[0]));
    ASSERT_TRUE(event_as<OrderCancelled>(events[1]));
}

TEST(MatchingEngine, FokRejectsWhenNotFullyFillable) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 5));

    auto events = engine.submit(make_new_order(2, Side::Buy, 60, 10, TimeInForce::FillOrKill));

    ASSERT_EQ(events.size(), 1u);
    const auto* rejected = event_as<OrderRejected>(events[0]);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->reason, RejectReason::FillOrKillUnfillable);
    // Book must be untouched: no partial execution occurred.
    EXPECT_EQ(engine.quantity_at_price(Side::Sell, 50), 5u);
}

TEST(MatchingEngine, FokExecutesFullyWhenFillable) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 10));

    auto events = engine.submit(make_new_order(2, Side::Buy, 60, 10, TimeInForce::FillOrKill));

    ASSERT_TRUE(event_as<OrderAccepted>(events[0]));
    ASSERT_TRUE(event_as<TradeExecuted>(events[1]));
    ASSERT_TRUE(event_as<OrderFilled>(events.back()));
}

TEST(MatchingEngine, PostOnlyRejectsWhenItWouldCross) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 10));

    auto events = engine.submit(make_new_order(2, Side::Buy, 60, 10, TimeInForce::PostOnly));

    ASSERT_EQ(events.size(), 1u);
    const auto* rejected = event_as<OrderRejected>(events[0]);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->reason, RejectReason::WouldCross);
}

TEST(MatchingEngine, PostOnlyRestsWhenItWouldNotCross) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 10));

    auto events = engine.submit(make_new_order(2, Side::Buy, 40, 10, TimeInForce::PostOnly));

    ASSERT_EQ(events.size(), 2u);
    ASSERT_TRUE(event_as<OrderAccepted>(events[0]));
    ASSERT_TRUE(event_as<OrderRested>(events[1]));
    EXPECT_EQ(engine.best_bid(), 40);
}

// --- Market orders ---

TEST(MatchingEngine, MarketOrderSweepsMultipleLevels) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 5));
    engine.submit(make_new_order(2, Side::Sell, 51, 5));

    auto events = engine.submit(make_new_order(3, Side::Buy, 0, 10, TimeInForce::ImmediateOrCancel, OrderType::Market));

    EXPECT_EQ(count_trades(events), 2);
    ASSERT_TRUE(event_as<OrderFilled>(events.back()));
    EXPECT_TRUE(engine.validate_invariants());
}

TEST(MatchingEngine, MarketOrderCancelsResidualWhenBookExhausted) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 5));

    auto events = engine.submit(make_new_order(2, Side::Buy, 0, 10, TimeInForce::ImmediateOrCancel, OrderType::Market));

    EXPECT_EQ(count_trades(events), 1);
    ASSERT_TRUE(event_as<OrderCancelled>(events.back()));
}

TEST(MatchingEngine, MarketFokRequiresTotalLiquidityAcrossAllPrices) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 5));

    auto events = engine.submit(make_new_order(2, Side::Buy, 0, 10, TimeInForce::FillOrKill, OrderType::Market));

    ASSERT_EQ(events.size(), 1u);
    const auto* rejected = event_as<OrderRejected>(events[0]);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->reason, RejectReason::FillOrKillUnfillable);
}

TEST(MatchingEngine, MarketOrderRejectsGoodTillCancel) {
    MatchingEngine engine;
    auto events = engine.submit(make_new_order(1, Side::Buy, 0, 10, TimeInForce::GoodTillCancel, OrderType::Market));
    ASSERT_EQ(events.size(), 1u);
    const auto* rejected = event_as<OrderRejected>(events[0]);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->reason, RejectReason::InvalidTimeInForceForOrderType);
}

TEST(MatchingEngine, MarketOrderRejectsPostOnly) {
    MatchingEngine engine;
    auto events = engine.submit(make_new_order(1, Side::Buy, 0, 10, TimeInForce::PostOnly, OrderType::Market));
    ASSERT_EQ(events.size(), 1u);
    const auto* rejected = event_as<OrderRejected>(events[0]);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->reason, RejectReason::InvalidTimeInForceForOrderType);
}

// --- Edge cases ---

TEST(MatchingEngine, DuplicateOrderIdIsRejected) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    auto events = engine.submit(make_new_order(1, Side::Sell, 60, 5));
    ASSERT_EQ(events.size(), 1u);
    const auto* rejected = event_as<OrderRejected>(events[0]);
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->reason, RejectReason::DuplicateOrderId);
}

TEST(MatchingEngine, ZeroQuantityIsRejected) {
    MatchingEngine engine;
    auto events = engine.submit(make_new_order(1, Side::Buy, 50, 0));
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(event_as<OrderRejected>(events[0])->reason, RejectReason::ZeroQuantity);
}

TEST(MatchingEngine, NonPositivePriceIsRejected) {
    MatchingEngine engine;
    auto zero_price = engine.submit(make_new_order(1, Side::Buy, 0, 10));
    EXPECT_EQ(event_as<OrderRejected>(zero_price[0])->reason, RejectReason::InvalidPrice);

    auto negative_price = engine.submit(make_new_order(2, Side::Buy, -5, 10));
    EXPECT_EQ(event_as<OrderRejected>(negative_price[0])->reason, RejectReason::InvalidPrice);
}

TEST(MatchingEngine, LargeQuantitiesMatchCorrectly) {
    MatchingEngine engine;
    const Quantity huge = 1'000'000'000ull;
    engine.submit(make_new_order(1, Side::Sell, 50, huge));
    auto events = engine.submit(make_new_order(2, Side::Buy, 50, huge));

    const auto* trade = event_as<TradeExecuted>(events[1]);
    ASSERT_TRUE(trade);
    EXPECT_EQ(trade->trade.quantity, huge);
    EXPECT_TRUE(engine.validate_invariants());
}

// --- Randomized invariant preservation across the full command set ---

TEST(MatchingEngine, RandomizedCommandSequencePreservesInvariants) {
    MatchingEngine engine;
    std::mt19937 rng(9001); // fixed seed: deterministic per spec section 20
    std::vector<OrderId> submitted_ids;
    OrderId next_id = 1;

    for (int step = 0; step < 3000; ++step) {
        const int choice = static_cast<int>(rng() % 10);

        if (choice < 6 || submitted_ids.empty()) {
            const bool is_market = (rng() % 5 == 0);
            const Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            const Price price = 90 + static_cast<Price>(rng() % 20);
            const Quantity qty = 1 + (rng() % 20);

            TimeInForce tif;
            if (is_market) {
                tif = (rng() % 2 == 0) ? TimeInForce::ImmediateOrCancel : TimeInForce::FillOrKill;
            } else {
                switch (rng() % 4) {
                    case 0: tif = TimeInForce::GoodTillCancel; break;
                    case 1: tif = TimeInForce::ImmediateOrCancel; break;
                    case 2: tif = TimeInForce::FillOrKill; break;
                    default: tif = TimeInForce::PostOnly; break;
                }
            }

            const OrderId id = next_id++;
            auto cmd = make_new_order(id, side, price, qty, tif,
                                       is_market ? OrderType::Market : OrderType::Limit,
                                       static_cast<Timestamp>(step));
            auto events = engine.submit(cmd);
            ASSERT_FALSE(events.empty());
            submitted_ids.push_back(id);
        } else if (choice < 8) {
            CancelOrder cmd{};
            cmd.id = submitted_ids[rng() % submitted_ids.size()];
            cmd.timestamp = static_cast<Timestamp>(step);
            auto events = engine.cancel(cmd);
            ASSERT_FALSE(events.empty());
        } else {
            ReplaceOrder cmd{};
            cmd.id = submitted_ids[rng() % submitted_ids.size()];
            cmd.new_price = 90 + static_cast<Price>(rng() % 20);
            cmd.new_quantity = 1 + (rng() % 20);
            cmd.timestamp = static_cast<Timestamp>(step);
            auto events = engine.replace(cmd);
            ASSERT_FALSE(events.empty());
        }

        ASSERT_TRUE(engine.validate_invariants()) << "invariant violated at step " << step;
    }
}
