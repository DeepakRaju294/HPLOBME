#include <gtest/gtest.h>

#include <random>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "order_book.hpp"

using namespace lob;

namespace {

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

TEST(OrderBook, EmptyBookHasNoBestPrices) {
    OrderBook book;
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 0u);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBook, InsertsRestOnCorrectSide) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Sell, 105, 5));

    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 105);
    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 10u);
    EXPECT_EQ(book.quantity_at_price(Side::Sell, 105), 5u);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBook, MultipleLevelsOrderedCorrectly) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 102, 5));
    book.add_order(make_order(3, Side::Buy, 98, 7));

    // Highest bid is best.
    EXPECT_EQ(book.best_bid(), 102);

    book.add_order(make_order(4, Side::Sell, 110, 10));
    book.add_order(make_order(5, Side::Sell, 108, 5));
    book.add_order(make_order(6, Side::Sell, 112, 7));

    // Lowest ask is best.
    EXPECT_EQ(book.best_ask(), 108);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBook, AggregateQuantitySumsOrdersAtLevel) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 100, 20));
    book.add_order(make_order(3, Side::Buy, 100, 5));

    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 35u);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBook, FifoOrderPreservedWithinLevel) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 100, 20));
    book.add_order(make_order(3, Side::Buy, 100, 5));

    EXPECT_EQ(book.order_ids_at_price(Side::Buy, 100), (std::vector<OrderId>{1, 2, 3}));
}

TEST(OrderBook, CancelRemovesFromFifoWithoutDisturbingOthers) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.add_order(make_order(2, Side::Buy, 100, 20));
    book.add_order(make_order(3, Side::Buy, 100, 5));

    // Cancel the middle order.
    EXPECT_TRUE(book.cancel_order(2));
    EXPECT_EQ(book.order_ids_at_price(Side::Buy, 100), (std::vector<OrderId>{1, 3}));
    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 15u);

    // Cancel the (now) first order.
    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_EQ(book.order_ids_at_price(Side::Buy, 100), (std::vector<OrderId>{3}));

    // Cancel the last remaining order; the level must disappear entirely.
    EXPECT_TRUE(book.cancel_order(3));
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBook, CancelUnknownIdReturnsFalse) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    EXPECT_FALSE(book.cancel_order(999));
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBook, RepeatedCancelReturnsFalseSecondTime) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    EXPECT_TRUE(book.cancel_order(1));
    EXPECT_FALSE(book.cancel_order(1));
}

TEST(OrderBook, DuplicateOrderIdThrows) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    EXPECT_THROW(book.add_order(make_order(1, Side::Sell, 105, 5)), std::invalid_argument);
}

TEST(OrderBook, ZeroQuantityOrderThrows) {
    OrderBook book;
    EXPECT_THROW(book.add_order(make_order(1, Side::Buy, 100, 0)), std::invalid_argument);
}

TEST(OrderBook, EmptyLevelsAreRemovedAfterFullCancel) {
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 100, 10));
    book.cancel_order(1);

    EXPECT_EQ(book.quantity_at_price(Side::Buy, 100), 0u);
    EXPECT_TRUE(book.order_ids_at_price(Side::Buy, 100).empty());
}

TEST(OrderBook, ValidateInvariantsDetectsCrossedBook) {
    // OrderBook itself does not prevent crossing -- that's the matching
    // engine's job (Milestone 2). This confirms the check can detect it.
    OrderBook book;
    book.add_order(make_order(1, Side::Buy, 105, 10));
    book.add_order(make_order(2, Side::Sell, 100, 10));
    EXPECT_FALSE(book.validate_invariants());
}

TEST(OrderBook, StateHashIsDeterministicForIdenticalSequences) {
    OrderBook a;
    OrderBook b;
    for (OrderId id = 1; id <= 5; ++id) {
        a.add_order(make_order(id, Side::Buy, 100 + static_cast<Price>(id), 10));
        b.add_order(make_order(id, Side::Buy, 100 + static_cast<Price>(id), 10));
    }
    EXPECT_EQ(a.state_hash(), b.state_hash());

    a.cancel_order(3);
    EXPECT_NE(a.state_hash(), b.state_hash());
}

TEST(OrderBook, RandomizedAddCancelSequencePreservesInvariants) {
    OrderBook book;
    std::mt19937 rng(1234); // fixed seed: deterministic per spec section 20
    std::unordered_set<OrderId> active_ids;
    OrderId next_id = 1;

    for (int step = 0; step < 2000; ++step) {
        const bool do_add = active_ids.empty() || (rng() % 3 != 0);
        if (do_add) {
            const OrderId id = next_id++;
            // Buys and asks use disjoint price ranges so random insertion
            // never crosses the book -- crossing resolution is the
            // matching engine's job (Milestone 2), not OrderBook's.
            const Side side = (rng() % 2 == 0) ? Side::Buy : Side::Sell;
            const Price price = (side == Side::Buy)
                ? 80 + static_cast<Price>(rng() % 20)
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
