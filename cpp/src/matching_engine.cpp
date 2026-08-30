#include "matching_engine.hpp"

#include <algorithm>
#include <utility>

namespace lob {

namespace {

bool time_in_force_valid_for_type(OrderType type, TimeInForce tif) {
    if (type == OrderType::Market) {
        // Market orders never rest, so GTC (rest until cancelled) and
        // PostOnly (must add liquidity) are meaningless for them.
        return tif == TimeInForce::ImmediateOrCancel || tif == TimeInForce::FillOrKill;
    }
    return true; // all four TIFs are valid for Limit orders
}

template <typename BookType>
bool would_cross(const BookType& book, Side side, Price price) {
    if (side == Side::Buy) {
        const auto ask = book.best_ask();
        return ask.has_value() && price >= *ask;
    }
    const auto bid = book.best_bid();
    return bid.has_value() && price <= *bid;
}

Order build_resting_order(OrderId id, Side side, OrderType type, TimeInForce tif,
                           Price price, Quantity quantity, Timestamp timestamp) {
    Order order{};
    order.id = id;
    order.side = side;
    order.type = type;
    order.time_in_force = tif;
    order.price = price;
    order.quantity = quantity;
    order.remaining_quantity = quantity;
    order.timestamp = timestamp;
    return order;
}

} // namespace

template <typename BookType>
MarketDataEvent& MatchingEngineT<BookType>::push_market_data(MarketDataEventType type, Side side, Price price,
                                                               Quantity quantity, Timestamp timestamp) {
    MarketDataEvent event{};
    event.sequence = next_sequence_++;
    event.timestamp = timestamp;
    event.type = type;
    event.side = side;
    event.price = price;
    event.quantity = quantity;
    market_data_queue_.push_back(event);
    return market_data_queue_.back();
}

template <typename BookType>
std::vector<MarketDataEvent> MatchingEngineT<BookType>::drain_market_data() {
    std::vector<MarketDataEvent> drained;
    std::swap(drained, market_data_queue_);
    return drained;
}

template <typename BookType>
BookSnapshot MatchingEngineT<BookType>::snapshot(std::size_t depth) const {
    BookSnapshot snap = book_.snapshot(depth);
    snap.sequence = (next_sequence_ > 1) ? (next_sequence_ - 1) : 0;
    snap.timestamp = last_timestamp_;
    return snap;
}

template <typename BookType>
void MatchingEngineT<BookType>::rest_order(const Order& order) {
    const Quantity before = book_.quantity_at_price(order.side, order.price);
    book_.add_order(order);
    const Quantity after = before + order.remaining_quantity;
    push_market_data(before == 0 ? MarketDataEventType::Add : MarketDataEventType::Modify,
                      order.side, order.price, after, order.timestamp);
}

template <typename BookType>
bool MatchingEngineT<BookType>::cancel_and_publish(OrderId id, Timestamp timestamp) {
    const Order* existing = book_.find_order(id);
    if (existing == nullptr) {
        return false;
    }
    const Side side = existing->side;
    const Price price = existing->price;
    book_.cancel_order(id);
    const Quantity after = book_.quantity_at_price(side, price);
    push_market_data(after == 0 ? MarketDataEventType::Delete : MarketDataEventType::Modify,
                      side, price, after, timestamp);
    return true;
}

template <typename BookType>
std::vector<EngineEvent> MatchingEngineT<BookType>::submit(const NewOrder& cmd) {
    return process_new_order(cmd);
}

template <typename BookType>
std::vector<EngineEvent> MatchingEngineT<BookType>::process_new_order(const NewOrder& cmd) {
    last_timestamp_ = cmd.timestamp;

    if (cmd.quantity == 0) {
        return {OrderRejected{cmd.id, RejectReason::ZeroQuantity, cmd.timestamp}};
    }
    if (book_.has_order(cmd.id)) {
        return {OrderRejected{cmd.id, RejectReason::DuplicateOrderId, cmd.timestamp}};
    }
    if (cmd.type == OrderType::Limit && cmd.price <= 0) {
        return {OrderRejected{cmd.id, RejectReason::InvalidPrice, cmd.timestamp}};
    }
    if (!time_in_force_valid_for_type(cmd.type, cmd.time_in_force)) {
        return {OrderRejected{cmd.id, RejectReason::InvalidTimeInForceForOrderType, cmd.timestamp}};
    }

    Order incoming = build_resting_order(cmd.id, cmd.side, cmd.type, cmd.time_in_force,
                                          cmd.price, cmd.quantity, cmd.timestamp);

    // PostOnly must never take liquidity: reject outright if it would
    // cross, otherwise rest in full with no matching attempted.
    if (cmd.time_in_force == TimeInForce::PostOnly) {
        if (would_cross(book_, incoming.side, incoming.price)) {
            return {OrderRejected{cmd.id, RejectReason::WouldCross, cmd.timestamp}};
        }
        rest_order(incoming);
        return {
            OrderAccepted{cmd.id, cmd.timestamp},
            OrderRested{cmd.id, incoming.price, incoming.remaining_quantity, cmd.timestamp},
        };
    }

    // FOK is all-or-nothing: verify full fillability before touching the
    // book, so a rejection never partially executes.
    if (cmd.time_in_force == TimeInForce::FillOrKill) {
        const std::optional<Price> price_limit =
            (cmd.type == OrderType::Limit) ? std::optional<Price>(cmd.price) : std::nullopt;
        const Quantity available = book_.matchable_quantity(incoming.side, price_limit, incoming.quantity);
        if (available < incoming.quantity) {
            return {OrderRejected{cmd.id, RejectReason::FillOrKillUnfillable, cmd.timestamp}};
        }
    }

    std::vector<EngineEvent> events;
    events.push_back(OrderAccepted{cmd.id, cmd.timestamp});

    walk_match(incoming, events);

    const Quantity filled = incoming.quantity - incoming.remaining_quantity;

    if (incoming.remaining_quantity == 0) {
        events.push_back(OrderFilled{cmd.id, cmd.timestamp});
        return events;
    }

    if (cmd.time_in_force == TimeInForce::GoodTillCancel) {
        if (filled > 0) {
            events.push_back(OrderPartiallyFilled{cmd.id, filled, incoming.remaining_quantity, cmd.timestamp});
        }
        rest_order(incoming);
        events.push_back(OrderRested{cmd.id, incoming.price, incoming.remaining_quantity, cmd.timestamp});
        return events;
    }

    // IOC (Limit or Market): never rests. Any residual is cancelled.
    // (FOK never reaches here with a residual -- the pre-check above
    // guarantees full fillability before matching starts.)
    if (filled > 0) {
        events.push_back(OrderPartiallyFilled{cmd.id, filled, incoming.remaining_quantity, cmd.timestamp});
    }
    events.push_back(OrderCancelled{cmd.id, cmd.timestamp});
    return events;
}

template <typename BookType>
void MatchingEngineT<BookType>::walk_match(Order& incoming, std::vector<EngineEvent>& events) {
    const Side opposite = (incoming.side == Side::Buy) ? Side::Sell : Side::Buy;

    while (incoming.remaining_quantity > 0) {
        const Order* maker = book_.best_order(opposite);
        if (maker == nullptr) {
            break;
        }

        if (incoming.type == OrderType::Limit) {
            const bool price_ok = (incoming.side == Side::Buy)
                ? (incoming.price >= maker->price)
                : (incoming.price <= maker->price);
            if (!price_ok) {
                break;
            }
        }

        const OrderId maker_id = maker->id;
        const Price trade_price = maker->price;
        const Quantity trade_qty = std::min(incoming.remaining_quantity, maker->remaining_quantity);

        const Quantity maker_remaining_after = book_.fill_best_order(opposite, trade_qty);
        incoming.remaining_quantity -= trade_qty;

        Trade trade{};
        trade.id = next_trade_id_++;
        trade.aggressor_order_id = incoming.id;
        trade.resting_order_id = maker_id;
        trade.aggressor_side = incoming.side;
        trade.price = trade_price;
        trade.quantity = trade_qty;
        trade.timestamp = incoming.timestamp;
        trade.sequence = push_market_data(MarketDataEventType::Trade, incoming.side, trade_price,
                                           trade_qty, incoming.timestamp)
                              .sequence;
        events.push_back(TradeExecuted{trade});

        if (maker_remaining_after == 0) {
            events.push_back(OrderFilled{maker_id, incoming.timestamp});
        } else {
            events.push_back(OrderPartiallyFilled{maker_id, trade_qty, maker_remaining_after, incoming.timestamp});
        }

        // The maker's own remaining quantity (above) is distinct from the
        // price level's aggregate, which may still hold other orders.
        const Quantity level_after = book_.quantity_at_price(opposite, trade_price);
        push_market_data(level_after == 0 ? MarketDataEventType::Delete : MarketDataEventType::Modify,
                          opposite, trade_price, level_after, incoming.timestamp);
    }
}

template <typename BookType>
std::vector<EngineEvent> MatchingEngineT<BookType>::cancel(const CancelOrder& cmd) {
    last_timestamp_ = cmd.timestamp;
    if (cancel_and_publish(cmd.id, cmd.timestamp)) {
        return {OrderCancelled{cmd.id, cmd.timestamp}};
    }
    return {OrderRejected{cmd.id, RejectReason::UnknownOrderId, cmd.timestamp}};
}

template <typename BookType>
std::vector<EngineEvent> MatchingEngineT<BookType>::replace(const ReplaceOrder& cmd) {
    last_timestamp_ = cmd.timestamp;

    const Order* existing = book_.find_order(cmd.id);
    if (existing == nullptr) {
        return {OrderRejected{cmd.id, RejectReason::UnknownOrderId, cmd.timestamp}};
    }
    if (cmd.new_quantity == 0) {
        return {OrderRejected{cmd.id, RejectReason::ZeroQuantity, cmd.timestamp}};
    }
    if (cmd.new_price <= 0) {
        return {OrderRejected{cmd.id, RejectReason::InvalidPrice, cmd.timestamp}};
    }

    const bool price_changed = (cmd.new_price != existing->price);
    const bool priority_preserved = !price_changed && (cmd.new_quantity <= existing->remaining_quantity);

    if (priority_preserved) {
        const Side side = existing->side;
        const Price price = existing->price;
        book_.reduce_quantity_in_place(cmd.id, cmd.new_quantity);
        const Quantity after = book_.quantity_at_price(side, price);
        push_market_data(MarketDataEventType::Modify, side, price, after, cmd.timestamp);
        return {OrderReplaced{cmd.id, cmd.new_price, cmd.new_quantity, true, cmd.timestamp}};
    }

    // Priority-losing replace: only GTC/PostOnly orders are ever found
    // resting here (IOC/FOK never rest), so snapshot its identity fields
    // and treat this as an atomic cancel-and-reinsert (spec section 11).
    // A PostOnly order must keep its never-takes-liquidity guarantee: if
    // the new price would cross, reject the replace and leave the
    // original order untouched rather than letting it trade.
    const Order snapshot = *existing;

    if (snapshot.time_in_force == TimeInForce::PostOnly && price_changed) {
        if (would_cross(book_, snapshot.side, cmd.new_price)) {
            return {OrderRejected{cmd.id, RejectReason::WouldCross, cmd.timestamp}};
        }
    }

    cancel_and_publish(cmd.id, cmd.timestamp);

    std::vector<EngineEvent> events;
    events.push_back(OrderReplaced{cmd.id, cmd.new_price, cmd.new_quantity, false, cmd.timestamp});

    Order incoming = build_resting_order(snapshot.id, snapshot.side, snapshot.type, snapshot.time_in_force,
                                          cmd.new_price, cmd.new_quantity, cmd.timestamp);

    // A price change may cause immediate execution against the book.
    walk_match(incoming, events);

    const Quantity filled = cmd.new_quantity - incoming.remaining_quantity;

    if (incoming.remaining_quantity == 0) {
        events.push_back(OrderFilled{cmd.id, cmd.timestamp});
        return events;
    }

    if (filled > 0) {
        events.push_back(OrderPartiallyFilled{cmd.id, filled, incoming.remaining_quantity, cmd.timestamp});
    }
    rest_order(incoming);
    events.push_back(OrderRested{cmd.id, incoming.price, incoming.remaining_quantity, cmd.timestamp});
    return events;
}

// Explicit instantiation: MatchingEngineT is only ever used with these
// two book types (see the MatchingEngine/DenseMatchingEngine aliases in
// matching_engine.hpp), so the template body lives here in the .cpp
// rather than bloating the header.
template class MatchingEngineT<OrderBook>;
template class MatchingEngineT<DenseOrderBook>;

} // namespace lob
