# Limit Order Book and Electronic Trading Simulator

A deterministic, performance-oriented single-instrument limit-order-book
and matching engine in C++20, exposed to Python via pybind11, used to
simulate an inventory-aware market-making strategy.

This project demonstrates the engineering principles behind electronic
trading systems — correct price-time-priority matching, deterministic
event processing and replay, sequenced market data, profile-guided
performance optimization, and Python-based strategy simulation — rather
than reproducing a production exchange. See `docs/` for the full design
spec and rationale.

**Status:** scaffolding only. Milestone 1 (core order book) has not
started yet — see [Milestones](#milestones) below.

## Architecture

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

The matching engine remains single-threaded and owns all mutable book
state. See `docs/architecture.md` for details.

## Repository layout

```text
cpp/include/     domain types, book, engine, replay, invariants (headers)
cpp/src/         implementations (Milestone 1+)
cpp/tests/       GoogleTest unit + property tests
cpp/benchmarks/  Google Benchmark harness
bindings/        pybind11 module
python/          simulator, strategies, analysis, tests
configs/         market-regime configs (baseline, high-vol, imbalance, shock)
scripts/         run_simulation.py, run_benchmarks.sh
results/         benchmark/strategy outputs (charts, raw data)
docs/            architecture, matching rules, benchmark methodology, perf analysis
```

## Build

Requires CMake 3.20+, a C++20 compiler, and network access on first
configure (GoogleTest/Google Benchmark/pybind11 are fetched via
`FetchContent`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

To build the Python bindings as well:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLOB_BUILD_PYTHON_BINDINGS=ON
cmake --build build
```

Sanitizer builds (required before merging matching-engine changes):

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLOB_ENABLE_ASAN=ON -DLOB_ENABLE_UBSAN=ON
cmake --build build-asan
ctest --test-dir build-asan
```

## Python

```bash
pip install -r requirements.txt
python -m pytest python/tests
python scripts/run_simulation.py --config configs/baseline.yaml
```

## Benchmarks

```bash
bash scripts/run_benchmarks.sh
```

See `docs/benchmark_methodology.md` for methodology and controls, and
`docs/performance_analysis.md` for the baseline -> profile -> optimize ->
measure story once Milestone 5 lands.

## Milestones

1. Core book (domain types, baseline map-based price levels, FIFO,
   order index, add/cancel, best bid/ask, invariants)
2. Matching engine (limit/market orders, partial fills, trades, GTC/IOC/
   FOK/PostOnly, replace)
3. Replay and market data (structured events, sequenced market data,
   snapshots, replay tool, state hashing)
4. Baseline benchmarks
5. Performance optimization (dense price levels, order pooling, profiling)
6. Python integration (pybind11, simulation event loop, deterministic seeds)
7. Market maker (fixed-spread baseline, inventory-aware strategy, PnL/
   inventory/adverse-selection accounting)
8. Final presentation (README, charts, reproduction script)

## Non-goals

Distributed matching, multiple exchange nodes, real exchange connectivity,
FIX protocol, kernel-bypass networking, production fault tolerance,
persistent database storage, regulatory reporting, auctions, derivatives/
multi-leg orders, hidden/iceberg/pegged/discretionary orders, and
production-level nanosecond latency claims are explicitly out of scope.

## Limitations

This is a synthetic, educational system. Benchmark numbers reflect a
specific workload/hardware/compiler configuration documented alongside
each result, not exchange-production performance. See
`docs/performance_analysis.md` and `docs/benchmark_methodology.md` once
populated.
