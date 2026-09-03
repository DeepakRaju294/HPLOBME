"""Reference-price process driving the simulation (spec section 17)."""

from __future__ import annotations

import numpy as np

from .config import ReferencePriceConfig


class ReferencePriceProcess:
    """Geometric Brownian motion reference price, reported in integer
    ticks, with an optional one-time deterministic shock at a configured
    step (spec section 17's "sudden price shock" regime)."""

    def __init__(self, config: ReferencePriceConfig, rng: np.random.Generator):
        self._config = config
        self._rng = rng
        self._price = float(config.initial_price)

    @property
    def price(self) -> int:
        return max(1, int(round(self._price)))

    def step(self, step_index: int) -> int:
        drift = self._config.drift
        volatility = self._config.volatility
        shock_term = self._rng.standard_normal()
        self._price *= 1.0 + drift + volatility * shock_term
        self._price = max(self._price, 1.0)  # prices must stay positive

        shock = self._config.shock
        if shock is not None and step_index == shock.step:
            self._price = max(1.0, self._price + shock.magnitude)

        return self.price
