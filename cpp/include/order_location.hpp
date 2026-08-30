#pragma once

#include "order.hpp"
#include "price_level.hpp"

namespace lob {

// Where a resting order lives, so cancel/replace never scans the book.
// Shared by both book representations (order_book.hpp, dense_order_book.hpp)
// compared in Milestone 5 -- both use PriceLevel as their per-price FIFO
// container, so a stable iterator into one means the same thing either way.
struct OrderLocation {
    Side side{};
    Price price{};
    PriceLevel::OrderIterator iterator{};
};

} // namespace lob
