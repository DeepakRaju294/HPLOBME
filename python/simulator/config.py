"""Loads a market-regime YAML config (configs/*.yaml) into typed
dataclasses. See spec section 17 for the fields these represent."""

from __future__ import annotations

import dataclasses
from typing import Optional

import yaml


@dataclasses.dataclass(frozen=True)
class PriceShockConfig:
    step: int
    magnitude: int


@dataclasses.dataclass(frozen=True)
class ReferencePriceConfig:
    model: str
    initial_price: int
    drift: float
    volatility: float
    shock: Optional[PriceShockConfig] = None


@dataclasses.dataclass(frozen=True)
class OrderFlowConfig:
    arrival_rate: float
    buy_probability: float
    order_size_mean: float
    aggressive_probability: float


@dataclasses.dataclass(frozen=True)
class MarketMakerConfig:
    strategy: str
    inventory_coefficient: float
    half_spread_ticks: int
    max_inventory: int
    max_order_size: int
    min_spread_ticks: int
    quote_refresh_interval_steps: int
    volatility_suspend_threshold: float


@dataclasses.dataclass(frozen=True)
class SimulationConfig:
    seed: int
    tick_size: int
    duration_steps: int
    reference_price: ReferencePriceConfig
    order_flow: OrderFlowConfig
    market_maker: MarketMakerConfig


def load_config(path: str) -> SimulationConfig:
    with open(path, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)

    rp_raw = raw["reference_price"]
    shock = None
    if "shock" in rp_raw:
        shock = PriceShockConfig(step=rp_raw["shock"]["step"], magnitude=rp_raw["shock"]["magnitude"])

    return SimulationConfig(
        seed=raw["seed"],
        tick_size=raw["tick_size"],
        duration_steps=raw["duration_steps"],
        reference_price=ReferencePriceConfig(
            model=rp_raw["model"],
            initial_price=rp_raw["initial_price"],
            drift=rp_raw["drift"],
            volatility=rp_raw["volatility"],
            shock=shock,
        ),
        order_flow=OrderFlowConfig(**raw["order_flow"]),
        market_maker=MarketMakerConfig(**raw["market_maker"]),
    )
