# Limit Order Book and Electronic Trading Simulator

**Status:** Draft v2
**Target duration:** 4–6 weeks part-time
**Primary languages:** C++20 and Python 3.11+
**Project type:** Trading systems, performance engineering, and quantitative simulation

---

## 1. Project Summary

Build a deterministic, performance-oriented single-instrument limit-order-book and matching engine in C++, expose it to Python, and use it to simulate an inventory-aware market-making strategy.

The project should demonstrate:

* Correct price-time-priority matching
* Efficient order insertion, matching, cancellation, and replacement
* Deterministic event processing and replay
* Sequenced market-data generation
* Profile-guided C++ performance optimization
* Comparison of alternative order-book data structures
* Python-based market simulation and strategy analysis
* Reproducible throughput and latency benchmarking
* Professional testing, documentation, and build tooling

The project is intended to demonstrate the engineering principles behind electronic trading systems rather than reproduce a production exchange.

---

# 2. Project Goals

## 2.1 Primary goals

1. Implement a deterministic C++ matching engine.
2. Support realistic core order lifecycle operations.
3. Preserve strict price-time priority.
4. Provide efficient order lookup and cancellation.
5. Generate structured execution and market-data events.
6. Support deterministic replay and state verification.
7. Establish a performance baseline and identify bottlenecks through profiling.
8. Implement at least two evidence-driven performance optimizations.
9. Connect the C++ engine to a Python simulation environment.
10. Evaluate a simple inventory-aware market-making strategy.
11. Produce reproducible throughput and tail-latency benchmarks.
12. Document architectural decisions and engineering tradeoffs.

## 2.2 Non-goals

The project will not include:

* Distributed matching
* Multiple exchange nodes
* Real exchange connectivity
* FIX protocol support
* Kernel-bypass networking
* Production fault tolerance
* Persistent database storage
* Regulatory reporting
* Auction logic
* Derivatives or multi-leg orders
* Hidden, iceberg, pegged, or discretionary orders
* Production-level nanosecond latency claims
* Complex quantitative strategy research

These may be discussed as future extensions but must not delay completion.

---

# 3. Success Criteria

## Correctness

* Limit orders execute using price-time priority.
* Marketable orders consume the best available prices first.
* Partial fills produce correct residual quantities.
* Cancelled orders cannot execute afterward.
* Replaced orders follow documented priority rules.
* Invalid commands cannot corrupt book state.
* Book invariants hold after every command.
* Deterministic replay reconstructs identical final state and trades.
* Market-data sequence numbers are strictly increasing.

## Performance

The final project must:

* Establish a reproducible baseline implementation.
* Measure median, p95, p99, and p99.9 operation latency.
* Measure total throughput under multiple workloads.
* Separate add, cancel, replace, and aggressive-match benchmarks.
* Compare at least two price-level representations.
* Perform profiling before optimization.
* Implement at least two optimizations justified by profiling.
* Report before-and-after performance results.

No fixed throughput number is required for completion.

Performance claims must be tied to documented hardware, workload, compiler, and configuration.

## Simulation

* Python can submit orders and receive engine events.
* Synthetic order flow is deterministic given a random seed.
* An inventory-aware market maker adjusts quotes based on inventory.
* Strategy analysis reports PnL, inventory risk, spread capture, and adverse selection.
* At least three market regimes are evaluated.

## Presentation

* Repository builds from documented commands.
* Automated tests execute from one command.
* Benchmark results are reproducible.
* README explains architecture, results, profiling findings, and limitations.
* Repository includes benchmark and strategy charts.

---

# 4. System Architecture

```text
                    Python Simulator
              order flow / market maker
                         |
                      pybind11
                         |
                         v
                Matching Engine
                 single-threaded
                         |
                         v
                Limit Order Book
              price-time priority
                 /            \
                /              \
      Execution Events      Market Data
                             sequence #
                             snapshots
                \              /
                 \            /
                  Replay / Analysis
```

The matching engine remains single-threaded and owns all mutable book state.

---

# 5. Component Responsibilities

## 5.1 Limit order book

Responsible for:

* Resting-order storage
* Sorted bid and ask price levels
* FIFO priority within each price level
* Order-ID lookup
* Cancellation and replacement
* Aggregate quantity
* Best bid/ask
* Book depth
* State hashing for replay validation

