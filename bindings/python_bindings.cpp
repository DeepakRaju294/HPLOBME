#include <pybind11/pybind11.h>

namespace py = pybind11;

// TODO(Milestone 6): expose MatchingEngine, NewOrder/CancelOrder/ReplaceOrder,
// Side, OrderType, TimeInForce, Trade, EngineEvent variants, MarketDataEvent,
// BookSnapshot. Bindings must preserve integer prices/quantities and must
// not duplicate matching logic in Python.
PYBIND11_MODULE(lob_engine, m) {
    m.doc() = "Python bindings for the C++ limit order book matching engine.";
}
