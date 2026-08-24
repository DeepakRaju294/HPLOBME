# Matching Rules

Implemented in `cpp/src/matching_engine.cpp` and `cpp/src/order_book.cpp`.
This documents the exact behavior, including the judgment calls made where
the spec (`docs/spec.md`, section 10-11) left room for interpretation.

## Price priority

A buy order matches the lowest available ask first; a sell order matches
the highest available bid first. Enforced structurally by the book's
`std::map` ordering (`bids_` descending, `asks_` ascending) -- matching
always walks from `begin()`.

## Time priority

Orders at the same price execute FIFO, via `PriceLevel`'s `std::list`
(oldest at `front()`).

## Execution price

Every trade executes at the **resting (maker) order's** price, never the
incoming (taker) order's limit price. See
`MatchingEngine::walk_match`.

## Partial fills

A partial fill updates, atomically within `OrderBook`: the maker's
resting/remaining quantity, the price level's aggregate quantity, and (if
the maker is fully consumed) removes it from the order index and, if the
level is now empty, removes the level itself. The taker's remaining
quantity is tracked locally during the match walk and only touches the
book again if it needs to rest (GTC) or never at all (IOC/FOK/Market).

## Time-in-force semantics

| TIF | Behavior |
|---|---|
| **GTC** | Matches available liquidity within its limit price, then rests any remainder. |
| **IOC** | Matches available liquidity, then cancels any remainder -- never rests. |
| **FOK** | All-or-nothing: pre-checks total fillable quantity (via `OrderBook::matchable_quantity`, no mutation) before touching the book. If it can't fill completely, the order is rejected outright -- no partial execution ever occurs. |
| **PostOnly** | Must never take liquidity. If the order would cross the book at all, it is rejected outright (not partially filled, not rested). Otherwise it rests in full with no matching attempted. |

**Market orders** are matched with no price bound (they walk the book
until filled or liquidity runs out) and never rest, since they have no
price to rest at. Because GTC ("stay resting until cancelled") and
PostOnly ("must add liquidity") are both meaningless for an order that can
never rest, **Market orders are only valid with IOC or FOK** -- GTC or
PostOnly on a Market order is rejected as
`RejectReason::InvalidTimeInForceForOrderType`. This is a design decision,
not explicit in the spec.

## Order lifecycle events

Every `submit()` that isn't rejected outright begins with `OrderAccepted`,
followed by zero or more `TradeExecuted` (+ the maker's own
`OrderFilled`/`OrderPartiallyFilled`) events per resting order matched,
and ends with exactly one terminal event for the incoming order:

* `OrderFilled` -- fully filled (whether immediately or after partial
  matches).
* `OrderRested` -- GTC/PostOnly with quantity remaining after matching
  (preceded by `OrderPartiallyFilled` if some quantity did fill first).
* `OrderCancelled` -- IOC/Market with quantity remaining after matching,
  i.e. the "cancelled residual" outcome (preceded by
  `OrderPartiallyFilled` if some quantity did fill first).

A command that fails validation (zero quantity, duplicate ID, invalid
price, invalid TIF/type combination, PostOnly-would-cross, or
FOK-unfillable) produces a single `OrderRejected` event and never touches
the book.

## Replace priority rules

* **Same price, quantity reduced or unchanged:** priority-preserving.
  The order's quantity is updated in place (`OrderBook::reduce_quantity_in_place`)
  without moving its FIFO position. Emits a single `OrderReplaced`
  (`priority_preserved = true`); no matching is attempted, since an
  unchanged price against an already-non-crossed book cannot newly cross.
* **Quantity increased (same price), or price changed:** priority-losing.
  Treated as an atomic cancel-and-reinsert: the order is snapshotted,
  cancelled, and resubmitted at the new price/quantity through the same
  match-then-rest path used by a fresh GTC order. Emits `OrderReplaced`
  (`priority_preserved = false`) followed by whatever trade/fill/rest
  events the reinsertion produces. **A price-changing replace can execute
  immediately** if the new price now crosses the book.
* **Only GTC and PostOnly orders are ever found resting to replace** --
  IOC/FOK orders never rest in the first place, so a replace against a
  since-filled or since-cancelled order simply falls through to the
  unknown-ID rejection path.
* **PostOnly replace that would cross:** since reinsertion for a
  priority-losing replace can execute immediately, a PostOnly order whose
  price change would cross the book is a direct violation of PostOnly's
  "never takes liquidity" guarantee. Rather than let it trade, the
  **replace command itself is rejected** (`RejectReason::WouldCross`) and
  the original resting order is left untouched. This is a design decision
  the spec doesn't make explicit, chosen to keep PostOnly's contract
  consistent between `submit()` and `replace()`.

## Reject reasons

`OrderRejected` carries a `RejectReason` (see `cpp/include/events.hpp`):
`ZeroQuantity`, `DuplicateOrderId`, `InvalidPrice`,
`InvalidTimeInForceForOrderType`, `UnknownOrderId`, `WouldCross`,
`FillOrKillUnfillable`.
