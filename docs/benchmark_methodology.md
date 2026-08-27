# Benchmark Methodology

The baseline benchmark suite lives in `cpp/benchmarks/` and is a **custom
harness**, not Google Benchmark. Spec section 25 permits either; a custom
harness was chosen because the two things spec section 22 actually
requires -- per-operation-type latency *percentiles* (p50/p95/p99/p99.9)
and *blended* mixed-workload streams with weighted operation types --
don't map cleanly onto Google Benchmark's per-repetition-aggregate
statistics model. Manually timing each pre-generated operation with
`std::chrono::steady_clock` and pooling the samples is more direct.

Run it with:

```bash
bash scripts/run_benchmarks.sh
```

(must be run from the repo root -- it writes `results/baseline_benchmark.csv`
relative to the current working directory).

## What's measured

Per spec section 22:

* **Six required operations, in isolation:** add at an existing level, add
  at a new level, cancel, replace, single-order match, multi-level sweep.
* **One "mixed event stream" operation**, run across the three required
  workload classes (passive-heavy 70/25/5, balanced 50/30/20, match-heavy
  40/20/40).
* Each of the above at three active-order-count targets: ~1,000, ~10,000,
  ~100,000.

That's 6x3 + 1x3x3 = 27 benchmark configurations per run, each reporting:
events processed, events/second, p50/p95/p99/p99.9 (nanoseconds), the
actual active-order count and price-level count at the end of the run,
and the workload configuration.

## Workload generation (`cpp/benchmarks/workload.cpp`)

Every workload is fully pre-generated -- both the untimed "seed" commands
that build up realistic book state and the "timed" commands actually
measured -- **before** any timing starts, using a seeded `std::mt19937_64`
(default seed 42) so a given (operation, active-order target) pair
produces the exact same command sequence on every run. No allocation or
random-number generation happens inside the timed loop.

**Single-operation benchmarks** seed a book (two-sided for
add/cancel/replace; one resting order per price level, sell-side only,
for single-match/sweep) and then time a batch of operations of exactly
one kind. To keep the reported "active orders"/"price levels" figures
representative of the target throughout the run, cancel/replace/
single-match/sweep never consume more than 25% of the seeded book in one
run.

**The mixed-event-stream benchmark** needs Add, Cancel, and
aggressive-match operations to all target *valid* state without the
generator re-implementing the matching engine to track exactly what an
aggressive fill consumes. It does this with two disjoint, non-overlapping
price pools seeded around a fixed mid-price:

* A **live pool** (the book Add/Cancel actually act on) at the outer
  price range.
* A **sacrificial pool** -- extra resting liquidity seeded at the inner
  (best) price range, sized at ~4x the expected aggressive-order demand
  -- that generated aggressive orders always cross into first, since
  price-time priority walks from the best price outward. The live pool's
  IDs (the only ones a generated Cancel ever targets) are therefore never
  invalidated by a match generated elsewhere in the same sequence.

This is a deliberate simplification: real-world matching hits whatever's
resting at the best price regardless of how it got there, and a truly
faithful generator would need to simulate the engine while generating
its workload. The two-pool approach keeps generation a single linear
pass while still producing a valid, engine-accepted command stream.

## Timing methodology

* **Release build** (`-DCMAKE_BUILD_TYPE=Release`), i.e. `-O2`/`-O3` per
  compiler default.
* **Warm-up:** 1 full repetition (seed + timed ops) run and discarded
  before any measured repetition.
* **Repetitions:** 5 measured repetitions per configuration. Each
  repetition seeds a **fresh** `MatchingEngine` so no shared state leaks
  between repetitions. Per-operation latencies from all 5 repetitions are
  pooled before computing percentiles, giving p99.9 enough samples (up to
  10,000 for a 2,000-op benchmark) to be meaningful rather than a single
  noisy outlier.
* **Timed region:** exactly one `engine.submit/cancel/replace()` call per
  timed sample, bracketed by `std::chrono::steady_clock::now()`. Nothing
  else (no logging, no allocation, no market-data draining) happens
  inside that bracket -- `drain_market_data()` is only called during
  untimed seeding, to keep the queue from growing unbounded, never during
  the timed loop.
* **Seeds:** fixed (`seed = 42` for workload generation) so results are
  reproducible run-to-run on the same machine/build.

## Environment for the committed baseline (`results/baseline_benchmark.csv`)

* **Hardware:** 12th Gen Intel Core i7-1255U (10 cores / 12 logical
  processors, 2.60 GHz base clock as reported by Windows).
* **OS:** Windows 11 Home (build 10.0.26200).
* **Compiler:** MSVC 19.29 (Visual Studio 2019 Build Tools), `/std:c++20`,
  Release configuration.
* **Not controlled for:** this is a developer laptop, not an isolated
  benchmark host. The process was **not** pinned to a specific CPU core,
  CPU frequency scaling/turbo boost was **not** disabled, and ordinary
  background OS/application load was present during the run. The wide
  p99.9 tails visible in `results/baseline_benchmark.csv` (particularly
  for `multi_level_sweep` at the 100,000-order target) are consistent
  with that lack of isolation, not necessarily with the engine itself --
  see the honesty requirement in spec section 23. These figures
  characterize this implementation's *relative* behavior (e.g. sweep
  costing more than a single match, larger books costing more than
  smaller ones) and are not a production latency claim.

## What Milestone 5 will add

Milestone 5 introduces the dense tick-indexed price-level representation
and at least one more profile-guided optimization, then re-runs this same
harness against the optimized build for a documented before/after
comparison (`docs/performance_analysis.md`).