## 5.2 Matching engine

Responsible for:

* Command validation
* Matching decisions
* Trade generation
* Time-in-force behavior
* Order acknowledgments/rejections
* Market-data event generation
* Sequence-number assignment

## 5.3 Python simulator

Responsible for:

* Simulation clock
* Reference-price process
* External order-flow generation
* Market-maker execution
* Cash/inventory accounting
* Result collection

## 5.4 Benchmark harness

Responsible for:

* Deterministic workload generation
* Throughput measurement
* Operation-level latency
* Percentile distributions
* Baseline-versus-optimized comparisons

---

# 6. Repository Structure

```text
limit-order-book/
├── CMakeLists.txt
├── README.md
├── cpp/
│   ├── include/
│   │   ├── order.hpp
│   │   ├── trade.hpp
│   │   ├── events.hpp
│   │   ├── market_data.hpp
│   │   ├── price_level.hpp
│   │   ├── order_book.hpp
│   │   ├── matching_engine.hpp
│   │   ├── replay.hpp
│   │   └── invariants.hpp
│   ├── src/
│   ├── tests/
│   └── benchmarks/
├── bindings/
│   └── python_bindings.cpp
├── python/
│   ├── simulator/
│   ├── strategies/
│   ├── analysis/
│   └── tests/
├── configs/
├── scripts/
├── results/
└── docs/
    ├── architecture.md
    ├── matching_rules.md
    ├── benchmark_methodology.md
    └── performance_analysis.md
```

---

# 7. Domain Model

Use integer types for all exchange-critical values.

```cpp
using OrderId = std::uint64_t;
using TradeId = std::uint64_t;
using SequenceNumber = std::uint64_t;
using Timestamp = std::uint64_t;
using Quantity = std::uint64_t;
using Price = std::int64_t;
```

Prices must use integer ticks.

Example:

```text
$101.25 at a $0.01 tick size -> 10125
```

## Enumerations

```cpp
enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market
};

enum class TimeInForce {
    GoodTillCancel,
    ImmediateOrCancel,
    FillOrKill,
    PostOnly
};
```

---

# 8. Order-Book Data Structures

## 8.1 Baseline implementation

Use:

```cpp
std::map<Price, PriceLevel, std::greater<Price>> bids;
std::map<Price, PriceLevel, std::less<Price>> asks;
```

Each price level contains a FIFO structure of resting orders.

Maintain an order index:

```cpp
std::unordered_map<OrderId, OrderLocation> order_index;
```

This enables average O(1) lookup for cancellation and replacement.

## 8.2 Required optimized implementation

Implement a second bounded-tick representation using dense price levels.

Conceptually:

```cpp
std::vector<PriceLevel> levels;
```

indexed using:

```text
price -> price - minimum_tick
```

Benchmark this representation against the `std::map` implementation.

Analyze:

* Throughput
* Median latency
* p99 latency
* Cache behavior where measurable
* Memory overhead
* Sparse versus dense price ranges

The purpose is not to declare one universally superior, but to understand the tradeoff.

---

# 9. Order Storage and Allocation

The initial correct implementation may use standard containers.

After profiling, implement preallocated order storage or an object pool if allocation appears materially on the hot path.

Example:

```text
Preallocated Order Pool

[Order][Order][Order][Order][Order]
   ^                 ^
 active            free-list
```

Compare baseline versus pooled allocation.

Measure:

* Throughput
* p99/p99.9 latency
* Number of allocations where measurable

This optimization should only be implemented after baseline profiling.

---

# 10. Matching Rules

## Price priority

A buy order matches the lowest available ask first.

A sell order matches the highest available bid first.

## Time priority

Orders at the same price execute FIFO.

## Execution price

Trades execute at the resting order's price.

## Partial fills

Correctly update:

* Resting quantity
* Incoming quantity
* Price-level aggregate quantity
* Order index
* Trade events

## Supported time-in-force policies

Required:

* GTC
* IOC
* FOK
* Post-only

No additional order types are required.

---

# 11. Order Lifecycle

Support:

### New order

May produce:

* Accepted
* Rejected
* Trade
* Partial fill
* Filled
* Rested
* Cancelled residual

