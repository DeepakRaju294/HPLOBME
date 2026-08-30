#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "dense_order_book.hpp"
#include "events.hpp"
#include "market_data.hpp"
#include "order.hpp"
#include "order_book.hpp"

namespace lob {

// Single-threaded matching engine, templated on its book representation
// so the std::map-based OrderBook and the dense tick-indexed
// DenseOrderBook (spec section 8.2) run through byte-for-byte identical
// matching logic -- only the underlying price-level storage differs.
// This is what makes the Milestone 5 benchmark comparison and the
// correctness-parity tests (matching_engine_test.cpp) meaningful: any
// difference in behavior between MatchingEngine and DenseMatchingEngine
// would be a bug, not a legitimate tradeoff. See
// docs/performance_analysis.md.
//
// `MatchingEngine` (OrderBook) is the default/production instantiation;
// `DenseMatchingEngine` (DenseOrderBook) is Milestone 5's optimized
// alternative.
//
// Owns all mutable book state and is the sole source of engine events
// (order lifecycle, returned synchronously from submit/cancel/replace)
// and the sequenced public market-data feed (buffered internally,
// drained via drain_market_data()). Core engine code must remain
// independent of Python.
//
// Market-data Add/Modify/Delete events describe price-level (L2)
// aggregate quantity, matching what snapshot() reports -- not individual
// order quantities. See docs/matching_rules.md.
template <typename BookType>
class MatchingEngineT {
public:
    // Forwards to BookType's constructor: OrderBook is default-
    // constructible (`MatchingEngine engine;`); DenseOrderBook requires a
    // price range (`DenseMatchingEngine engine(min_price, max_price);`).
    template <typename... Args>
    explicit MatchingEngineT(Args&&... args) : book_(std::forward<Args>(args)...) {}

    std::vector<EngineEvent> submit(const NewOrder& cmd);
    std::vector<EngineEvent> cancel(const CancelOrder& cmd);
    std::vector<EngineEvent> replace(const ReplaceOrder& cmd);

    // Returns and clears all market-data events generated since the last
    // call to drain_market_data(). Sequence numbers are strictly
    // increasing across the entire feed, regardless of how many times
    // this is called.
    std::vector<MarketDataEvent> drain_market_data();

    std::optional<Price> best_bid() const { return book_.best_bid(); }
    std::optional<Price> best_ask() const { return book_.best_ask(); }
    Quantity quantity_at_price(Side side, Price price) const { return book_.quantity_at_price(side, price); }
    std::size_t active_order_count() const noexcept { return book_.active_order_count(); }

    // sequence is stamped with the last market-data sequence number
    // emitted so far (0 if none yet), so a consumer recovering from a gap
    // knows to resume the incremental feed strictly after this value.
    BookSnapshot snapshot(std::size_t depth) const;

    bool validate_invariants() const { return book_.validate_invariants(); }
    std::uint64_t state_hash() const { return book_.state_hash(); }

private:
    std::vector<EngineEvent> process_new_order(const NewOrder& cmd);

    // Matches `incoming` against resting liquidity opposite its side,
    // decrementing its remaining_quantity and appending TradeExecuted plus
    // the resulting maker OrderFilled/OrderPartiallyFilled events (and the
    // corresponding Trade/level-update market-data events). Stops when
    // incoming is exhausted, the book runs out of liquidity, or (for
    // Limit orders) no resting price satisfies incoming's limit price.
    void walk_match(Order& incoming, std::vector<EngineEvent>& events);

    // Rests `order` in the book and publishes the resulting Add (new
    // level) or Modify (existing level grew) market-data event.
    void rest_order(const Order& order);

    // Cancels a resting order by ID and publishes the resulting Modify
    // (level still has quantity) or Delete (level now empty) market-data
    // event. Returns false without publishing anything if id is unknown.
    bool cancel_and_publish(OrderId id, Timestamp timestamp);

    MarketDataEvent& push_market_data(MarketDataEventType type, Side side, Price price,
                                       Quantity quantity, Timestamp timestamp);

    BookType book_;
    std::vector<MarketDataEvent> market_data_queue_;
    SequenceNumber next_sequence_{1};
    TradeId next_trade_id_{1};
    Timestamp last_timestamp_{0};
};

using MatchingEngine = MatchingEngineT<OrderBook>;
using DenseMatchingEngine = MatchingEngineT<DenseOrderBook>;

} // namespace lob
