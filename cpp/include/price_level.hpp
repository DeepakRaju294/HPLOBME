#pragma once

#include <cstddef>
#include <list>

#include "object_pool.hpp"
#include "order.hpp"

namespace lob {

// FIFO queue of resting orders at a single price. Owned by the baseline
// std::map<Price, PriceLevel> book (order_book.hpp) and by the dense
// tick-indexed representation (dense_order_book.hpp) compared against it
// in Milestone 5.
//
// std::list (not deque/vector) so that OrderLocation can hold a stable
// iterator into this level, giving O(1) cancel/erase without scanning.
// Backed by PoolAllocator rather than the default allocator: profiling
// showed per-order node malloc/free dominating the matching hot path
// (spec section 9's preallocated order pool).
class PriceLevel {
public:
    using OrderList = std::list<Order, PoolAllocator<Order>>;
    using OrderIterator = OrderList::iterator;
    using ConstOrderIterator = OrderList::const_iterator;

    explicit PriceLevel(Price price) : price_(price) {}

    Price price() const noexcept { return price_; }
    Quantity total_quantity() const noexcept { return total_quantity_; }
    bool empty() const noexcept { return orders_.empty(); }
    std::size_t order_count() const noexcept { return orders_.size(); }

    // Appends a new resting order, preserving time priority. Returns a
    // stable iterator for the order_index (see order_book.hpp).
    OrderIterator push_back(const Order& order);

    // O(1) removal given an iterator from push_back (no scanning).
    void erase(OrderIterator it);

    Order& front();
    const Order& front() const;
    void pop_front();

    // Reduces the resting quantity of the front (time-priority) order by a
    // partial fill, keeping total_quantity() reconciled. Used by matching.
    void reduce_front_quantity(Quantity filled);

    // Sets an arbitrary order's resting quantity in place, without moving
    // it -- used for a priority-preserving replace (same price, lower
    // quantity; spec section 11). Caller guarantees new_quantity does not
    // exceed the order's current remaining_quantity.
    void reduce_quantity(OrderIterator it, Quantity new_quantity);

    ConstOrderIterator begin() const { return orders_.begin(); }
    ConstOrderIterator end() const { return orders_.end(); }

private:
    Price price_;
    Quantity total_quantity_{};
    OrderList orders_;
};

} // namespace lob
