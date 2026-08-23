# Performance Analysis

TODO: fill in during Milestone 5. This is a primary portfolio deliverable
and should tell a coherent story:

```text
baseline
   -> profile
   -> identified bottleneck
   -> design change
   -> benchmark
   -> measured improvement
```

## Sections to complete

- Phase 1: baseline implementation and its benchmark results
- Phase 2: profiling method (perf / flamegraphs / cachegrind) and the
  bottlenecks it identified
- Phase 3: optimizations implemented, each justified by a specific
  profiling finding (candidates: dense tick-indexed price levels,
  preallocated order storage / object pool)
- Phase 4: before/after comparison table (throughput, p50, p99, p99.9) and
  relative change (%)
- `std::map` vs. dense price-level representation tradeoff discussion
  (sparse vs. dense price ranges, memory overhead, cache behavior)

Only measured numbers may appear in this document.
