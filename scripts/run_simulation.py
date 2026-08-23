#!/usr/bin/env python3
"""Entry point for running a market simulation from a config file.

Usage:
    python scripts/run_simulation.py --config configs/baseline.yaml
"""

import argparse


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a market-making simulation.")
    parser.add_argument("--config", required=True, help="Path to a YAML simulation config.")
    parser.parse_args()

    raise NotImplementedError(
        "TODO(Milestone 6/7): load config, build the simulator on top of the "
        "pybind11 engine, run the market maker, and report PnL/inventory/"
        "spread-capture/adverse-selection metrics."
    )


if __name__ == "__main__":
    main()
