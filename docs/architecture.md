# Architecture

TODO: fill in as milestones land. See the project spec for the target
system diagram (Python simulator -> pybind11 -> MatchingEngine ->
OrderBook -> execution events / sequenced market data -> replay & analysis)
and component responsibilities (limit order book, matching engine, Python
simulator, benchmark harness).

## Sections to complete

- Overview
- Component responsibilities
- Data flow (order submission -> matching -> events -> market data)
- Threading model (single-threaded matching engine; where concurrency is
  allowed to live around it)
- Domain model (integer prices/ticks, event types)
