#pragma once

#include "order.hpp"

namespace lob {

// A single execution between an incoming (aggressor) order and a resting
// (maker) order. Trades always execute at the resting order's price.
struct Trade {
    TradeId id{};
    OrderId aggressor_order_id{};
    OrderId resting_order_id{};
    Side aggressor_side{};
    Price price{};
    Quantity quantity{};
    Timestamp timestamp{};
    SequenceNumber sequence{};
};

} // namespace lob
