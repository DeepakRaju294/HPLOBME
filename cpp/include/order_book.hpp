#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "market_data.hpp"
#include "order.hpp"
#include "price_level.hpp"

namespace lob {

// Where a resting order lives, so cancel/replace never scans the book.
struct OrderLocation {
    Side side{};
    Price price{};
    PriceLevel::OrderIterator iterator{};
};

// Price-time-priority limit order book for a single instrument.
//
// Baseline representation per spec section 8.1: std::map keyed by price,
// ordered so that bids_.begin() is the best bid and asks_.begin() is the
// best ask. A dense tick-indexed alternative (section 8.2) will be added
// and benchmarked against this one in Milestone 5.
//
// OrderBook is a pure resting-order container: it does not match or
// validate commands (that's MatchingEngine's job, section 5.2). Inserting
// a crossing order will rest it as-is, which validate_invariants() will
// correctly flag.
class OrderBook {
public:
    // Rests a new order. Throws std::invalid_argument for a zero quantity
    // or a duplicate order ID.
    OrderLocation add_order(const Order& order);

    // Removes a resting order by ID. Returns false (no scan, no throw) if
    // the ID is unknown or already cancelled.
    bool cancel_order(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity quantity_at_price(Side side, Price price) const;
    BookSnapshot snapshot(std::size_t depth) const;
    bool validate_invariants() const;
    std::uint64_t state_hash() const;

    // Order IDs at a price level in FIFO (time-priority) order. Primarily
    // for tests/introspection.
    std::vector<OrderId> order_ids_at_price(Side side, Price price) const;
    std::size_t active_order_count() const noexcept { return order_index_.size(); }

    // --- Matching-engine primitives (MatchingEngine owns matching
    // decisions; OrderBook only performs the resulting mutations). ---

    bool has_order(OrderId id) const noexcept { return order_index_.contains(id); }

    // Read-only lookup of a resting order by ID, e.g. to snapshot its
    // fields before a priority-losing replace. nullptr if unknown.
    const Order* find_order(OrderId id) const;

    // The time-priority order at the best price on `side`, or nullptr if
    // that side is empty.
    const Order* best_order(Side side) const;

    // Fills `qty` (<= the best order's remaining_quantity) against the
    // best resting order on `side`. Removes it (and its price level, if
    // now empty) if fully filled. Returns the order's remaining_quantity
    // after the fill (0 if it was removed).
    Quantity fill_best_order(Side side, Quantity qty);

    // Total quantity matchable against `incoming_side`'s opposite book,
    // within an optional price bound (nullopt = no bound, i.e. a market
    // order), capped at `cap` (stops walking once reached). Used for the
    // FOK all-or-nothing pre-check; never mutates the book.
    Quantity matchable_quantity(Side incoming_side, std::optional<Price> price_limit, Quantity cap) const;

    // Sets a resting order's quantity in place without moving its FIFO
    // position -- the priority-preserving replace case (spec section 11).
    // Returns false if the ID is unknown.
    bool reduce_quantity_in_place(OrderId id, Quantity new_quantity);

    friend bool validate_book_invariants(const OrderBook& book);

private:
    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskLevels = std::map<Price, PriceLevel, std::less<Price>>;

    template <typename LevelMap>
    static PriceLevel& level_for(LevelMap& levels, Price price);

    template <typename LevelMap>
    static Quantity fill_best_order_impl(LevelMap& levels, Quantity qty,
                                          std::unordered_map<OrderId, OrderLocation>& order_index);

    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, OrderLocation> order_index_;
};

} // namespace lob
