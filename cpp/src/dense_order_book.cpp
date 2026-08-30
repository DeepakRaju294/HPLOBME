#include "dense_order_book.hpp"

#include <stdexcept>
#include <unordered_set>

namespace lob {

DenseOrderBook::DenseOrderBook(Price min_price, Price max_price) : min_price_(min_price), max_price_(max_price) {
    if (max_price < min_price) {
        throw std::invalid_argument("DenseOrderBook: max_price must be >= min_price");
    }
    const auto range = static_cast<std::size_t>(max_price - min_price) + 1;
    bid_levels_.reserve(range);
    ask_levels_.reserve(range);
    for (std::size_t i = 0; i < range; ++i) {
        const Price price = min_price_ + static_cast<Price>(i);
        bid_levels_.emplace_back(price);
        ask_levels_.emplace_back(price);
    }
}

void DenseOrderBook::update_best_on_insert(Side side, std::size_t index) {
    auto& best = (side == Side::Buy) ? best_bid_index_ : best_ask_index_;
    if (!best.has_value()) {
        best = index;
        return;
    }
    if (side == Side::Buy) {
        if (index > *best) {
            best = index; // higher price is the better bid
        }
    } else {
        if (index < *best) {
            best = index; // lower price is the better ask
        }
    }
}

void DenseOrderBook::update_best_on_possible_empty(Side side, std::size_t index) {
    auto& best = (side == Side::Buy) ? best_bid_index_ : best_ask_index_;
    const auto& levels = levels_for(side);

    if (!best.has_value() || *best != index) {
        return; // the changed level wasn't the cached best; no update needed
    }
    if (!levels[index].empty()) {
        return; // still occupied, still the best
    }

    if (side == Side::Buy) {
        for (std::size_t i = index; i-- > 0;) {
            if (!levels[i].empty()) {
                best = i;
                return;
            }
        }
    } else {
        for (std::size_t i = index + 1; i < levels.size(); ++i) {
            if (!levels[i].empty()) {
                best = i;
                return;
            }
        }
    }
    best.reset(); // no occupied levels remain on this side
}

OrderLocation DenseOrderBook::add_order(const Order& order) {
    if (order.remaining_quantity == 0) {
        throw std::invalid_argument("DenseOrderBook::add_order: zero quantity");
    }
    if (order_index_.contains(order.id)) {
        throw std::invalid_argument("DenseOrderBook::add_order: duplicate order id");
    }
    if (!in_range(order.price)) {
        throw std::out_of_range("DenseOrderBook::add_order: price outside configured range");
    }

    const std::size_t idx = index_of(order.price);
    auto iterator = levels_for(order.side)[idx].push_back(order);
    update_best_on_insert(order.side, idx);

    OrderLocation location{order.side, order.price, iterator};
    order_index_.emplace(order.id, location);
    return location;
}

bool DenseOrderBook::cancel_order(OrderId id) {
    auto found = order_index_.find(id);
    if (found == order_index_.end()) {
        return false;
    }
    const OrderLocation location = found->second;
    order_index_.erase(found);

    const std::size_t idx = index_of(location.price);
    levels_for(location.side)[idx].erase(location.iterator);
    update_best_on_possible_empty(location.side, idx);
    return true;
}

std::optional<Price> DenseOrderBook::best_bid() const {
    if (!best_bid_index_.has_value()) {
        return std::nullopt;
    }
    return price_of(*best_bid_index_);
}

std::optional<Price> DenseOrderBook::best_ask() const {
    if (!best_ask_index_.has_value()) {
        return std::nullopt;
    }
    return price_of(*best_ask_index_);
}

Quantity DenseOrderBook::quantity_at_price(Side side, Price price) const {
    if (!in_range(price)) {
        return 0;
    }
    return levels_for(side)[index_of(price)].total_quantity();
}

std::vector<OrderId> DenseOrderBook::order_ids_at_price(Side side, Price price) const {
    std::vector<OrderId> ids;
    if (!in_range(price)) {
        return ids;
    }
    for (const auto& order : levels_for(side)[index_of(price)]) {
        ids.push_back(order.id);
    }
    return ids;
}

const Order* DenseOrderBook::find_order(OrderId id) const {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) {
        return nullptr;
    }
    return &(*it->second.iterator);
}

