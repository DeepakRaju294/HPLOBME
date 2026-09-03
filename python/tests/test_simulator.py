"""Tests for the discrete-event simulator (Milestone 6 exit gate:
"Python simulation completes correctly" and "fixed-seed runs reproduce").
"""

from typing import Optional

from simulator import (
    MarketMakerConfig,
    OrderFlowConfig,
    PriceShockConfig,
    ReferencePriceConfig,
    SimulationConfig,
    Simulator,
    load_config,
)
from simulator.engine import lob_engine


def _make_config(seed: int, duration_steps: int = 200, shock: Optional[PriceShockConfig] = None) -> SimulationConfig:
    return SimulationConfig(
        seed=seed,
        tick_size=1,
        duration_steps=duration_steps,
        reference_price=ReferencePriceConfig(
            model="gbm", initial_price=10000, drift=0.0, volatility=0.001, shock=shock
        ),
        order_flow=OrderFlowConfig(
            arrival_rate=4.0, buy_probability=0.5, order_size_mean=20, aggressive_probability=0.2
        ),
        market_maker=MarketMakerConfig(
            strategy="null",
            inventory_coefficient=0.001,
            half_spread_ticks=5,
            max_inventory=1000,
            max_order_size=100,
            min_spread_ticks=2,
            quote_refresh_interval_steps=1,
            volatility_suspend_threshold=0.01,
        ),
    )


def test_simulation_completes_and_produces_metrics():
    config = _make_config(seed=1)
    result = Simulator(config).run()

    assert len(result.steps) == config.duration_steps
    assert all(step.reference_price > 0 for step in result.steps)
    # Some external order flow (arrival_rate=4/step over 200 steps) should
    # have produced at least a few trades and some resting orders.
    assert result.total_trades > 0
    assert any(step.active_orders > 0 for step in result.steps)


def test_fixed_seed_runs_reproduce_exactly():
    config = _make_config(seed=42)

    result_a = Simulator(config).run()
    result_b = Simulator(config).run()

    assert result_a.reference_prices == result_b.reference_prices
    assert result_a.total_trades == result_b.total_trades
    assert result_a.final_state_hash == result_b.final_state_hash
    for step_a, step_b in zip(result_a.steps, result_b.steps):
        assert step_a == step_b


def test_different_seeds_produce_different_trajectories():
    result_a = Simulator(_make_config(seed=1)).run()
    result_b = Simulator(_make_config(seed=2)).run()

    assert result_a.reference_prices != result_b.reference_prices


def test_price_shock_regime_applies_at_configured_step():
    shock = PriceShockConfig(step=50, magnitude=500)
    config = _make_config(seed=7, duration_steps=100, shock=shock)

    result = Simulator(config).run()

    price_before = result.steps[49].reference_price
    price_at_shock = result.steps[50].reference_price
    assert price_at_shock - price_before >= 400  # shock dominates normal step-to-step drift


def test_load_config_reads_baseline_yaml():
    config = load_config("configs/baseline.yaml")
    assert config.seed == 42
    assert config.reference_price.model == "gbm"
    assert config.market_maker.strategy == "inventory_aware"


def test_engine_module_is_importable_and_functional():
    engine = lob_engine.MatchingEngine()
    events = engine.submit(lob_engine.NewOrder(id=1, side=lob_engine.Side.Buy, quantity=10, price=100))
    assert any(isinstance(e, lob_engine.OrderAccepted) for e in events)
    assert engine.validate_invariants()
