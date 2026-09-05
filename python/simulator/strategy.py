"""Market-making strategies and reconciled strategy accounting."""
from __future__ import annotations

import abc
import dataclasses
import math
from typing import Dict, Iterable, List, Optional, Tuple

from .config import MarketMakerConfig
from .engine import lob_engine


@dataclasses.dataclass(frozen=True)
class Fill:
    step: int
    side: str
    price: int
    quantity: int
    reference_price: int


@dataclasses.dataclass(frozen=True)
class StrategySnapshot:
    inventory: int
    cash: int
    pnl: int
    bid: Optional[int]
    ask: Optional[int]
    fill_count: int
    filled_volume: int


@dataclasses.dataclass(frozen=True)
class StrategyMetrics:
    total_pnl: int
    maximum_drawdown: int
    final_inventory: int
    maximum_absolute_inventory: int
    fill_count: int
    filled_volume: int
    average_spread_captured: float
    adverse_selection: float
    final_cash: int
    quote_count: int


class MarketMakerStrategy(abc.ABC):
    """A strategy submits commands; all matching remains in C++."""
    @abc.abstractmethod
    def on_step(self, step_index: int, timestamp: int, reference_price: int, engine, simulator) -> None:
        raise NotImplementedError

    def on_events(self, step_index: int, reference_price: int, events: Iterable[object]) -> None:
        pass

    def snapshot(self, reference_price: int) -> StrategySnapshot:
        return StrategySnapshot(0, 0, 0, None, None, 0, 0)

    def metrics(self, final_reference_price: int) -> StrategyMetrics:
        return StrategyMetrics(0, 0, 0, 0, 0, 0, 0.0, 0.0, 0, 0)


class NullStrategy(MarketMakerStrategy):
    def on_step(self, step_index, timestamp, reference_price, engine, simulator) -> None:
        pass


