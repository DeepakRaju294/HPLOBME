"""Discrete-event market simulator (spec section 17).

All matching decisions are made by the C++ MatchingEngine, loaded via
`simulator.engine`; this package only decides what commands to submit and
records the resulting metrics -- it never reimplements matching logic.
"""

from .config import (
    MarketMakerConfig,
    OrderFlowConfig,
    PriceShockConfig,
    ReferencePriceConfig,
    SimulationConfig,
    load_config,
)
from .simulation import Simulator, SimulationResult, StepMetrics
from .strategy import MarketMakerStrategy, NullStrategy

__all__ = [
    "SimulationConfig",
    "ReferencePriceConfig",
    "OrderFlowConfig",
    "MarketMakerConfig",
    "PriceShockConfig",
    "load_config",
    "Simulator",
    "SimulationResult",
    "StepMetrics",
    "MarketMakerStrategy",
    "NullStrategy",
]
