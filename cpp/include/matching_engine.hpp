#pragma once

#include <vector>

#include "events.hpp"
#include "market_data.hpp"
#include "order.hpp"
#include "order_book.hpp"

namespace lob {

// Single-threaded matching engine. Owns all mutable book state and is the
// sole source of engine events (order lifecycle) and sequenced market-data
// events (public feed). Core engine code must remain independent of Python.
class MatchingEngine {
public:
    std::vector<EngineEvent> submit(const NewOrder&);
    std::vector<EngineEvent> cancel(const CancelOrder&);
    std::vector<EngineEvent> replace(const ReplaceOrder&);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Quantity quantity_at_price(Side side, Price price) const;
    BookSnapshot snapshot(std::size_t depth) const;
    bool validate_invariants() const;
    std::uint64_t state_hash() const;

    // TODO(Milestone 2):
    //   - command validation (reject unknown/invalid before touching book)
    //   - matching loop against OrderBook, trade generation
    //   - time-in-force handling: GTC, IOC, FOK, PostOnly
    //   - replace priority rules (section 11)
    //   - market-data event emission with monotonic sequence numbers

private:
    OrderBook book_;
    SequenceNumber next_sequence_{1};
    TradeId next_trade_id_{1};
};

} // namespace lob
