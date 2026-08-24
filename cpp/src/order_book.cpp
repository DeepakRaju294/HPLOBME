#include "order_book.hpp"

#include <stdexcept>

#include "invariants.hpp"

namespace lob {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

// FNV-1a over the bytes of a 64-bit value, mixed into an existing hash.
// Deterministic across runs/platforms, which is what state_hash() needs
// for replay verification (Milestone 3).
std::uint64_t fnv1a(std::uint64_t hash, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        auto byte = static_cast<std::uint8_t>((value >> shift) & 0xFFu);
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

template <typename LevelMap>
PriceLevel& OrderBook::level_for(LevelMap& levels, Price price) {
    auto it = levels.find(price);
    if (it == levels.end()) {
        it = levels.emplace(price, PriceLevel(price)).first;
    }
    return it->second;
}

OrderLocation OrderBook::add_order(const Order& order) {
    if (order.remaining_quantity == 0) {
        throw std::invalid_argument("OrderBook::add_order: zero quantity");
    }
    if (order_index_.contains(order.id)) {
        throw std::invalid_argument("OrderBook::add_order: duplicate order id");
    }

    PriceLevel& level = (order.side == Side::Buy)
        ? level_for(bids_, order.price)
        : level_for(asks_, order.price);

    auto iterator = level.push_back(order);
    OrderLocation location{order.side, order.price, iterator};
    order_index_.emplace(order.id, location);
    return location;
}

bool OrderBook::cancel_order(OrderId id) {
    auto found = order_index_.find(id);
    if (found == order_index_.end()) {
        return false;
    }
    const OrderLocation location = found->second;
    order_index_.erase(found);

    if (location.side == Side::Buy) {
        auto level_it = bids_.find(location.price);
        level_it->second.erase(location.iterator);
        if (level_it->second.empty()) {
            bids_.erase(level_it);
        }
    } else {
        auto level_it = asks_.find(location.price);
        level_it->second.erase(location.iterator);
        if (level_it->second.empty()) {
            asks_.erase(level_it);
        }
    }
    return true;
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

Quantity OrderBook::quantity_at_price(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it == bids_.end() ? Quantity{0} : it->second.total_quantity();
    }
    auto it = asks_.find(price);
    return it == asks_.end() ? Quantity{0} : it->second.total_quantity();
}

std::vector<OrderId> OrderBook::order_ids_at_price(Side side, Price price) const {
    std::vector<OrderId> ids;
    auto collect = [&](const PriceLevel& level) {
        for (const auto& order : level) {
            ids.push_back(order.id);
        }
    };
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end()) {
            collect(it->second);
        }
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) {
            collect(it->second);
        }
    }
    return ids;
}

BookSnapshot OrderBook::snapshot(std::size_t depth) const {
    BookSnapshot snap;
    snap.sequence = 0;
    snap.timestamp = 0;

    std::size_t count = 0;
    for (const auto& [price, level] : bids_) {
        if (count++ >= depth) {
            break;
        }
        snap.bids.push_back(PriceLevelView{price, level.total_quantity()});
    }

    count = 0;
    for (const auto& [price, level] : asks_) {
        if (count++ >= depth) {
            break;
        }
        snap.asks.push_back(PriceLevelView{price, level.total_quantity()});
    }

    return snap;
}

const Order* OrderBook::find_order(OrderId id) const {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) {
        return nullptr;
    }
    return &(*it->second.iterator);
}

const Order* OrderBook::best_order(Side side) const {
    if (side == Side::Buy) {
        if (bids_.empty()) {
            return nullptr;
        }
        return &bids_.begin()->second.front();
    }
    if (asks_.empty()) {
        return nullptr;
    }
    return &asks_.begin()->second.front();
}

template <typename LevelMap>
Quantity OrderBook::fill_best_order_impl(LevelMap& levels, Quantity qty,
                                          std::unordered_map<OrderId, OrderLocation>& order_index) {
    auto level_it = levels.begin();
    PriceLevel& level = level_it->second;
    const Order& front = level.front();

    if (qty >= front.remaining_quantity) {
        const OrderId id = front.id;
        order_index.erase(id);
        level.pop_front();
        if (level.empty()) {
            levels.erase(level_it);
        }
        return 0;
    }

    level.reduce_front_quantity(qty);
    return level.front().remaining_quantity;
}

Quantity OrderBook::fill_best_order(Side side, Quantity qty) {
    if (side == Side::Buy) {
        return fill_best_order_impl(bids_, qty, order_index_);
    }
    return fill_best_order_impl(asks_, qty, order_index_);
}

Quantity OrderBook::matchable_quantity(Side incoming_side, std::optional<Price> price_limit, Quantity cap) const {
    Quantity total = 0;
    if (incoming_side == Side::Buy) {
        for (const auto& [price, level] : asks_) {
            if (price_limit.has_value() && price > *price_limit) {
                break;
            }
            total += level.total_quantity();
            if (total >= cap) {
                break;
            }
        }
    } else {
        for (const auto& [price, level] : bids_) {
            if (price_limit.has_value() && price < *price_limit) {
                break;
            }
            total += level.total_quantity();
            if (total >= cap) {
                break;
            }
        }
    }
    return total;
}

bool OrderBook::reduce_quantity_in_place(OrderId id, Quantity new_quantity) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) {
        return false;
    }
    const OrderLocation& location = it->second;
    if (location.side == Side::Buy) {
        bids_.find(location.price)->second.reduce_quantity(location.iterator, new_quantity);
    } else {
        asks_.find(location.price)->second.reduce_quantity(location.iterator, new_quantity);
    }
    return true;
}

bool OrderBook::validate_invariants() const {
    return validate_book_invariants(*this);
}

std::uint64_t OrderBook::state_hash() const {
    std::uint64_t hash = kFnvOffsetBasis;

    for (const auto& [price, level] : bids_) {
        hash = fnv1a(hash, static_cast<std::uint64_t>(price));
        for (const auto& order : level) {
            hash = fnv1a(hash, order.id);
            hash = fnv1a(hash, order.remaining_quantity);
        }
    }

    hash = fnv1a(hash, 0xFFFFFFFFFFFFFFFFull); // side separator

    for (const auto& [price, level] : asks_) {
        hash = fnv1a(hash, static_cast<std::uint64_t>(price));
        for (const auto& order : level) {
            hash = fnv1a(hash, order.id);
            hash = fnv1a(hash, order.remaining_quantity);
        }
    }

    return hash;
}

} // namespace lob