### Cancel

Must:

* Find order by ID
* Remove without scanning the book
* Update level quantity
* Delete empty levels
* Reject unknown IDs

### Replace

Priority rules:

* Same price + lower quantity preserves priority.
* Increased quantity loses priority.
* Changed price loses priority.
* Priority-losing replace behaves as atomic cancel-and-reinsert.

A price-changing replacement may immediately execute.

---

# 12. Engine Event Model

Use structured events.

```cpp
using EngineEvent = std::variant<
    OrderAccepted,
    OrderRejected,
    OrderRested,
    OrderPartiallyFilled,
    OrderFilled,
    OrderCancelled,
    OrderReplaced,
    TradeExecuted
>;
```

Event ordering must be deterministic.

Console output must never be part of engine behavior.

---

# 13. Market-Data Feed

The engine must additionally generate a sequenced market-data stream.

Required event types:

* Add
* Modify
* Delete
* Trade
* Snapshot

Each incremental event should include:

```cpp
SequenceNumber sequence;
Timestamp timestamp;
Side side;
Price price;
Quantity quantity;
```

as applicable.

Sequence numbers must be strictly increasing.

## 13.1 Snapshot support

Expose a deterministic depth snapshot containing:

* Sequence number
* Bid levels
* Ask levels

This creates the basic mechanism used by consumers to initialize or recover book state.

## 13.2 Optional gap-recovery demonstration

A small client may intentionally drop an incremental update:

```text
1051
1052
1054
```

detect the missing sequence number, request a snapshot, and resume from synchronized state.

This is a stretch feature, not required for project completion.

---

# 14. Public C++ API

```cpp
class MatchingEngine {
public:
    std::vector<EngineEvent> submit(const NewOrder&);
    std::vector<EngineEvent> cancel(const CancelOrder&);
    std::vector<EngineEvent> replace(const ReplaceOrder&);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;

    Quantity quantity_at_price(
        Side side,
        Price price
    ) const;

    BookSnapshot snapshot(
        std::size_t depth
    ) const;

    bool validate_invariants() const;

    std::uint64_t state_hash() const;
};
```

Core engine code must remain independent of Python.

---

# 15. Deterministic Replay

Provide a replay mechanism capable of reading previously submitted commands and reproducing engine state.

Replay must verify:

* Identical trades
* Identical event ordering
* Identical final book
* Identical state hash

Optionally record checkpoint hashes:

```text
sequence 10000 -> hash A
sequence 20000 -> hash B
sequence 30000 -> hash C
```

If replay diverges, report the earliest checkpoint or event where state differs.

Replay parsing time must not be included in matching-engine benchmarks.

---

# 16. Python Bindings

Use pybind11 to expose:

* MatchingEngine
* Order command types
* Side
* OrderType
* TimeInForce
* Trades
* Engine events
* Market-data events
* Best bid/ask
* Depth snapshots

Bindings must preserve integer prices and quantities.

Python must not duplicate matching logic.

---

# 17. Market Simulator

Use a discrete-event simulation.

At each simulation step:

1. Advance time.
2. Update reference price.
3. Generate external order flow.
4. Allow market maker to observe state.
5. Submit quote updates.
6. Process fills and events.
7. Update inventory and cash.
8. Record metrics.

## Required market regimes

Only four are required:

1. Baseline
2. High volatility
3. Directional imbalance
4. Sudden price shock

Each simulation accepts a random seed.

Same configuration + same seed + same code version must produce identical results.

---

# 18. Market-Making Strategy

Implement one primary inventory-aware strategy plus one simple baseline.

## Fixed-spread baseline

Quotes symmetrically around a midpoint.

## Inventory-aware strategy

Use:

```text
reservation price =
midprice - inventory_coefficient * inventory
```

and:

```text
bid = reservation price - half spread
ask = reservation price + half spread
```

Required controls:

* Maximum inventory
* Maximum order size
* Minimum spread
* Quote-refresh interval
* Quote suspension after configurable volatility threshold

The purpose is to demonstrate market-microstructure understanding, not optimize trading profitability.

---

# 19. Strategy Metrics

Track:

