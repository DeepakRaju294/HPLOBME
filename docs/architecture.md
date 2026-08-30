# Architecture

See [spec.md](spec.md) for the full original design spec and rationale.
This document tracks what's actually built, updated as milestones land.

## Overview

```text
                    Python Simulator          (Milestone 6/7)
              order flow / market maker
                         |
                      pybind11                (Milestone 6)
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
      (EngineEvent,          (MarketDataEvent,
       synchronous)           sequenced, buffered)
                \              /
                 \            /
                  Replay / Analysis
```

## Component responsibilities

* **`PriceLevel`** (`cpp/include/price_level.hpp`) -- a FIFO queue
  (`std::list<Order, PoolAllocator<Order>>`) of resting orders at a
  single price. `std::list`, not `std::deque`/`vector`, so an
  `OrderLocation` can hold a stable iterator into it, giving O(1) cancel
  without scanning. Backed by `PoolAllocator` (`cpp/include/object_pool.hpp`)
  rather than the default allocator -- profiling showed per-order node
  malloc/free dominating the matching hot path; see
  `docs/performance_analysis.md`.
* **`OrderBook`** (`cpp/include/order_book.hpp`) -- the baseline
  `std::map<Price, PriceLevel>` book (spec section 8.1), plus an
  `unordered_map<OrderId, OrderLocation>` index. Pure resting-order
  storage: it does not match or validate commands, only performs the
  mutations `MatchingEngine` decides on (`add_order`, `cancel_order`,
  `fill_best_order`, `reduce_quantity_in_place`, `matchable_quantity`,
  `find_order`). Also owns invariant validation and state hashing.
* **`DenseOrderBook`** (`cpp/include/dense_order_book.hpp`) -- the dense
  tick-indexed alternative (spec section 8.2): `std::vector<PriceLevel>`
  indexed by `price - min_price`, with a cached best-index per side. Same
  public interface as `OrderBook`, so `MatchingEngineT<BookType>` (below)
  can use either interchangeably. Benchmarked against `OrderBook` in
  `docs/performance_analysis.md` -- faster for add/replace/mixed
  workloads, slower for match-heavy sweeps over sparse-relative-to-range
  books, a tradeoff rather than a strict win.
* **`MatchingEngineT<BookType>`** (`cpp/include/matching_engine.hpp`) --
  the matching engine is a class template, explicitly instantiated as
  `MatchingEngine` (`= MatchingEngineT<OrderBook>`, the default/production
  instantiation) and `DenseMatchingEngine`
  (`= MatchingEngineT<DenseOrderBook>`). Both run identical matching logic;
  only the underlying price-level storage differs, which is what makes
  the correctness-parity tests (`matching_engine_test.cpp`'s `TYPED_TEST`s)
  and the performance comparison meaningful. This is the only component
  that makes matching decisions, generates trades, enforces
  time-in-force semantics, and assigns market-data sequence numbers. See
  `docs/matching_rules.md` for the exact rules.
* **`invariants.hpp`/`.cpp`** -- the book-state checks from spec section
  21, run via `OrderBook::validate_invariants()`.
* **`replay.hpp`/`.cpp`** -- `CommandLogWriter` (records a command
  sequence plus its outcome) and `replay_from_file` (verifies a fresh
  engine reproduces that outcome exactly). See "Replay" below.

## Data flow

1. A caller submits a `NewOrder`/`CancelOrder`/`ReplaceOrder` to
   `MatchingEngine`.
2. The engine validates it, matches it against `OrderBook` as needed, and
   returns a `std::vector<EngineEvent>` synchronously -- the order
   lifecycle outcome for *that specific command* (accepted, rejected,
   trades, fills, rested, cancelled, replaced).
3. Every book mutation along the way also appends to an internal
   `market_data_queue_`, drained on demand via
   `MatchingEngine::drain_market_data()`. This is deliberately a separate
   stream from `EngineEvent`: the public market-data feed is not the same
   thing as the private acknowledgment returned to whoever submitted the
   command (spec section 13 vs. section 12).

## Market-data model

Required event types per spec section 13: Add, Modify, Delete, Trade,
Snapshot. Every `MarketDataEvent` carries a strictly-increasing
`sequence` (one global counter per engine, incremented for every event
regardless of type) and a `timestamp`.

**Design decision:** Add/Modify/Delete describe **price-level (L2)
aggregate quantity**, not individual order quantities -- consistent with
what `snapshot()` reports, since a consumer must be able to apply the
incremental feed on top of a snapshot and get the same numbers either
way. Concretely:

* **Add** -- a price level went from not-existing to existing;
  `quantity` = the level's new aggregate.
* **Modify** -- an existing level's aggregate changed (an order joined,
  a partial fill/cancel/in-place-replace shrank it); `quantity` = the
  level's new aggregate.
* **Delete** -- a level's aggregate hit zero and it was removed;
  `quantity` = 0.
* **Trade** -- `side` = aggressor side, `price`/`quantity` = the
  execution.

A single command can (and often does) produce several market-data events
-- e.g. a sweep across three price levels emits three Trade events
interleaved with three Modify/Delete events, one pair per level consumed.

`Trade.sequence` (on the `Trade` struct embedded in the synchronous
`TradeExecuted` engine event) is stamped with the same sequence number as
its corresponding market-data Trade event, so the two streams can be
cross-referenced.

`MatchingEngine::snapshot(depth)` stamps its `sequence` with the last
market-data sequence number emitted so far (0 if none yet) -- the
mechanism a consumer uses for gap recovery (spec section 13.2): detect a
missing sequence number in the incremental feed, request a snapshot, and
resume listening for sequence numbers strictly greater than the
snapshot's.

## Replay

`CommandLogWriter` appends each submitted command to a deterministic,
pipe-delimited, line-oriented text log (see the format comment at the top
of `cpp/src/replay.cpp`), then `finish()` appends the run's actual
outcome: every trade produced, in order, plus the final `state_hash()`.

`replay_from_file()` reads that log, replays every command through a
**fresh** `MatchingEngine`, and compares what it produces against the
recorded outcome -- same trades (id, price, quantity, sequence, etc., in
the same order) and the same final state hash. A `ReplayResult` reports
`matched` plus, on failure, the `divergent_sequence` (the first
mismatched trade's sequence number, or 0 if only the final hash
differed) and a human-readable `detail`.

This is a correctness/test tool, not a performance one -- log parsing
time is never included in matching-engine benchmarks (spec section 15).
Checkpoint hashes at intervals (spec section 15, optional) are not
implemented; full-run comparison was sufficient to meet the exit gate.

## Threading model

The matching engine is single-threaded: commands are processed strictly
sequentially, and `OrderBook`/`MatchingEngine` state is never accessed
concurrently. This is a deliberate simplicity/correctness tradeoff (spec
section 31) -- concurrency, if ever needed, belongs around the engine
(e.g. a single-writer queue feeding it), not inside it.

## Domain model

Integer types throughout for all exchange-critical values (`Price` as
integer ticks, `Quantity`, `OrderId`, `SequenceNumber`, `Timestamp` --
see `cpp/include/order.hpp`). No floating-point prices anywhere in the
matching path.
