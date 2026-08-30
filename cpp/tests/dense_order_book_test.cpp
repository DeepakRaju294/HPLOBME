#include <gtest/gtest.h>

#include <random>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "dense_order_book.hpp"

using namespace lob;

namespace {

constexpr Price kMin = 0;
constexpr Price kMax = 999;

Order make_order(OrderId id, Side side, Price price, Quantity qty, Timestamp ts = 0) {
    Order order{};
    order.id = id;
    order.side = side;
    order.type = OrderType::Limit;
    order.time_in_force = TimeInForce::GoodTillCancel;
    order.price = price;
    order.quantity = qty;
    order.remaining_quantity = qty;
    order.timestamp = ts;
    return order;
}

} // namespace

TEST(DenseOrderBook, EmptyBookHasNoBestPrices) {
    DenseOrderBook book(kMin, kMax);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 0u);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(DenseOrderBook, InsertsRestOnCorrectSide) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Sell, 105, 5));

    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 105);
    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 10u);
    EXPECT_EQ(book.quantity_at_price(Side::Sell, 105), 5u);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(DenseOrderBook, MultipleLevelsOrderedCorrectly) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 102, 5));
    book.add_order(make_order(3, Side::Buy, 98, 7));
    EXPECT_EQ(book.best_bid(), 102);

    book.add_order(make_order(4, Side::Sell, 110, 10));
    book.add_order(make_order(5, Side::Sell, 108, 5));
    book.add_order(make_order(6, Side::Sell, 112, 7));
    EXPECT_EQ(book.best_ask(), 108);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(DenseOrderBook, BestBidUpdatesWhenCurrentBestIsRemoved) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 102, 5));
    book.add_order(make_order(3, Side::Buy, 98, 7));
    ASSERT_EQ(book.best_bid(), 102);

    book.cancel_order(2); // removes the current best bid
    EXPECT_EQ(book.best_bid(), 100); // must scan outward to the next-best
    EXPECT_TRUE(book.validate_invariants());

    book.cancel_order(1);
    EXPECT_EQ(book.best_bid(), 98);

    book.cancel_order(3);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_TRUE(book.validate_invariants());
}

TEST(DenseOrderBook, BestAskUpdatesWhenCurrentBestIsRemoved) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Sell, 110, 10));
    book.add_order(make_order(2, Side::Sell, 108, 5));
    book.add_order(make_order(3, Side::Sell, 112, 7));
    ASSERT_EQ(book.best_ask(), 108);

    book.cancel_order(2);
    EXPECT_EQ(book.best_ask(), 110);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(DenseOrderBook, AggregateQuantitySumsOrdersAtLevel) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 100, 20));
    book.add_order(make_order(3, Side::Buy, 100, 5));
    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 35u);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(DenseOrderBook, FifoOrderPreservedWithinLevel) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 100, 20));
    book.add_order(make_order(3, Side::Buy, 100, 5));
    EXPECT_EQ(book.order_ids_at_price(Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));
}

TEST(DenseOrderBook, CancelRemovesFromFifoWithoutDisturbingOthers) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 100, 20));
    book.add_order(make_order(3, Side::Buy, 100, 5));

    EXPECT_TRUE(book.cancel_order(2));
    EXPECT_EQ(book.order_ids_at_price(Side::Buy, 100), (std::vector<OrderId>{1, 3}));
    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 15u);

    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_TRUE(book.cancel_order(3));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_TRUE(book.validate_invariants());
}

TEST(DenseOrderBook, CancelUnknownIdReturnsFalse) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 100, 10));
    EXPECT_FALSE(book.cancel_order(999));
    EXPECT_TRUE(book.validate_invariants());
}

TEST(DenseOrderBook, DuplicateOrderIdThrows) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 100, 10));
    EXPECT_THROW(book.add_order(make_order(1, Side::Sell, 105, 5)), std::invalid_argument);
}

TEST(DenseOrderBook, ZeroQuantityOrderThrows) {
    DenseOrderBook book(kMin, kMax);
    EXPECT_THROW(book.add_order(make_order(1, Side::Buy, 100, 0)), std::invalid_argument);
}

TEST(DenseOrderBook, PriceOutOfConfiguredRangeThrows) {
    DenseOrderBook book(kMin, kMax);
    EXPECT_THROW(book.add_order(make_order(1, Side::Buy, kMax + 1, 10)), std::out_of_range);
    EXPECT_THROW(book.add_order(make_order(2, Side::Buy, kMin - 1, 10)), std::out_of_range);
}

TEST(DenseOrderBook, ValidateInvariantsDetectsCrossedBook) {
    DenseOrderBook book(kMin, kMax);
    book.add_order(make_order(1, Side::Buy, 105, 10));
    book.add_order(make_order(2, Side::Sell, 100, 10));
    EXPECT_FALSE(book.validate_invariants());
}

TEST(DenseOrderBook, StateHashIsDeterministicForIdenticalSequences) {
    DenseOrderBook a(kMin, kMax);
    DenseOrderBook b(kMin, kMax);
    for (OrderId id = 1; id <= 5; ++id) {
        a.add_order(make_order(id, Side::Buy, 100 + static_cast<Price>(id), 10));
        b.add_order(make_order(id, Side::Buy, 100 + static_cast<Price>(id), 10));
    }
    EXPECT_EQ(a.state_hash(), b.state_hash());

    a.cancel_order(3);
    EXPECT_NE(a.state_hash(), b.state_hash());
}

TEST(DenseOrderBook, RandomizedAddCancelSequencePreservesInvariants) {
    DenseOrderBook book(kMin, kMax);
    std::mt19937 rng(1234); // same seed as the OrderBook equivalent test
    std::unordered_set<OrderId> active_ids;
    OrderId next_id = 1;

    for (int step = 0; step < 2000; ++step) {
        const bool do_add = active_ids.empty() || (rng() % 3 != 0);
        if (do_add) {
            const OrderId id = next_id++;
            const Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            const Price price = (side == Side::Buy) ? 80 + static_cast<Price>(rng() % 20)
                                                      : 101 + static_cast<Price>(rng() % 20);
            const Quantity qty = 1 + (rng() % 50);
            book.add_order(make_order(id, side, price, qty));
            active_ids.insert(id);
        } else {
            auto it = active_ids.begin();
            std::advance(it, rng() % active_ids.size());
            const OrderId id = *it;
            ASSERT_TRUE(book.cancel_order(id));
            active_ids.erase(it);
        }

        ASSERT_TRUE(book.validate_invariants()) << "invariant violated at step " << step;
        ASSERT_EQ(book.active_order_count(), active_ids.size());
    }
}
