"""Milestone 7 strategy, controls, determinism, and accounting gates."""
import dataclasses

import pytest

from simulator import Simulator, load_config


@pytest.mark.parametrize("regime", ["baseline", "high_volatility", "directional_imbalance", "price_shock"])
@pytest.mark.parametrize("strategy", ["fixed_spread", "inventory_aware"])
def test_strategies_run_in_every_required_regime(regime, strategy):
    config = load_config(f"configs/{regime}.yaml")
    config = dataclasses.replace(config, duration_steps=300,
                                 market_maker=dataclasses.replace(config.market_maker, strategy=strategy))
    result = Simulator(config).run()
    metrics = result.strategy_metrics
    final = result.steps[-1]

    assert len(result.steps) == 300
    assert metrics.total_pnl == metrics.final_cash + metrics.final_inventory * final.reference_price
    assert metrics.maximum_absolute_inventory <= config.market_maker.max_inventory
    assert abs(metrics.final_inventory) <= config.market_maker.max_inventory
    assert metrics.filled_volume >= metrics.fill_count
    assert all(s.maker_bid is None or s.maker_ask is None or
               s.maker_ask - s.maker_bid >= config.market_maker.min_spread_ticks for s in result.steps)


def test_market_maker_run_is_deterministic():
    config = dataclasses.replace(load_config("configs/baseline.yaml"), duration_steps=500)
    a, b = Simulator(config).run(), Simulator(config).run()
    assert a.steps == b.steps
    assert a.strategy_metrics == b.strategy_metrics
    assert a.final_state_hash == b.final_state_hash


def test_inventory_aware_reservation_price_moves_against_inventory():
    from simulator import InventoryAwareStrategy
    config = load_config("configs/baseline.yaml").market_maker
    strategy = InventoryAwareStrategy(config)
    strategy.inventory = 100
    assert strategy.reservation_price(10_000) < 10_000
    strategy.inventory = -100
    assert strategy.reservation_price(10_000) > 10_000
