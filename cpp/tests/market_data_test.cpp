#include <gtest/gtest.h>

#include <variant>

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

} // namespace

TEST(MarketData, SequenceNumbersAreStrictlyIncreasingAcrossDrains) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    auto first_batch = engine.drain_market_data();
    engine.submit(make_new_order(2, Side::Buy, 51, 10));
    auto second_batch = engine.drain_market_data();

    ASSERT_FALSE(first_batch.empty());
    ASSERT_FALSE(second_batch.empty());

    SequenceNumber previous = 0;
    for (const auto& event : first_batch) {
        EXPECT_GT(event.sequence, previous);
        previous = event.sequence;
    }
    for (const auto& event : second_batch) {
        EXPECT_GT(event.sequence, previous);
        previous = event.sequence;
    }
}

TEST(MarketData, DrainClearsTheQueue) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    auto first = engine.drain_market_data();
    EXPECT_FALSE(first.empty());

    auto second = engine.drain_market_data();
    EXPECT_TRUE(second.empty());
}

TEST(MarketData, NewLevelProducesAddWithOrderQuantity) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    auto events = engine.drain_market_data();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, MarketDataEventType::Add);
    EXPECT_EQ(events[0].side, Side::Buy);
    EXPECT_EQ(events[0].price, 50);
    EXPECT_EQ(events[0].quantity, 10u);
}

TEST(MarketData, ExistingLevelGrowingProducesModifyWithAggregate) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    engine.drain_market_data();

    engine.submit(make_new_order(2, Side::Buy, 50, 5));
    auto events = engine.drain_market_data();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, MarketDataEventType::Modify);
    EXPECT_EQ(events[0].quantity, 15u); // level aggregate, not the new order's own qty
}

TEST(MarketData, FullLevelConsumptionProducesTradeThenDelete) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 10));
    engine.drain_market_data();

    engine.submit(make_new_order(2, Side::Buy, 60, 10));
    auto events = engine.drain_market_data();

    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].type, MarketDataEventType::Trade);
    EXPECT_EQ(events[0].price, 50);
    EXPECT_EQ(events[0].quantity, 10u);
    EXPECT_EQ(events[1].type, MarketDataEventType::Delete);
    EXPECT_EQ(events[1].price, 50);
}

TEST(MarketData, PartialConsumptionWithOtherOrdersRemainingProducesModifyNotDelete) {
    MatchingEngine engine;
    // Two resting sells at the same price; consuming the first in full
    // must not report Delete, since the level still holds the second.
    engine.submit(make_new_order(1, Side::Sell, 50, 5));
    engine.submit(make_new_order(2, Side::Sell, 50, 5));
    engine.drain_market_data();

    engine.submit(make_new_order(3, Side::Buy, 60, 5)); // fully consumes order 1 only
    auto events = engine.drain_market_data();

    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].type, MarketDataEventType::Trade);
    EXPECT_EQ(events[1].type, MarketDataEventType::Modify);
    EXPECT_EQ(events[1].quantity, 5u); // order 2 still resting
}

TEST(MarketData, CancelLastOrderAtLevelProducesDelete) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    engine.drain_market_data();

    CancelOrder cancel_cmd{};
    cancel_cmd.id = 1;
    engine.cancel(cancel_cmd);
    auto events = engine.drain_market_data();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, MarketDataEventType::Delete);
}

TEST(MarketData, CancelOneOfTwoOrdersAtLevelProducesModify) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    engine.submit(make_new_order(2, Side::Buy, 50, 5));
    engine.drain_market_data();

    CancelOrder cancel_cmd{};
    cancel_cmd.id = 1;
    engine.cancel(cancel_cmd);
    auto events = engine.drain_market_data();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, MarketDataEventType::Modify);
    EXPECT_EQ(events[0].quantity, 5u);
}

TEST(MarketData, PriorityPreservingReplaceProducesModify) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    engine.drain_market_data();

    ReplaceOrder replace_cmd{};
    replace_cmd.id = 1;
    replace_cmd.new_price = 50;
    replace_cmd.new_quantity = 4;
    engine.replace(replace_cmd);
    auto events = engine.drain_market_data();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, MarketDataEventType::Modify);
    EXPECT_EQ(events[0].quantity, 4u);
}

TEST(MarketData, RejectedCommandProducesNoMarketData) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Buy, 50, 0)); // zero quantity -> rejected
    EXPECT_TRUE(engine.drain_market_data().empty());
}

TEST(MarketData, TradeSequenceMatchesItsMarketDataEvent) {
    MatchingEngine engine;
    engine.submit(make_new_order(1, Side::Sell, 50, 10));
    engine.drain_market_data();

    auto engine_events = engine.submit(make_new_order(2, Side::Buy, 60, 10));
    auto md_events = engine.drain_market_data();

    SequenceNumber trade_engine_sequence = 0;
    for (const auto& event : engine_events) {
        if (const auto* trade = std::get_if<TradeExecuted>(&event)) {
            trade_engine_sequence = trade->trade.sequence;
        }
    }
    ASSERT_NE(trade_engine_sequence, 0u);

    bool found_matching_md_trade = false;
    for (const auto& md : md_events) {
        if (md.type == MarketDataEventType::Trade && md.sequence == trade_engine_sequence) {
            found_matching_md_trade = true;
        }
    }
    EXPECT_TRUE(found_matching_md_trade);
}

TEST(MarketData, SnapshotSequenceReflectsLastEmittedEvent) {
    MatchingEngine engine;
    EXPECT_EQ(engine.snapshot(10).sequence, 0u); // nothing emitted yet

    engine.submit(make_new_order(1, Side::Buy, 50, 10));
    auto events = engine.drain_market_data();
    ASSERT_EQ(events.size(), 1u);

    EXPECT_EQ(engine.snapshot(10).sequence, events[0].sequence);
}