const Order* DenseOrderBook::best_order(Side side) const {
    const auto& best = (side == Side::Buy) ? best_bid_index_ : best_ask_index_;
    if (!best.has_value()) {
        return nullptr;
    }
    return &levels_for(side)[*best].front();
}

Quantity DenseOrderBook::fill_best_order(Side side, Quantity qty) {
    auto& best = (side == Side::Buy) ? best_bid_index_ : best_ask_index_;
    const std::size_t idx = *best; // caller only calls this after best_order() confirmed non-null
    PriceLevel& level = levels_for(side)[idx];
    const Order& front = level.front();

    if (qty >= front.remaining_quantity) {
        const OrderId id = front.id;
        order_index_.erase(id);
        level.pop_front();
        update_best_on_possible_empty(side, idx);
        return 0;
    }

    level.reduce_front_quantity(qty);
    return level.front().remaining_quantity;
}

Quantity DenseOrderBook::matchable_quantity(Side incoming_side, std::optional<Price> price_limit,
                                             Quantity cap) const {
    Quantity total = 0;
    const Side opposite = (incoming_side == Side::Buy) ? Side::Sell : Side::Buy;
    const auto& levels = levels_for(opposite);
    const auto& best = (opposite == Side::Buy) ? best_bid_index_ : best_ask_index_;
    if (!best.has_value()) {
        return 0;
    }

    if (incoming_side == Side::Buy) {
        // Asks ascend in price as the index grows; walk from the best
        // (lowest) ask upward.
        for (std::size_t i = *best; i < levels.size(); ++i) {
            const Price price = price_of(i);
            if (price_limit.has_value() && price > *price_limit) {
                break;
            }
            total += levels[i].total_quantity();
            if (total >= cap) {
                break;
            }
        }
    } else {
        // Bids descend in price as the index shrinks; walk from the best
        // (highest) bid downward.
        for (std::size_t i = *best + 1; i-- > 0;) {
            const Price price = price_of(i);
            if (price_limit.has_value() && price < *price_limit) {
                break;
            }
            total += levels[i].total_quantity();
            if (total >= cap) {
                break;
            }
        }
    }
    return total;
}

bool DenseOrderBook::reduce_quantity_in_place(OrderId id, Quantity new_quantity) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) {
        return false;
    }
    const OrderLocation& location = it->second;
    levels_for(location.side)[index_of(location.price)].reduce_quantity(location.iterator, new_quantity);
    return true;
}

BookSnapshot DenseOrderBook::snapshot(std::size_t depth) const {
    BookSnapshot snap;
    snap.sequence = 0;
    snap.timestamp = 0;

    std::size_t count = 0;
    if (best_bid_index_.has_value()) {
        for (std::size_t i = *best_bid_index_ + 1; i-- > 0 && count < depth;) {
            if (!bid_levels_[i].empty()) {
                snap.bids.push_back(PriceLevelView{price_of(i), bid_levels_[i].total_quantity()});
                ++count;
            }
        }
    }

    count = 0;
    if (best_ask_index_.has_value()) {
        for (std::size_t i = *best_ask_index_; i < ask_levels_.size() && count < depth; ++i) {
            if (!ask_levels_[i].empty()) {
                snap.asks.push_back(PriceLevelView{price_of(i), ask_levels_[i].total_quantity()});
                ++count;
            }
        }
    }
    return snap;
}

bool DenseOrderBook::validate_invariants() const {
    return validate_dense_book_invariants(*this);
}

