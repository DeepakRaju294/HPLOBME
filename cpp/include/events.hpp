#pragma once

#include <variant>

#include "order.hpp"
#include "trade.hpp"

namespace lob {

// Structured engine events produced by MatchingEngine::submit/cancel/replace.
// Event ordering within a single command's result vector is deterministic.
// These represent order-lifecycle outcomes, distinct from the sequenced
// market-data feed (see market_data.hpp).

struct OrderAccepted {
    OrderId order_id{};
    Timestamp timestamp{};
};

enum class RejectReason {
    ZeroQuantity,
    DuplicateOrderId,
    InvalidPrice,
    InvalidTimeInForceForOrderType,
    UnknownOrderId,
    WouldCross,             // PostOnly order/replace that would take liquidity
    FillOrKillUnfillable,   // FOK could not be filled in full immediately
};

struct OrderRejected {
    OrderId order_id{};
    RejectReason reason{};
    Timestamp timestamp{};
};

struct OrderRested {
    OrderId order_id{};
    Price price{};
    Quantity quantity{};
    Timestamp timestamp{};
};

struct OrderPartiallyFilled {
    OrderId order_id{};
    Quantity filled_quantity{};
    Quantity remaining_quantity{};
    Timestamp timestamp{};
};

struct OrderFilled {
    OrderId order_id{};
    Timestamp timestamp{};
};

struct OrderCancelled {
    OrderId order_id{};
    Timestamp timestamp{};
};

struct OrderReplaced {
    OrderId order_id{};
    Price new_price{};
    Quantity new_quantity{};
    bool priority_preserved{};
    Timestamp timestamp{};
};

struct TradeExecuted {
    Trade trade{};
};

using EngineEvent = std::variant<
    OrderAccepted,
    OrderRejected,
    OrderRested,
    OrderPartiallyFilled,
    OrderFilled,
    OrderCancelled,
    OrderReplaced,
    TradeExecuted
>;

} // namespace lob
