"""The discrete-event simulation loop (spec section 17).

At each step:
  1. Advance time.
  2. Update reference price.
  3. Generate external order flow.
  4. Allow market maker to observe state.
  5. Submit quote updates.
  6. Process fills and events.
  7. Update inventory and cash.
  8. Record metrics.

Steps 4-7 for the market maker are delegated entirely to a
MarketMakerStrategy (Milestone 7); this loop only guarantees one call per
step with a consistent view of engine state. All matching decisions are
made by the C++ MatchingEngine -- this loop never reimplements them.
"""

from __future__ import annotations

import dataclasses
from typing import List, Optional

import numpy as np

from .config import SimulationConfig
from .engine import lob_engine
from .order_flow import OrderFlowGenerator
from .reference_price import ReferencePriceProcess
from .strategy import MarketMakerStrategy, NullStrategy


@dataclasses.dataclass
class StepMetrics:
    step: int
    timestamp: int
    reference_price: int
    best_bid: Optional[int]
    best_ask: Optional[int]
    active_orders: int
    trades_this_step: int


@dataclasses.dataclass
class SimulationResult:
    config: SimulationConfig
    steps: List[StepMetrics]
    final_state_hash: int

    @property
    def reference_prices(self) -> List[int]:
        return [s.reference_price for s in self.steps]

    @property
    def total_trades(self) -> int:
        return sum(s.trades_this_step for s in self.steps)


class Simulator:
    def __init__(self, config: SimulationConfig, strategy: Optional[MarketMakerStrategy] = None):
        self._config = config
        self._strategy = strategy if strategy is not None else NullStrategy()

        # Independent (but still deterministically seed-derived) streams
        # per concern, so changing order-flow parameters can't accidentally
        # shift the reference-price path of an otherwise-identical config,
        # or vice versa -- each still reproduces exactly given the same
        # top-level seed (spec section 17).
        seed_sequence = np.random.SeedSequence(config.seed)
        price_seed, flow_seed = seed_sequence.spawn(2)
        self._reference_price = ReferencePriceProcess(config.reference_price, np.random.default_rng(price_seed))
        self._order_flow = OrderFlowGenerator(config.order_flow, np.random.default_rng(flow_seed))
        self._engine = lob_engine.MatchingEngine()

    @property
    def engine(self):
        return self._engine

    def run(self) -> SimulationResult:
        steps: List[StepMetrics] = []

        for step_index in range(self._config.duration_steps):
            timestamp = step_index

            reference_price = self._reference_price.step(step_index)

            trades_this_step = 0
            for cmd in self._order_flow.generate(timestamp, reference_price):
                events = self._engine.submit(cmd)
                trades_this_step += sum(1 for event in events if isinstance(event, lob_engine.TradeExecuted))

            self._strategy.on_step(step_index, timestamp, reference_price, self._engine, self)

            self._engine.drain_market_data()  # bound the queue; unconsumed until a feed consumer needs it

            steps.append(
                StepMetrics(
                    step=step_index,
                    timestamp=timestamp,
                    reference_price=reference_price,
                    best_bid=self._engine.best_bid(),
                    best_ask=self._engine.best_ask(),
                    active_orders=self._engine.active_order_count(),
                    trades_this_step=trades_this_step,
                )
            )

        return SimulationResult(config=self._config, steps=steps, final_state_hash=self._engine.state_hash())
