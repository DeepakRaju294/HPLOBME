# Matching Rules

TODO: fill in during Milestone 2.

## Sections to complete

- Price priority (buy matches lowest ask first, sell matches highest bid
  first)
- Time priority (FIFO within a price level)
- Execution price (trades execute at the resting order's price)
- Partial fills (residual quantity, level aggregate, order index updates)
- Time-in-force semantics: GTC, IOC, FOK, Post-only
- Replace priority rules: same-price/lower-quantity preserves priority;
  increased quantity or changed price loses priority (atomic
  cancel-and-reinsert); a price-changing replace may execute immediately
- Order lifecycle outcomes: accepted, rejected, trade, partial fill,
  filled, rested, cancelled
