# Benchmark Methodology

TODO: fill in during Milestone 4.

## Sections to complete

- Operations measured: add at existing level, add at new level, cancel,
  replace, single-order match, multi-level sweep, mixed event stream
- Workloads: passive-heavy (70/25/5), balanced (50/30/20), match-heavy
  (40/20/40)
- Active-order counts: ~1,000 / ~10,000 / ~100,000
- Measurements: events processed, events/sec, p50/p95/p99/p99.9, active
  orders, price levels, workload configuration
- Controls: Release build, pre-generated workloads, no allocation/logging
  in timed sections, warm-up, multiple repetitions, deterministic seeds,
  CPU pinning where practical
- Reported environment: hardware, OS, compiler + version, optimization
  flags, CPU-frequency configuration

All numbers in this document must be measured, not estimated, and must be
clearly scoped as synthetic (not production exchange performance).
