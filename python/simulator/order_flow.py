"""Synthetic external order-flow generation (spec section 17, step 3).

Deterministic given the simulator's seeded RNG -- the C++ engine is the
only thing that makes matching decisions; this module only decides what
orders to submit.
"""

from __future__ import annotations

from typing import List

import numpy as np

from .config import OrderFlowConfig
from .engine import lob_engine


class OrderFlowGenerator:
    """Generates synthetic external limit/aggressive orders around the
    current reference price each simulation step."""

    def __init__(self, config: OrderFlowConfig, rng: np.random.Generator):
        self._config = config
        self._rng = rng
        self._next_id = 1

    def generate(self, timestamp: int, reference_price: int) -> List["lob_engine.NewOrder"]:
        commands: List[lob_engine.NewOrder] = []
        n_orders = self._rng.poisson(self._config.arrival_rate)

        for _ in range(n_orders):
            is_buy = self._rng.random() < self._config.buy_probability
            is_aggressive = self._rng.random() < self._config.aggressive_probability
            size = max(1, int(self._rng.exponential(self._config.order_size_mean)))
            offset_ticks = int(self._rng.integers(1, 10))

            side = lob_engine.Side.Buy if is_buy else lob_engine.Side.Sell
            if is_aggressive:
                # Priced through the opposite touch so it's marketable.
                price = reference_price + offset_ticks if is_buy else reference_price - offset_ticks
                time_in_force = lob_engine.TimeInForce.ImmediateOrCancel
            else:
                # Priced away from the touch so it rests.
                price = reference_price - offset_ticks if is_buy else reference_price + offset_ticks
                time_in_force = lob_engine.TimeInForce.GoodTillCancel
            price = max(1, price)

            order_id = self._next_id
            self._next_id += 1
            commands.append(
                lob_engine.NewOrder(
                    id=order_id,
                    side=side,
                    quantity=size,
                    price=price,
                    type=lob_engine.OrderType.Limit,
                    time_in_force=time_in_force,
                    timestamp=timestamp,
                )
            )

        return commands
