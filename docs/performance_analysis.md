# Performance Analysis

This is the Milestone 5 story: baseline, profile, identify the bottleneck,
change the design, measure, and report the result honestly -- including
where the "optimized" representation turned out to be a genuine tradeoff
rather than a strict win.

```text
baseline (Milestone 4)
   -> profile (valgrind callgrind + cachegrind)
   -> identified bottleneck (malloc/free + red-black tree cost)
   -> design change #1: preallocated order pool
   -> design change #2: dense tick-indexed price levels
   -> benchmark (same 27-config harness, same seeds)
   -> measured improvement -- and one measured regression, explained
```

All numbers below are measured on this machine (see environment block)
using the exact reproduction commands given in each section. Nothing here
is a production-exchange latency claim -- see
`docs/benchmark_methodology.md` for the controls this environment does
and does not have.

## Phase 1: baseline

The Milestone 4 baseline (`results/baseline_benchmark.csv`) used the
std::map-based `OrderBook` with no pooling: `PriceLevel`'s internal
`std::list<Order>` used the default allocator, so every order insert and
removal round-tripped through the general-purpose heap allocator.

## Phase 2: profile

Reproduction:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target lob_profile_driver
valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./build-linux/cpp/benchmarks/lob_profile_driver
callgrind_annotate --threshold=99 callgrind.out
valgrind --tool=cachegrind --cachegrind-out-file=cachegrind.out ./build-linux/cpp/benchmarks/lob_profile_driver
```

`lob_profile_driver` runs one large, representative match-heavy mixed
workload (50,000 seeded orders, 20,000 timed operations) through a single
`MatchingEngine` in one pass -- deliberately separate from `lob_benchmarks`
(27 configs x 6 repetitions), since callgrind/cachegrind instrumentation
carries 10-50x overhead. Full output saved in `results/profiling/`.

**Finding: malloc/free machinery dominated the profile.**

From `results/profiling/callgrind_baseline_top_functions.txt` (856M
instructions total):

| Function | Instructions | Share |
|---|---:|---:|
| `_int_free` | 110,917,777 | 12.95% |
| `_int_malloc` | 98,412,076 | 11.49% |
| `malloc` | 65,565,273 | 7.66% |
| `malloc_consolidate` | 36,817,842 | 4.30% |
| `free` | 33,196,422 | 3.88% |
| `unlink_chunk` | 15,336,905 | 1.79% |
| **malloc/free subtotal** | **360,246,295** | **~42.1%** |

Nearly **42% of all instructions executed were inside the memory
allocator**, not the matching logic itself. The next-largest contributors
were `std::map`/red-black-tree operations: `OrderBook::add_order`
(9.23%, includes the tree lookup/insert), `OrderBook::quantity_at_price`
(6.19% -- called repeatedly by the market-data before/after diffing in
`rest_order`/`cancel_and_publish`/`walk_match`, each call a fresh O(log
levels) tree traversal), the two `_Rb_tree::_M_emplace_unique`
instantiations for `bids_`/`asks_` (3.30% each), and
`_Rb_tree_insert_and_rebalance` (2.20%).

This matches spec section 24's candidate bottleneck list almost exactly:
**dynamic allocation** was the dominant cost, with **tree traversal**
(pointer-chasing through red-black tree nodes) a clear secondary cost.
This directly motivates the two changes below, in priority order.

## Phase 3: optimizations

### Optimization 1 -- preallocated order pool (`cpp/include/object_pool.hpp`)

The malloc/free cost traced to `PriceLevel`'s `std::list<Order>`: every
`push_back`/`erase` allocated or freed one list node. `PoolAllocator<T>`
is a free-list allocator (spec section 9's "preallocated order pool"
diagram) that services those same allocation requests, reusing a shared
pool of pre-sized blocks instead of round-tripping through the general
allocator. `PriceLevel::OrderList` is now
`std::list<Order, PoolAllocator<Order>>` -- a one-line type change, no
logic change, since `PoolAllocator` is a conforming `std::allocator`
replacement.

### Optimization 2 -- dense tick-indexed price levels (`cpp/include/dense_order_book.hpp`)

`DenseOrderBook` replaces `std::map<Price, PriceLevel>` with
`std::vector<PriceLevel>` indexed by `price - min_price`, per spec
section 8.2: O(1) level lookup instead of O(log levels) tree traversal.
Finding the *best* price (not just a known one) needs its own mechanism
in a flat array, so it maintains a cached best-index per side, updated in
O(1) on insert and by scanning outward past now-empty slots when the
current best is removed.

`MatchingEngine` was turned into a class template,
`MatchingEngineT<BookType>`, explicitly instantiated for both `OrderBook`
and `DenseOrderBook` (aliased as `MatchingEngine` and
`DenseMatchingEngine`) so both representations run through **byte-for-
byte identical matching logic** -- only the price-level storage differs.
Every existing correctness test (`matching_engine_test.cpp`, 27 scenarios
plus a 3,000-step randomized invariant sequence) now runs as a
`TYPED_TEST` against both engines, and `dense_order_book_test.cpp` covers
`DenseOrderBook`-specific concerns (in particular, that the cached
best-index bookkeeping is exactly correct after removals). All 100 tests
pass on MSVC/Release and clean under ASan+UBSan.

## Phase 4: results

Reproduction: `bash scripts/run_benchmarks.sh` (writes
`results/optimized_map_pooled_benchmark.csv` and
`results/optimized_dense_pooled_benchmark.csv`; the pre-optimization
`results/baseline_benchmark.csv` is the Milestone 4 reference point, same
harness, same seeds, same 100,000-active-order configurations shown
below).

### Pooling alone: baseline -> MatchingEngine (map + pool)

| Operation | Baseline ev/s | Pooled ev/s | Throughput | Baseline p99.9 | Pooled p99.9 | p99.9 change |
|---|---:|---:|---:|---:|---:|---:|
| add_existing_level | 432,204 | 957,039 | **+121%** | 81,001 ns | 27,100 ns | **-67%** |
| cancel | 473,691 | 1,233,270 | **+160%** | 44,603 ns | 15,100 ns | **-66%** |
| replace | 298,649 | 1,334,370 | **+347%** | 58,712 ns | 13,300 ns | **-77%** |
| single_match | 314,802 | 968,035 | **+208%** | 117,108 ns | 40,403 ns | **-66%** |
| multi_level_sweep | 63,039 | 162,256 | **+157%** | 634,739 ns | 317,205 ns | **-50%** |
| mixed (match_heavy) | 191,137 | 609,210 | **+219%** | 150,303 ns | 46,901 ns | **-69%** |

Pooling alone -- with **zero change to matching logic or the price-level
data structure** -- delivered the single largest improvement in this
milestone, exactly as the profile predicted: removing the dominant cost
(malloc/free) produced the dominant win.

### Dense representation: MatchingEngine (map + pool) -> DenseMatchingEngine (dense + pool)

| Operation | Map+pool ev/s | Dense+pool ev/s | Change |
|---|---:|---:|---:|
| add_existing_level | 957,039 | 1,213,140 | **+27%** |
| add_new_level | 1,103,180 | 961,178 | -13% |
| replace | 1,334,370 | 1,788,400 | **+34%** |
| mixed (passive_heavy) | 750,143 | 1,336,010 | **+78%** |
| mixed (match_heavy) | 609,210 | 705,164 | **+16%** |
| cancel | 1,233,270 | 1,080,880 | -12% |
| single_match | 968,035 | 695,425 | **-28%** |
| multi_level_sweep | 162,256 | 113,592 | **-30%** |

Unlike pooling, the dense representation is **not a uniform win**. It
clearly helps add/replace/mixed workloads, where O(1) level lookup beats
O(log levels) tree traversal. It measurably *hurts* the operations that
repeatedly walk and empty a long, contiguous, one-order-per-tick run of
price levels (`single_match`, `multi_level_sweep`) and, to a lesser
extent, plain `cancel`.

**Why:** cachegrind on the exact `multi_level_sweep` shape
(`results/profiling/cachegrind_optimized_comparison.txt`) shows dense
executing *fewer* total instructions and *fewer* data references than
map (128.9M vs 174.2M instructions) -- yet its last-level cache miss rate
is **nearly 4x higher** (2.2% vs 0.6%) with **2.6x more absolute LL
misses** (1,297,440 vs 495,253). Raw instruction count says dense should
be faster; wall-clock time says the opposite, and cache behavior is the
reconciling variable: an LL miss costs on the order of 100+ cycles, so a
higher miss rate on fewer total accesses can still lose to a lower miss
rate on more accesses.

The plausible mechanism: `DenseOrderBook`'s array must be sized for the
*worst-case* configuration across every benchmark (up to a 100,000-order,
one-per-tick book), so it's provisioned across a `kMidPrice +/- 150,000`
range -- roughly 24MB of `PriceLevel` slots per engine instance. A
workload like `single_match`/`multi_level_sweep`, which only ever
populates ~50,000-100,000 of those slots on one side, is *sparse relative
to the range the array had to be sized for*, even though it looks dense
locally (one order per tick). That's precisely the "sparse versus dense
price ranges" tradeoff spec section 8.2 asks to be understood, not
resolved: a `std::map` only ever pays for the levels that exist, so its
memory (and, evidently, its cache footprint under this exact access
pattern) scales with *actual* level count, not with the *configured tick
range*. `mixed_event_stream` and `add_existing_level`, in contrast, keep
their activity concentrated in a narrower live-order price band, so dense
wins there.

### Combined effect (baseline -> dense + pool, the two optimizations together)

| Operation | Baseline ev/s | Dense+pool ev/s | Overall throughput |
|---|---:|---:|---:|
| add_existing_level | 432,204 | 1,213,140 | **+181%** |
| replace | 298,649 | 1,788,400 | **+499%** |
| mixed (passive_heavy) | 231,329 | 1,336,010 | **+478%** |
| mixed (match_heavy) | 191,137 | 705,164 | **+269%** |
| cancel | 473,691 | 1,080,880 | **+128%** |
| single_match | 314,802 | 695,425 | **+121%** |
| multi_level_sweep | 63,039 | 113,592 | **+80%** |

Even where the dense representation regressed relative to the pooled map
(`cancel`, `single_match`, `multi_level_sweep`), the *combined* baseline-
to-optimized comparison is still a large net win, because pooling's
contribution dominates. No configuration in the 27-benchmark suite
regressed relative to the Milestone 4 baseline.

## Engineering conclusion

Given the measured tradeoff, **`MatchingEngine` (pooled `OrderBook`)
remains this project's default/production instantiation**;
`DenseMatchingEngine` is kept and fully tested as the Milestone 5
deliverable spec section 8.2 asks for, and is the better choice
specifically for workloads dominated by adds/replaces/blended traffic
over a bounded, moderately dense price range -- not as a universal
replacement. This is the intended lesson of the milestone: profiling
identified a real, large bottleneck (allocation) with a clean, uniform
fix; the second "obvious" optimization (dense arrays) turned out to be
workload-dependent, which is only visible because it was actually
measured and compared, not assumed.

## Environment

Same machine as `docs/benchmark_methodology.md`: 12th Gen Intel Core
i7-1255U (10 cores / 12 logical processors), Windows 11 Home
(build 10.0.26200) for the `lob_benchmarks` throughput/latency numbers
(MSVC 19.29, Release), and WSL2 Ubuntu with GCC 11.4.0 for the
valgrind callgrind/cachegrind profiling (Release, `-g`). Not an isolated
benchmark host -- no CPU pinning, no frequency scaling disabled, ordinary
background load present. See `docs/benchmark_methodology.md` for full
methodology and what that does and doesn't affect.