* Total PnL
* Maximum drawdown
* Final inventory
* Maximum absolute inventory
* Fill count
* Filled volume
* Average spread captured
* Adverse selection

Optional:

* PnL volatility
* Sharpe-like statistic
* Quote-to-fill ratio

Do not expand the project into extensive quantitative strategy research.

---

# 20. Testing Requirements

## Unit tests

Cover:

### Book behavior

* Bid/ask insertion
* Multiple levels
* FIFO ordering
* Aggregate quantities

### Matching

* Full fill
* Partial fill
* Multi-order sweep
* Multi-level sweep
* Maker-price execution

### Lifecycle

* Cancel first/middle/last order
* Unknown cancel
* Priority-preserving reduction
* Priority-losing replace
* Crossing replace

### Time in force

* IOC
* FOK
* Post-only

### Edge cases

* Duplicate ID
* Zero quantity
* Invalid price
* Empty book
* Large quantities
* Repeated cancel
* Replace after fill

## Property/randomized tests

Generate randomized command sequences and verify:

* Invariants never fail
* No duplicate active IDs
* No negative or zero resting quantities
* Book is not crossed after command completion
* Filled volume reconciles
* Aggregate level quantity equals contained quantity

## Sanitizers

Required:

* AddressSanitizer
* UndefinedBehaviorSanitizer

---

# 21. Required Invariants

After each command:

* Bids are strictly ordered highest to lowest.
* Asks are strictly ordered lowest to highest.
* Resting book is never crossed.
* Every active order appears exactly once.
* Every indexed order exists in the book.
* Every resting quantity is positive.
* Price-level quantity equals contained-order quantity.
* Empty price levels do not exist.
* Order IDs are unique.
* Market-data sequence numbers are monotonic.

Debug builds must expose invariant validation.

---

# 22. Benchmark Specification

## Required operations

Measure:

* Add at existing level
* Add at new level
* Cancel
* Replace
* Single-order match
* Multi-level sweep
* Mixed event stream

## Workloads

### Passive-heavy

```text
70% adds
25% cancels
5% aggressive
```

### Balanced

```text
50% adds
30% cancels
20% aggressive
```

### Match-heavy

```text
40% adds
20% cancels
40% aggressive
```

Three workload classes are sufficient.

## Active-order counts

Test approximately:

* 1,000
* 10,000
* 100,000

## Required measurements

* Events processed
* Events/second
* p50
* p95
* p99
* p99.9
* Active orders
* Price levels
* Workload configuration

Do not emphasize minimum or maximum latency because isolated observations are often noisy.

---

# 23. Benchmark Methodology

Benchmarks must:

* Use release optimization.
* Pre-generate workloads.
* Avoid allocation in event generation during timed sections.
* Avoid logging during timed sections.
* Warm up before measurement.
* Run multiple repetitions.
* Use deterministic seeds.
* Report hardware.
* Report OS.
* Report compiler and version.
* Report optimization flags.

Where practical:

* Pin benchmark process to a CPU core.
* Record CPU-frequency configuration.
* Minimize unrelated background workload.

These controls must be documented honestly rather than used to imply production-grade latency.

---

# 24. Performance Profiling and Optimization

This is a required project milestone.

## Phase 1 — baseline

Benchmark the simplest correct implementation.

## Phase 2 — profile

Use profiling tools such as:

* Linux `perf`
* Flamegraphs
* Cachegrind or equivalent

Identify the dominant bottlenecks.

Potential areas include:

* Tree traversal
* Pointer chasing
* Dynamic allocation
* Hash-table lookup
* Branch behavior

## Phase 3 — optimize

Implement at least two justified optimizations.

Preferred candidates:

1. Dense tick-indexed price levels
2. Preallocated order storage / object pool

Other optimizations may be used when profiling supports them.

## Phase 4 — compare

Produce results such as:

```text
                       Baseline    Optimized
Throughput               X           Y
p50 latency              X           Y
p99 latency              X           Y
p99.9 latency            X           Y
```

Also report relative change:

```text
throughput: +X%
p99 latency: -Y%
```

Only measured numbers may appear in final documentation.

---

# 25. Build and Tooling

## C++

* C++20
* CMake
* GCC or Clang
* GoogleTest or Catch2
* Google Benchmark or custom harness
* pybind11

## Python