class QuotingStrategy(MarketMakerStrategy):
    """Shared quote lifecycle, controls, fills, and mark-to-market PnL."""
    _FIRST_ORDER_ID = 1 << 63

    def __init__(self, config: MarketMakerConfig):
        self.config = config
        self.inventory = 0
        self.cash = 0
        self._next_id = self._FIRST_ORDER_ID
        self._orders: Dict[int, Tuple[str, int]] = {}
        self._bid_id: Optional[int] = None
        self._ask_id: Optional[int] = None
        self._bid: Optional[int] = None
        self._ask: Optional[int] = None
        self._last_reference: Optional[int] = None
        self._fills: List[Fill] = []
        self._pnl_path: List[int] = []
        self._max_abs_inventory = 0
        self._filled_volume = 0
        self._spread_capture_value = 0
        self._adverse_value = 0
        self._adverse_volume = 0
        self._pending_adverse: List[Fill] = []
        self._quote_count = 0

    @abc.abstractmethod
    def reservation_price(self, reference_price: int) -> float:
        raise NotImplementedError

    def on_events(self, step_index: int, reference_price: int, events: Iterable[object]) -> None:
        for fill in self._pending_adverse:
            direction = 1 if fill.side == "buy" else -1
            self._adverse_value += direction * (fill.price - reference_price) * fill.quantity
            self._adverse_volume += fill.quantity
        self._pending_adverse.clear()

        event_list = list(events)
        for event in event_list:
            if not isinstance(event, lob_engine.TradeExecuted):
                continue
            trade = event.trade
            own = self._orders.get(trade.resting_order_id)
            if own is None:
                continue
            side, _ = own
            quantity, price = int(trade.quantity), int(trade.price)
            if side == "buy":
                self.inventory += quantity
                self.cash -= price * quantity
                self._spread_capture_value += (reference_price - price) * quantity
            else:
                self.inventory -= quantity
                self.cash += price * quantity
                self._spread_capture_value += (price - reference_price) * quantity
            fill = Fill(step_index, side, price, quantity, reference_price)
            self._fills.append(fill)
            self._pending_adverse.append(fill)
            self._filled_volume += quantity
            self._max_abs_inventory = max(self._max_abs_inventory, abs(self.inventory))
        for event in event_list:
            if isinstance(event, lob_engine.OrderFilled) and event.order_id in self._orders:
                self._forget_order(event.order_id)

    def _forget_order(self, order_id: int) -> None:
        self._orders.pop(order_id, None)
        if self._bid_id == order_id:
            self._bid_id, self._bid = None, None
        if self._ask_id == order_id:
            self._ask_id, self._ask = None, None

    def _cancel_quotes(self, timestamp: int, engine) -> None:
        for order_id in (self._bid_id, self._ask_id):
            if order_id is not None and order_id in self._orders:
                engine.cancel(lob_engine.CancelOrder(order_id, timestamp))
                self._forget_order(order_id)

    def _submit_quote(self, side: str, price: int, quantity: int, timestamp: int, engine) -> None:
        if quantity <= 0 or price <= 0:
            return
        order_id = self._next_id
        self._next_id += 1
        enum_side = lob_engine.Side.Buy if side == "buy" else lob_engine.Side.Sell
        events = engine.submit(lob_engine.NewOrder(
            id=order_id, side=enum_side, quantity=quantity, price=price,
            type=lob_engine.OrderType.Limit, time_in_force=lob_engine.TimeInForce.PostOnly,
            timestamp=timestamp,
        ))
        if any(isinstance(e, lob_engine.OrderRested) for e in events):
            self._orders[order_id] = (side, quantity)
            if side == "buy":
                self._bid_id, self._bid = order_id, price
            else:
                self._ask_id, self._ask = order_id, price
            self._quote_count += 1

    def on_step(self, step_index: int, timestamp: int, reference_price: int, engine, simulator) -> None:
        previous = self._last_reference
        self._last_reference = reference_price
        interval = max(1, self.config.quote_refresh_interval_steps)
        if step_index % interval != 0:
            self._pnl_path.append(self.cash + self.inventory * reference_price)
            return
        self._cancel_quotes(timestamp, engine)
        relative_move = 0.0 if previous is None else abs(reference_price - previous) / max(1, previous)
        if relative_move >= self.config.volatility_suspend_threshold:
            self._pnl_path.append(self.cash + self.inventory * reference_price)
            return
        half = max(self.config.half_spread_ticks, math.ceil(self.config.min_spread_ticks / 2))
        reservation = self.reservation_price(reference_price)
        bid = min(math.floor(reservation - half), reference_price - 1)
        ask = max(math.ceil(reservation + half), reference_price + 1, bid + self.config.min_spread_ticks)
        buy_size = min(self.config.max_order_size, max(0, self.config.max_inventory - self.inventory))
        sell_size = min(self.config.max_order_size, max(0, self.config.max_inventory + self.inventory))
        self._submit_quote("buy", bid, buy_size, timestamp, engine)
        self._submit_quote("sell", ask, sell_size, timestamp, engine)
        self._pnl_path.append(self.cash + self.inventory * reference_price)

    def snapshot(self, reference_price: int) -> StrategySnapshot:
        return StrategySnapshot(self.inventory, self.cash, self.cash + self.inventory * reference_price,
                                self._bid, self._ask, len(self._fills), self._filled_volume)

    def metrics(self, final_reference_price: int) -> StrategyMetrics:
        for fill in self._pending_adverse:
            direction = 1 if fill.side == "buy" else -1
            self._adverse_value += direction * (fill.price - final_reference_price) * fill.quantity
            self._adverse_volume += fill.quantity
        self._pending_adverse.clear()
        pnl = self.cash + self.inventory * final_reference_price
        path = self._pnl_path or [pnl]
        peak, drawdown = path[0], 0
        for value in path:
            peak = max(peak, value)
            drawdown = max(drawdown, peak - value)
        volume = self._filled_volume
        return StrategyMetrics(
            pnl, drawdown, self.inventory, self._max_abs_inventory, len(self._fills), volume,
            self._spread_capture_value / volume if volume else 0.0,
            self._adverse_value / self._adverse_volume if self._adverse_volume else 0.0,
            self.cash, self._quote_count,
        )


class FixedSpreadStrategy(QuotingStrategy):
    def reservation_price(self, reference_price: int) -> float:
        return float(reference_price)


class InventoryAwareStrategy(QuotingStrategy):
    def reservation_price(self, reference_price: int) -> float:
        return reference_price - self.config.inventory_coefficient * self.inventory


def create_strategy(config: MarketMakerConfig) -> MarketMakerStrategy:
    name = config.strategy.lower().replace("-", "_")
    if name == "null":
        return NullStrategy()
    if name in {"fixed", "fixed_spread"}:
        return FixedSpreadStrategy(config)
    if name in {"inventory", "inventory_aware"}:
        return InventoryAwareStrategy(config)
    raise ValueError(f"unknown market-maker strategy: {config.strategy!r}")
