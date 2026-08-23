#pragma once

#include <cstdint>

namespace lob {

using OrderId = std::uint64_t;
using TradeId = std::uint64_t;
using SequenceNumber = std::uint64_t;
using Timestamp = std::uint64_t;
using Quantity = std::uint64_t;
using Price = std::int64_t;

enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market
};

enum class TimeInForce {
    GoodTillCancel,
    ImmediateOrCancel,
    FillOrKill,
    PostOnly
};

// A resting or in-flight order as tracked by the book.
struct Order {
    OrderId id{};
    Side side{};
    OrderType type{};
    TimeInForce time_in_force{};
    Price price{};
    Quantity quantity{};
    Quantity remaining_quantity{};
    Timestamp timestamp{};
};

// Command: submit a new order.
struct NewOrder {
    OrderId id{};
    Side side{};
    OrderType type{};
    TimeInForce time_in_force{};
    Price price{};
    Quantity quantity{};
    Timestamp timestamp{};
};

// Command: cancel a resting order by ID.
struct CancelOrder {
    OrderId id{};
    Timestamp timestamp{};
};

// Command: replace a resting order's price and/or quantity.
struct ReplaceOrder {
    OrderId id{};
    Price new_price{};
    Quantity new_quantity{};
    Timestamp timestamp{};
};

} // namespace lob
