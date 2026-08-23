#pragma once

namespace lob {

class OrderBook;

// Debug-build invariant checks (spec section 21). After every command:
//   - bids strictly ordered highest to lowest, asks lowest to highest
//   - resting book is never crossed
//   - every active order appears exactly once; every indexed order exists
//     in the book
//   - every resting quantity is positive
//   - price-level aggregate quantity equals contained-order quantity
//   - no empty price levels
//   - order IDs are unique
bool validate_book_invariants(const OrderBook& book);

} // namespace lob