std::uint64_t DenseOrderBook::state_hash() const {
    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    auto fnv1a = [](std::uint64_t hash, std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            auto byte = static_cast<std::uint8_t>((value >> shift) & 0xFFu);
            hash ^= byte;
            hash *= kFnvPrime;
        }
        return hash;
    };

    std::uint64_t hash = kFnvOffsetBasis;

    // Iterate bids from best (highest price) down, matching OrderBook's
    // ordering, so identical book contents hash identically regardless
    // of which representation produced them.
    if (best_bid_index_.has_value()) {
        for (std::size_t i = *best_bid_index_ + 1; i-- > 0;) {
            if (bid_levels_[i].empty()) {
                continue;
            }
            hash = fnv1a(hash, static_cast<std::uint64_t>(price_of(i)));
            for (const auto& order : bid_levels_[i]) {
                hash = fnv1a(hash, order.id);
                hash = fnv1a(hash, order.remaining_quantity);
            }
        }
    }

    hash = fnv1a(hash, 0xFFFFFFFFFFFFFFFFull); // side separator

    if (best_ask_index_.has_value()) {
        for (std::size_t i = *best_ask_index_; i < ask_levels_.size(); ++i) {
            if (ask_levels_[i].empty()) {
                continue;
            }
            hash = fnv1a(hash, static_cast<std::uint64_t>(price_of(i)));
            for (const auto& order : ask_levels_[i]) {
                hash = fnv1a(hash, order.id);
                hash = fnv1a(hash, order.remaining_quantity);
            }
        }
    }

    return hash;
}

namespace {

bool check_dense_side(const std::vector<PriceLevel>& levels, Side expected_side, Price min_price,
                       const std::unordered_map<OrderId, OrderLocation>& order_index,
                       std::optional<std::size_t> cached_best, bool best_is_max,
                       std::unordered_set<OrderId>& seen_ids, std::size_t& counted_orders) {
    std::optional<std::size_t> true_best;

    for (std::size_t i = 0; i < levels.size(); ++i) {
        const PriceLevel& level = levels[i];
        if (level.empty()) {
            continue; // unlike the map book, empty slots are expected here
        }

        if (!true_best.has_value() || (best_is_max ? i > *true_best : i < *true_best)) {
            true_best = i;
        }

        const Price expected_price = min_price + static_cast<Price>(i);
        Quantity summed = 0;
        for (const auto& order : level) {
            if (order.remaining_quantity == 0) {
                return false;
            }
            if (order.side != expected_side || order.price != expected_price) {
                return false;
            }
            if (!seen_ids.insert(order.id).second) {
                return false;
            }

            auto indexed = order_index.find(order.id);
            if (indexed == order_index.end()) {
                return false;
            }
            if (indexed->second.side != expected_side || indexed->second.price != expected_price) {
                return false;
            }

            summed += order.remaining_quantity;
            ++counted_orders;
        }
        if (summed != level.total_quantity()) {
            return false;
        }
    }

    return true_best == cached_best;
}

} // namespace

bool validate_dense_book_invariants(const DenseOrderBook& book) {
    std::unordered_set<OrderId> seen_ids;
    std::size_t counted_orders = 0;

    if (!check_dense_side(book.bid_levels_, Side::Buy, book.min_price_, book.order_index_, book.best_bid_index_,
                           /*best_is_max=*/true, seen_ids, counted_orders)) {
        return false;
    }
    if (!check_dense_side(book.ask_levels_, Side::Sell, book.min_price_, book.order_index_, book.best_ask_index_,
                           /*best_is_max=*/false, seen_ids, counted_orders)) {
        return false;
    }

    if (counted_orders != book.order_index_.size()) {
        return false;
    }

    if (book.best_bid_index_.has_value() && book.best_ask_index_.has_value()) {
        if (book.price_of(*book.best_bid_index_) >= book.price_of(*book.best_ask_index_)) {
            return false; // resting book must never be crossed
        }
    }

    return true;
}

} // namespace lob
