#!/usr/bin/env python3
"""Entry point for running a market simulation from a config file.

Usage:
    python scripts/run_simulation.py --config configs/baseline.yaml

The strategy is selected by the config's market_maker.strategy field.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "python"))

from simulator import Simulator, load_config  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a market-making simulation.")
    parser.add_argument("--config", required=True, help="Path to a YAML simulation config.")
    args = parser.parse_args()

    config = load_config(args.config)
    simulator = Simulator(config)
    result = simulator.run()

    prices = result.reference_prices
    print(f"config: {args.config}")
    print(f"seed: {config.seed}")
    print(f"steps: {len(result.steps)}")
    print(f"reference price: start={prices[0]}, end={prices[-1]}, "
          f"min={min(prices)}, max={max(prices)}")
    print(f"total trades: {result.total_trades}")
    print(f"final active orders: {result.steps[-1].active_orders}")
    print(f"final best bid/ask: {result.steps[-1].best_bid} / {result.steps[-1].best_ask}")
    print(f"final state hash: {result.final_state_hash}")
    metrics = result.strategy_metrics
    print(f"strategy: {config.market_maker.strategy}")
    print(f"PnL (ticks x quantity): {metrics.total_pnl}")
    print(f"maximum drawdown: {metrics.maximum_drawdown}")
    print(f"inventory: final={metrics.final_inventory}, max_abs={metrics.maximum_absolute_inventory}")
    print(f"fills: count={metrics.fill_count}, volume={metrics.filled_volume}")
    print(f"average spread captured: {metrics.average_spread_captured:.4f} ticks")
    print(f"adverse selection (1-step): {metrics.adverse_selection:.4f} ticks")


if __name__ == "__main__":
    main()
