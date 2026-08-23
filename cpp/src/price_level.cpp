#include "price_level.hpp"

namespace lob {

PriceLevel::OrderIterator PriceLevel::push_back(const Order& order) {
    orders_.push_back(order);
    total_quantity_ += order.remaining_quantity;
    auto it = orders_.end();
    --it;
    return it;
}

void PriceLevel::erase(OrderIterator it) {
    total_quantity_ -= it->remaining_quantity;
    orders_.erase(it);
}

Order& PriceLevel::front() {
    return orders_.front();
}

const Order& PriceLevel::front() const {
    return orders_.front();
}

void PriceLevel::pop_front() {
    total_quantity_ -= orders_.front().remaining_quantity;
    orders_.pop_front();
}

void PriceLevel::reduce_front_quantity(Quantity filled) {
    Order& order = orders_.front();
    order.remaining_quantity -= filled;
    total_quantity_ -= filled;
}

} // namespace lob
