"""Market-maker strategy interface (spec section 18).

Concrete strategies (fixed-spread baseline, inventory-aware) land in
Milestone 7. The interface exists now so the simulation event loop
doesn't change shape when they do -- it only needs to know how to call a
strategy, not what any particular strategy does.
"""

from __future__ import annotations

import abc


class MarketMakerStrategy(abc.ABC):
    """Called once per simulation step, after external order flow has
    been processed, so a strategy can observe book/inventory state and
    submit its own quote updates via ``engine``. Must never reimplement
    matching decisions itself -- only submit/cancel/replace commands."""

    @abc.abstractmethod
    def on_step(self, step_index: int, timestamp: int, reference_price: int, engine, simulator) -> None:
        raise NotImplementedError


class NullStrategy(MarketMakerStrategy):
    """Participates in no way. The default until Milestone 7 adds real
    strategies, and useful on its own as a "no market maker, just observe
    external flow" regime."""

    def on_step(self, step_index, timestamp, reference_price, engine, simulator) -> None:
        return None
