#include "invariants.hpp"

#include <unordered_set>

#include "order_book.hpp"

namespace lob {

namespace {

template <typename LevelMap>
bool check_side(const LevelMap& levels,
                 Side expected_side,
                 const std::unordered_map<OrderId, OrderLocation>& order_index,
                 std::unordered_set<OrderId>& seen_ids,
                 std::size_t& counted_orders) {
    for (const auto& [price, level] : levels) {
        // No empty price levels, and the map key must match the level's
        // own price (std::map ordering already guarantees strict sort).
        if (level.empty()) {
            return false;
        }
        if (level.price() != price) {
            return false;
        }

        Quantity summed_quantity = 0;
        for (const auto& order : level) {
            if (order.remaining_quantity == 0) {
                return false;
            }
            if (order.side != expected_side || order.price != price) {
                return false;
            }
            if (!seen_ids.insert(order.id).second) {
                return false; // duplicate order id
            }

            auto indexed = order_index.find(order.id);
            if (indexed == order_index.end()) {
                return false; // every resting order must be indexed
            }
            if (indexed->second.side != expected_side || indexed->second.price != price) {
                return false;
            }

            summed_quantity += order.remaining_quantity;
            ++counted_orders;
        }

        if (summed_quantity != level.total_quantity()) {
            return false; // aggregate quantity must equal contained quantity
        }
    }
    return true;
}

} // namespace

bool validate_book_invariants(const OrderBook& book) {
    std::unordered_set<OrderId> seen_ids;
    std::size_t counted_orders = 0;

    if (!check_side(book.bids_, Side::Buy, book.order_index_, seen_ids, counted_orders)) {
        return false;
    }
    if (!check_side(book.asks_, Side::Sell, book.order_index_, seen_ids, counted_orders)) {
        return false;
    }

    // Every indexed order must exist in the book (sizes reconcile) -- this
    // also catches an index entry left dangling after a book-side change.
    if (counted_orders != book.order_index_.size()) {
        return false;
    }

    // Resting book must never be crossed.
    if (!book.bids_.empty() && !book.asks_.empty()) {
        if (book.bids_.begin()->first >= book.asks_.begin()->first) {
            return false;
        }
    }

    return true;
}

} // namespace lob
