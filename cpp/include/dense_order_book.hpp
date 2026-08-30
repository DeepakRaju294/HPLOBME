#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "market_data.hpp"
#include "order.hpp"
#include "order_location.hpp"
#include "price_level.hpp"

namespace lob {

// Dense tick-indexed price-level representation (spec section 8.2),
// benchmarked against the std::map-based OrderBook in Milestone 5.
//
// Price levels for each side live in a single contiguous
// std::vector<PriceLevel> spanning [min_price, max_price], indexed by
// `price - min_price`: O(1) level lookup by price, versus OrderBook's
// O(log levels) tree lookup. The tradeoff (spec section 8.2) is memory --
// every tick in range gets a PriceLevel slot whether occupied or not,
// unlike a map which only pays for levels that exist -- and "find the
// best price" cost, which here means maintaining a cached best-index and
// scanning outward past empty slots when that level empties, rather than
// getting it for free from map ordering. See docs/performance_analysis.md.
//
// Same public surface as OrderBook (see order_book.hpp for the rationale
// behind each method) so MatchingEngineT<BookType> can use either
// interchangeably.
class DenseOrderBook {
public:
    // `min_price`/`max_price` are inclusive; any order priced outside
    // this range is rejected by add_order (throws std::out_of_range).
    DenseOrderBook(Price min_price, Price max_price);

    OrderLocation add_order(const Order& order);
    bool cancel_order(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity quantity_at_price(Side side, Price price) const;
    BookSnapshot snapshot(std::size_t depth) const;
    bool validate_invariants() const;
    std::uint64_t state_hash() const;

    std::vector<OrderId> order_ids_at_price(Side side, Price price) const;
    std::size_t active_order_count() const noexcept { return order_index_.size(); }

    bool has_order(OrderId id) const noexcept { return order_index_.contains(id); }
    const Order* find_order(OrderId id) const;
    const Order* best_order(Side side) const;
    Quantity fill_best_order(Side side, Quantity qty);
    Quantity matchable_quantity(Side incoming_side, std::optional<Price> price_limit, Quantity cap) const;
    bool reduce_quantity_in_place(OrderId id, Quantity new_quantity);

    Price min_price() const noexcept { return min_price_; }
    Price max_price() const noexcept { return max_price_; }

    friend bool validate_dense_book_invariants(const DenseOrderBook& book);

private:
    std::size_t index_of(Price price) const noexcept { return static_cast<std::size_t>(price - min_price_); }
    Price price_of(std::size_t index) const noexcept { return min_price_ + static_cast<Price>(index); }
    bool in_range(Price price) const noexcept { return price >= min_price_ && price <= max_price_; }

    std::vector<PriceLevel>& levels_for(Side side) { return side == Side::Buy ? bid_levels_ : ask_levels_; }
    const std::vector<PriceLevel>& levels_for(Side side) const {
        return side == Side::Buy ? bid_levels_ : ask_levels_;
    }

    void update_best_on_insert(Side side, std::size_t index);
    void update_best_on_possible_empty(Side side, std::size_t index);

    Price min_price_;
    Price max_price_;
    std::vector<PriceLevel> bid_levels_;
    std::vector<PriceLevel> ask_levels_;
    std::optional<std::size_t> best_bid_index_;
    std::optional<std::size_t> best_ask_index_;
    std::unordered_map<OrderId, OrderLocation> order_index_;
};

} // namespace lob
