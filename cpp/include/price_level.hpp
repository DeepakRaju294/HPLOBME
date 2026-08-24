#pragma once

#include <cstddef>
#include <list>

#include "order.hpp"

namespace lob {

// FIFO queue of resting orders at a single price. Owned by the baseline
// std::map<Price, PriceLevel> book (order_book.hpp) and, later, by the
// dense tick-indexed representation used for the Milestone 5 comparison.
//
// std::list (not deque/vector) so that OrderLocation can hold a stable
// iterator into this level, giving O(1) cancel/erase without scanning.
class PriceLevel {
public:
    using OrderIterator = std::list<Order>::iterator;
    using ConstOrderIterator = std::list<Order>::const_iterator;

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
    std::list<Order> orders_;
};

} // namespace lob