* Python 3.11+
* NumPy
* Pandas
* Matplotlib
* PyYAML
* pytest

## Required commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build

python -m pytest python/tests
python scripts/run_simulation.py --config configs/baseline.yaml
bash scripts/run_benchmarks.sh
```

---

# 26. Continuous Integration

GitHub Actions should:

* Configure C++ build
* Compile engine
* Run C++ tests
* Run Python tests
* Run sanitizers

Performance benchmarks should not gate pull requests because shared CI hardware produces noisy measurements.

---

# 27. Documentation

README must include:

1. Project overview
2. Architecture diagram
3. Matching rules
4. Data structures
5. Event and market-data model
6. Build instructions
7. Testing methodology
8. Replay mechanism
9. Benchmark methodology
10. Baseline results
11. Profiling findings
12. Optimizations
13. Optimized results
14. Market-making experiment
15. Engineering tradeoffs
16. Limitations

## Performance document

`docs/performance_analysis.md` should tell a coherent story:

```text
baseline
   ↓
profile
   ↓
identified bottleneck
   ↓
design change
   ↓
benchmark
   ↓
measured improvement
```

This is a primary portfolio deliverable.

---

# 28. Required Charts

## Systems/performance

Required:

* Throughput versus active-order count
* p50/p95/p99 latency by operation
* Baseline versus optimized throughput
* Baseline versus optimized p99 latency
* `std::map` versus dense price-level representation

## Market-making

Required:

* PnL over time
* Inventory over time
* Reference price with bid/ask quotes

No additional charts are required.

---

# 29. Milestone Plan

## Milestone 1 — Core book

Build:

* Domain types
* Baseline map-based price levels
* FIFO orders
* Order index
* Add/cancel
* Best bid/ask
* Invariants

Exit gate:

* Unit tests pass.
* Invariant checks pass.

---

## Milestone 2 — Matching engine

Build:

* Limit orders
* Market orders
* Partial fills
* Trade events
* GTC
* IOC
* FOK
* Post-only
* Replace

Exit gate:

* Lifecycle tests pass.
* Randomized invariant tests pass.
* Sanitizers pass.

---

## Milestone 3 — Replay and market data

Build:

* Structured engine events
* Sequenced market-data events
* Snapshots
* Replay tool
* State hashing

Exit gate:

* Replay reproduces identical final state.
* Replay reproduces identical trades.
* Sequence numbers are deterministic.

---

## Milestone 4 — Baseline benchmarks

Build:

* Pre-generated workloads
* Operation benchmarks
* Percentile reporting
* Mixed workload benchmarks

Exit gate:

* Reproducible baseline results exist.
* Hardware/compiler metadata is recorded.

---

## Milestone 5 — Performance optimization

Build:

* Profiling workflow
* Dense price-level implementation
* At least one additional profile-guided optimization
* Preferably order pooling if justified

Exit gate:

* Baseline and optimized results are compared.
* Performance changes are measurable and documented.

---

## Milestone 6 — Python integration

Build:

* pybind11 module
* Python API
* Simulation event loop
* Synthetic external flow
* Deterministic seeds

Exit gate:

* Python simulation completes correctly.
* Fixed-seed runs reproduce.

---

## Milestone 7 — Market maker

Build:

* Fixed-spread baseline
* Inventory-aware strategy
* Inventory controls
* PnL accounting
* Adverse-selection metric

Exit gate:

* Accounting reconciles.
* Strategies run under all required regimes.

---

## Milestone 8 — Final presentation

Deliver:

* README
* Architecture diagram
* Performance analysis
* Benchmark charts
* Strategy charts
* Reproduction script

Exit gate:

A new user can:

```text
clone
build
test
replay
benchmark
simulate
```

using only the README.

---

# 30. Quality Gates

The project is not complete if:

* Matching is validated only through hand-written examples.
* Cancels scan the book.
* Floating-point prices are used.
* Resting order priority is ambiguous.
* The book can remain crossed.
* Replay is nondeterministic.
* Benchmarks include logging or workload generation.
* Performance claims omit workload/hardware information.
* Optimization occurs without a baseline.
* Optimization claims are made without measurement.
* Python duplicates C++ matching logic.
* Strategy PnL ignores remaining inventory.
* The README describes the project as production-ready.
* Synthetic benchmark results are presented as exchange-production performance.

---

# 31. Design Decisions

## Single-threaded matching engine

The matching engine processes commands sequentially.

Reasons:

* Deterministic ordering
* Clear ownership of mutable book state
* Easier correctness reasoning
* Avoid unnecessary synchronization in the hot path

Concurrency belongs around the matching engine rather than inside it.

## Integer prices

Prices are integer ticks to preserve exact ordering and equality.

## Baseline-first optimization

The first implementation prioritizes correctness and clarity.

Performance optimizations occur only after:

```text
correctness
    ↓
