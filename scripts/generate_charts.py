#!/usr/bin/env python3
"""Generate the exact benchmark and strategy charts required by the spec."""
from __future__ import annotations

import argparse
import dataclasses
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))
from simulator import Simulator, load_config  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RESULTS = os.path.join(ROOT, "results")


def benchmark_chart() -> None:
    files = {
        "baseline map": "baseline_benchmark.csv",
        "pooled map": "optimized_map_pooled_benchmark.csv",
        "pooled dense": "optimized_dense_pooled_benchmark.csv",
    }
    frames = []
    for implementation, filename in files.items():
        frame = pd.read_csv(os.path.join(RESULTS, filename))
        frame["implementation"] = implementation
        frames.append(frame)
    data = pd.concat(frames, ignore_index=True)
    mixed = data[data.operation == "mixed_event_stream"]
    if mixed.empty:
        mixed = data[data.workload_config != "n/a"]
    representative = data[(data.operation == "cancel")]

    fig, axes = plt.subplots(2, 2, figsize=(14, 9))
    axes = axes.ravel()
    for name, group in mixed.groupby("implementation"):
        series = group.groupby("active_order_target").events_per_second.mean().sort_index()
        axes[0].plot(series.index, series.values, marker="o", label=name)
    axes[0].set(xscale="log", title="Throughput vs active-order target", xlabel="orders", ylabel="events/s")
    axes[0].legend(fontsize=8)

    latency = representative.groupby("implementation")[["p50_ns", "p95_ns", "p99_ns"]].mean()
    latency.plot.bar(ax=axes[1])
    axes[1].set(title="Cancel latency percentiles", ylabel="nanoseconds", xlabel="")
    axes[1].tick_params(axis="x", rotation=20)

    comparison = data.groupby("implementation").agg(throughput=("events_per_second", "mean"), p99=("p99_ns", "mean"))
    comparison.throughput.div(1e6).plot.bar(ax=axes[2], color="tab:blue")
    axes[2].set(title="Baseline vs optimized throughput", ylabel="million events/s", xlabel="")
    axes[2].tick_params(axis="x", rotation=20)
    comparison.p99.div(1000).plot.bar(ax=axes[3], color="tab:orange")
    axes[3].set(title="Map vs dense p99 latency", ylabel="microseconds", xlabel="")
    axes[3].tick_params(axis="x", rotation=20)
    fig.tight_layout()
    fig.savefig(os.path.join(RESULTS, "benchmark_charts.png"), dpi=160)
    plt.close(fig)


def strategy_charts(steps: int) -> None:
    regimes = ["baseline", "high_volatility", "directional_imbalance", "price_shock"]
    summaries = []
    runs = {}
    for regime in regimes:
        config = load_config(os.path.join(ROOT, "configs", f"{regime}.yaml"))
        if steps:
            scale = steps / config.duration_steps
            ref = config.reference_price
            shock = ref.shock
            if shock is not None:
                shock = dataclasses.replace(shock, step=max(1, int(shock.step * scale)))
                ref = dataclasses.replace(ref, shock=shock)
            config = dataclasses.replace(config, duration_steps=steps, reference_price=ref)
        for strategy in ("fixed_spread", "inventory_aware"):
            run_config = dataclasses.replace(config, market_maker=dataclasses.replace(config.market_maker, strategy=strategy))
            result = Simulator(run_config).run()
            if strategy == "inventory_aware":
                runs[regime] = result
            summaries.append({"regime": regime, "strategy": strategy, **dataclasses.asdict(result.strategy_metrics)})
    pd.DataFrame(summaries).to_csv(os.path.join(RESULTS, "strategy_summary.csv"), index=False)

    fig, axes = plt.subplots(3, 1, figsize=(12, 11), sharex=True)
    for regime, result in runs.items():
        x = [s.step for s in result.steps]
        axes[0].plot(x, [s.pnl for s in result.steps], label=regime)
        axes[1].plot(x, [s.inventory for s in result.steps], label=regime)
    baseline = runs["baseline"]
    x = [s.step for s in baseline.steps]
    axes[2].plot(x, [s.reference_price for s in baseline.steps], label="reference", color="black")
    axes[2].plot(x, [s.maker_bid for s in baseline.steps], label="maker bid", alpha=.75)
    axes[2].plot(x, [s.maker_ask for s in baseline.steps], label="maker ask", alpha=.75)
    axes[0].set(title="Inventory-aware PnL by regime", ylabel="ticks x quantity")
    axes[1].set(title="Inventory by regime", ylabel="quantity")
    axes[2].set(title="Baseline reference price and quotes", ylabel="price ticks", xlabel="simulation step")
    for axis in axes:
        axis.legend(ncol=2, fontsize=8)
        axis.grid(alpha=.2)
    fig.tight_layout()
    fig.savefig(os.path.join(RESULTS, "strategy_charts.png"), dpi=160)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--steps", type=int, default=2000, help="steps per strategy run (0 uses config durations)")
    args = parser.parse_args()
    benchmark_chart()
    strategy_charts(args.steps)
    print("wrote results/benchmark_charts.png, results/strategy_charts.png, results/strategy_summary.csv")


if __name__ == "__main__":
    main()
