#pragma once

#include <vector>

#include "order.hpp"

namespace lob {

enum class MarketDataEventType {
    Add,
    Modify,
    Delete,
    Trade,
    Snapshot
};

// A single sequenced market-data event. Sequence numbers are strictly
// increasing across the entire feed produced by one MatchingEngine.
struct MarketDataEvent {
    SequenceNumber sequence{};
    Timestamp timestamp{};
    MarketDataEventType type{};
    Side side{};
    Price price{};
    Quantity quantity{};
};

struct PriceLevelView {
    Price price{};
    Quantity quantity{};
};

// Deterministic depth snapshot used to initialize or recover consumer state.
struct BookSnapshot {
    SequenceNumber sequence{};
    Timestamp timestamp{};
    std::vector<PriceLevelView> bids;
    std::vector<PriceLevelView> asks;
};

} // namespace lob
