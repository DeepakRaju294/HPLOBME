// pybind11 bindings for the C++ matching engine (spec section 16).
//
// Exposes MatchingEngine (the pooled std::map-based instantiation --
// DenseMatchingEngine is a C++-internal performance comparison, not part
// of the Python-facing simulation surface), the three command types,
// every EngineEvent alternative, Trade, MarketDataEvent, and BookSnapshot.
// Integer prices/quantities are preserved as Python ints throughout --
// nothing here introduces floating point into the matching path. Python
// calls into this module for every matching decision; it must never
// reimplement matching logic itself (spec section 16).
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <sstream>

#include "events.hpp"
#include "market_data.hpp"
#include "matching_engine.hpp"
#include "order.hpp"
#include "trade.hpp"

namespace py = pybind11;
using namespace lob;

namespace {

std::string side_repr(Side side) {
    return side == Side::Buy ? "Side.Buy" : "Side.Sell";
}

template <typename EventT>
std::string simple_event_repr(const char* name, OrderId order_id, Timestamp timestamp) {
    std::ostringstream out;
    out << name << "(order_id=" << order_id << ", timestamp=" << timestamp << ")";
    return out.str();
}

} // namespace

PYBIND11_MODULE(lob_engine, m) {
    m.doc() = "Python bindings for the C++ limit order book matching engine.";

    // --- Enums ---

    py::enum_<Side>(m, "Side")
        .value("Buy", Side::Buy)
        .value("Sell", Side::Sell);

    py::enum_<OrderType>(m, "OrderType")
        .value("Limit", OrderType::Limit)
        .value("Market", OrderType::Market);

    py::enum_<TimeInForce>(m, "TimeInForce")
        .value("GoodTillCancel", TimeInForce::GoodTillCancel)
        .value("ImmediateOrCancel", TimeInForce::ImmediateOrCancel)
        .value("FillOrKill", TimeInForce::FillOrKill)
        .value("PostOnly", TimeInForce::PostOnly);

    py::enum_<RejectReason>(m, "RejectReason")
        .value("ZeroQuantity", RejectReason::ZeroQuantity)
        .value("DuplicateOrderId", RejectReason::DuplicateOrderId)
        .value("InvalidPrice", RejectReason::InvalidPrice)
        .value("InvalidTimeInForceForOrderType", RejectReason::InvalidTimeInForceForOrderType)
        .value("UnknownOrderId", RejectReason::UnknownOrderId)
        .value("WouldCross", RejectReason::WouldCross)
        .value("FillOrKillUnfillable", RejectReason::FillOrKillUnfillable);

    py::enum_<MarketDataEventType>(m, "MarketDataEventType")
        .value("Add", MarketDataEventType::Add)
        .value("Modify", MarketDataEventType::Modify)
        .value("Delete", MarketDataEventType::Delete)
        .value("Trade", MarketDataEventType::Trade)
        .value("Snapshot", MarketDataEventType::Snapshot);

    // --- Commands ---

    py::class_<NewOrder>(m, "NewOrder")
        .def(py::init([](OrderId id, Side side, Quantity quantity, Price price, OrderType type,
                          TimeInForce time_in_force, Timestamp timestamp) {
                 NewOrder cmd{};
                 cmd.id = id;
                 cmd.side = side;
                 cmd.type = type;
                 cmd.time_in_force = time_in_force;
                 cmd.price = price;
                 cmd.quantity = quantity;
                 cmd.timestamp = timestamp;
                 return cmd;
             }),
             py::arg("id"), py::arg("side"), py::arg("quantity"), py::arg("price") = 0,
             py::arg("type") = OrderType::Limit, py::arg("time_in_force") = TimeInForce::GoodTillCancel,
             py::arg("timestamp") = 0)
        .def_readwrite("id", &NewOrder::id)
        .def_readwrite("side", &NewOrder::side)
        .def_readwrite("type", &NewOrder::type)
        .def_readwrite("time_in_force", &NewOrder::time_in_force)
        .def_readwrite("price", &NewOrder::price)
        .def_readwrite("quantity", &NewOrder::quantity)
        .def_readwrite("timestamp", &NewOrder::timestamp)
        .def("__repr__", [](const NewOrder& o) {
            std::ostringstream out;
            out << "NewOrder(id=" << o.id << ", side=" << side_repr(o.side) << ", quantity=" << o.quantity
                << ", price=" << o.price << ", timestamp=" << o.timestamp << ")";
            return out.str();
        });

    py::class_<CancelOrder>(m, "CancelOrder")
        .def(py::init([](OrderId id, Timestamp timestamp) {
                 CancelOrder cmd{};
                 cmd.id = id;
                 cmd.timestamp = timestamp;
                 return cmd;
             }),
             py::arg("id"), py::arg("timestamp") = 0)
        .def_readwrite("id", &CancelOrder::id)
        .def_readwrite("timestamp", &CancelOrder::timestamp)
        .def("__repr__", [](const CancelOrder& o) {
            std::ostringstream out;
            out << "CancelOrder(id=" << o.id << ", timestamp=" << o.timestamp << ")";
            return out.str();
        });

    py::class_<ReplaceOrder>(m, "ReplaceOrder")
        .def(py::init([](OrderId id, Price new_price, Quantity new_quantity, Timestamp timestamp) {
                 ReplaceOrder cmd{};
                 cmd.id = id;
                 cmd.new_price = new_price;
                 cmd.new_quantity = new_quantity;
                 cmd.timestamp = timestamp;
                 return cmd;
             }),
             py::arg("id"), py::arg("new_price"), py::arg("new_quantity"), py::arg("timestamp") = 0)
        .def_readwrite("id", &ReplaceOrder::id)
        .def_readwrite("new_price", &ReplaceOrder::new_price)
        .def_readwrite("new_quantity", &ReplaceOrder::new_quantity)
        .def_readwrite("timestamp", &ReplaceOrder::timestamp)
        .def("__repr__", [](const ReplaceOrder& o) {
            std::ostringstream out;
            out << "ReplaceOrder(id=" << o.id << ", new_price=" << o.new_price
                << ", new_quantity=" << o.new_quantity << ", timestamp=" << o.timestamp << ")";
            return out.str();
        });

    // --- Trade and engine events (spec section 12) ---

    py::class_<Trade>(m, "Trade")
        .def_readonly("id", &Trade::id)
        .def_readonly("aggressor_order_id", &Trade::aggressor_order_id)
        .def_readonly("resting_order_id", &Trade::resting_order_id)
        .def_readonly("aggressor_side", &Trade::aggressor_side)
        .def_readonly("price", &Trade::price)
        .def_readonly("quantity", &Trade::quantity)
        .def_readonly("timestamp", &Trade::timestamp)
        .def_readonly("sequence", &Trade::sequence)
        .def("__repr__", [](const Trade& t) {
            std::ostringstream out;
            out << "Trade(id=" << t.id << ", aggressor_order_id=" << t.aggressor_order_id
                << ", resting_order_id=" << t.resting_order_id << ", price=" << t.price
                << ", quantity=" << t.quantity << ", sequence=" << t.sequence << ")";
            return out.str();
        });

    py::class_<OrderAccepted>(m, "OrderAccepted")
        .def_readonly("order_id", &OrderAccepted::order_id)
        .def_readonly("timestamp", &OrderAccepted::timestamp)
        .def("__repr__",
             [](const OrderAccepted& e) { return simple_event_repr<OrderAccepted>("OrderAccepted", e.order_id, e.timestamp); });

    py::class_<OrderRejected>(m, "OrderRejected")
        .def_readonly("order_id", &OrderRejected::order_id)
        .def_readonly("reason", &OrderRejected::reason)
        .def_readonly("timestamp", &OrderRejected::timestamp)
        .def("__repr__", [](const OrderRejected& e) {
            std::ostringstream out;
            out << "OrderRejected(order_id=" << e.order_id << ", timestamp=" << e.timestamp << ")";
            return out.str();
        });

    py::class_<OrderRested>(m, "OrderRested")
        .def_readonly("order_id", &OrderRested::order_id)
        .def_readonly("price", &OrderRested::price)
        .def_readonly("quantity", &OrderRested::quantity)
        .def_readonly("timestamp", &OrderRested::timestamp)
        .def("__repr__", [](const OrderRested& e) {
            std::ostringstream out;
            out << "OrderRested(order_id=" << e.order_id << ", price=" << e.price << ", quantity=" << e.quantity
                << ", timestamp=" << e.timestamp << ")";
            return out.str();
        });

    py::class_<OrderPartiallyFilled>(m, "OrderPartiallyFilled")
        .def_readonly("order_id", &OrderPartiallyFilled::order_id)
        .def_readonly("filled_quantity", &OrderPartiallyFilled::filled_quantity)
        .def_readonly("remaining_quantity", &OrderPartiallyFilled::remaining_quantity)
        .def_readonly("timestamp", &OrderPartiallyFilled::timestamp)
        .def("__repr__", [](const OrderPartiallyFilled& e) {
            std::ostringstream out;
            out << "OrderPartiallyFilled(order_id=" << e.order_id << ", filled_quantity=" << e.filled_quantity
                << ", remaining_quantity=" << e.remaining_quantity << ")";
            return out.str();
        });

    py::class_<OrderFilled>(m, "OrderFilled")
        .def_readonly("order_id", &OrderFilled::order_id)
        .def_readonly("timestamp", &OrderFilled::timestamp)
        .def("__repr__",
             [](const OrderFilled& e) { return simple_event_repr<OrderFilled>("OrderFilled", e.order_id, e.timestamp); });

    py::class_<OrderCancelled>(m, "OrderCancelled")
        .def_readonly("order_id", &OrderCancelled::order_id)
        .def_readonly("timestamp", &OrderCancelled::timestamp)
        .def("__repr__", [](const OrderCancelled& e) {
            return simple_event_repr<OrderCancelled>("OrderCancelled", e.order_id, e.timestamp);
        });

    py::class_<OrderReplaced>(m, "OrderReplaced")
        .def_readonly("order_id", &OrderReplaced::order_id)
        .def_readonly("new_price", &OrderReplaced::new_price)
        .def_readonly("new_quantity", &OrderReplaced::new_quantity)
        .def_readonly("priority_preserved", &OrderReplaced::priority_preserved)
        .def_readonly("timestamp", &OrderReplaced::timestamp)
        .def("__repr__", [](const OrderReplaced& e) {
            std::ostringstream out;
            out << "OrderReplaced(order_id=" << e.order_id << ", new_price=" << e.new_price
                << ", new_quantity=" << e.new_quantity << ", priority_preserved=" << (e.priority_preserved ? "True" : "False")
                << ")";
            return out.str();
        });

    py::class_<TradeExecuted>(m, "TradeExecuted")
        .def_readonly("trade", &TradeExecuted::trade)
        .def("__repr__", [](const TradeExecuted& e) {
            std::ostringstream out;
            out << "TradeExecuted(trade=Trade(id=" << e.trade.id << ", price=" << e.trade.price
                << ", quantity=" << e.trade.quantity << "))";
            return out.str();
        });

    // --- Market data (spec section 13) ---

    py::class_<MarketDataEvent>(m, "MarketDataEvent")
        .def_readonly("sequence", &MarketDataEvent::sequence)
        .def_readonly("timestamp", &MarketDataEvent::timestamp)
        .def_readonly("type", &MarketDataEvent::type)
        .def_readonly("side", &MarketDataEvent::side)
        .def_readonly("price", &MarketDataEvent::price)
        .def_readonly("quantity", &MarketDataEvent::quantity)
        .def("__repr__", [](const MarketDataEvent& e) {
            std::ostringstream out;
            out << "MarketDataEvent(sequence=" << e.sequence << ", price=" << e.price << ", quantity=" << e.quantity
                << ")";
            return out.str();
        });

    py::class_<PriceLevelView>(m, "PriceLevelView")
        .def_readonly("price", &PriceLevelView::price)
        .def_readonly("quantity", &PriceLevelView::quantity)
        .def("__repr__", [](const PriceLevelView& v) {
            std::ostringstream out;
            out << "PriceLevelView(price=" << v.price << ", quantity=" << v.quantity << ")";
            return out.str();
        });

    py::class_<BookSnapshot>(m, "BookSnapshot")
        .def_readonly("sequence", &BookSnapshot::sequence)
        .def_readonly("timestamp", &BookSnapshot::timestamp)
        .def_readonly("bids", &BookSnapshot::bids)
        .def_readonly("asks", &BookSnapshot::asks);

    // --- The engine itself ---

    py::class_<MatchingEngine>(m, "MatchingEngine")
        .def(py::init<>())
        .def("submit", &MatchingEngine::submit, py::arg("cmd"))
        .def("cancel", &MatchingEngine::cancel, py::arg("cmd"))
        .def("replace", &MatchingEngine::replace, py::arg("cmd"))
        .def("drain_market_data", &MatchingEngine::drain_market_data)
        .def("best_bid", &MatchingEngine::best_bid)
        .def("best_ask", &MatchingEngine::best_ask)
        .def("quantity_at_price", &MatchingEngine::quantity_at_price, py::arg("side"), py::arg("price"))
        .def("active_order_count", &MatchingEngine::active_order_count)
        .def("snapshot", &MatchingEngine::snapshot, py::arg("depth"))
        .def("validate_invariants", &MatchingEngine::validate_invariants)
        .def("state_hash", &MatchingEngine::state_hash);
}