benchmark
    ↓
profile
    ↓
optimize
```

## Python simulation

Python handles strategy experimentation and analysis.

C++ handles matching, order-book state, event generation, and performance-sensitive work.

---

# 32. Stretch Extensions

Only begin after all core milestones are complete.

## Highest-value

* TCP order gateway
* Compact binary order protocol
* SPSC ring buffer between gateway and matching thread
* Incremental market-data client with gap detection
* Historical event-data replay
* CPU cycle-level benchmarking
* Additional cache/memory profiling

## Lower priority

* Multi-symbol support
* Self-trade prevention
* Binary replay log
* Custom allocator beyond basic object pooling
* Avellaneda-Stoikov quoting

## Explicitly deprioritized

* FIX
* Web dashboard
* Iceberg orders
* Auctions
* Persistent database
* Distributed exchange architecture

---

# 33. Final Demonstration

The final demonstration should show two separate stories.

## Part A — Trading system

1. Populate book.
2. Submit passive/aggressive orders.
3. Demonstrate partial fills.
4. Cancel and replace orders.
5. Show market-data sequence numbers.
6. Save event stream.
7. Replay stream.
8. Verify identical state hash.

## Part B — Performance

Show:

```text
baseline
vs
optimized
```

for:

* Throughput
* p50
* p99
* p99.9

Explain which bottlenecks were identified and why each optimization helped or did not help.

## Part C — Market maker

Run a brief strategy simulation showing:

* Reference price
* Bid/ask quotes
* Inventory
* PnL
* Response to a volatility shock

The entire demonstration should be runnable in several minutes.

---

# 34. Resume Deliverables

The project should ultimately support **two or three technically distinct resume bullets**, rather than one large description.

Example structure:

* Built a deterministic C++20 electronic matching engine implementing price-time priority, constant-time order cancellation, replacement semantics, multiple time-in-force policies, and sequenced market-data generation.
* Increased matching throughput by **X%** and reduced **p99 latency by Y%** through profile-guided optimization of price-level storage and order allocation, benchmarking workloads with up to **Z active orders**.
* Integrated the engine with a Python inventory-aware market-making simulator to measure PnL, inventory exposure, spread capture, and adverse selection across configurable market regimes.

Only measured figures may replace X, Y, and Z.

For trading-system-focused applications, bullets should appear in this order:

**matching architecture → performance optimization → quantitative simulation**

not:

**market-making strategy → matching engine → performance.**

---

# 35. Definition of Done

The project is complete when:

* Core matching behavior is correct.
* Order lifecycle operations work.
* Required time-in-force policies work.
* Randomized tests preserve invariants.
* Sanitizers pass.
* Structured execution events exist.
* Sequenced market-data events exist.
* Deterministic replay verifies state.
* Baseline benchmarks exist.
* Profiling identifies real bottlenecks.
* At least two performance improvements are investigated.
* At least one produces measured results worth discussing.
* Map and dense price representations are compared.
* Python bindings work.
* Inventory-aware market-making simulation works.
* Required market regimes are evaluated.
* Benchmark and strategy results are reproducible.
* README clearly distinguishes synthetic results from production exchange performance.

The central project story should be:

> **Build a correct matching engine, measure it, understand where it is slow, improve it, and then use it as the exchange core of a quantitative simulation.**

This is the version I'd build. The biggest change is that **the market-making portion is now deliberately smaller**, while profiling, memory/data-structure decisions, market-data sequencing, and deterministic replay become first-class requirements. That gives you much more to discuss with quant/fintech SWE recruiters without requiring you to build an entire production exchange.
