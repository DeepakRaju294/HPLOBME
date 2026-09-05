# Market-making strategy analysis

The simulator evaluates a symmetric fixed-spread baseline and the required
inventory-aware strategy under baseline, high-volatility, directional-flow,
and sudden-shock regimes. These are synthetic educational experiments, not
claims about exchange-production profitability.

## Strategy and controls

The fixed-spread strategy centers quotes on the integer reference price. The
inventory-aware strategy uses the specified reservation price:

```text
reference price - inventory coefficient * inventory
```

Both place post-only orders through the C++ matching engine. They enforce
maximum inventory, maximum order size, minimum spread, quote-refresh interval,
and relative-price-move suspension. Maker IDs occupy the upper half of the
64-bit ID range, keeping attribution disjoint from synthetic external orders.

## Accounting definitions

Cash changes at the actual C++ trade price: buys reduce cash and increase
inventory; sells do the reverse. Every step is marked to the current reference:

```text
PnL = cash + inventory * reference price
```

This identity is asserted in tests at the end of every required regime.
Maximum drawdown is the largest peak-to-trough decline in marked PnL. Spread
capture is the volume-weighted distance between fill and contemporaneous
reference price. Adverse selection is the volume-weighted signed cost versus
the next step's reference price (positive means the move was against the
maker). Final-step fills use the final reference mark.

## Reproducible outputs

`python scripts/generate_charts.py` runs both strategies and writes:

* `results/strategy_summary.csv` — metrics by regime and strategy.
* `results/strategy_charts.png` — PnL, inventory, and reference/bid/ask plots.
* `results/benchmark_charts.png` — throughput/scale, latency percentiles, and
  baseline/map/dense comparisons from the recorded benchmark CSVs.

The checked-in charts use 2,000 steps per strategy/regime to keep reproduction
quick. Pass `--steps 0` to use the full 10,000-step config durations. The
results show why inventory controls matter: directional imbalance can pin a
strategy at its limit, while volatile regimes make mark-to-market PnL dominate
short-horizon spread capture. Comparison should focus on risk exposure as well
as terminal PnL.
