# Stage 5: Semantic Type Safety — Implementation Plan

## Objective

Introduce zero-cost semantic type wrappers throughout the execution engine to prevent
accidental mixing of Price, Qty, Notional, BasisPoints, OrderId, and InstrumentId values
at compile time. All types use `explicit` constructors and require `.raw()` to extract
the underlying primitive, making type confusion a compile error rather than a runtime bug.

## Design Principles

1. **Zero overhead** — All types are `sizeof(double)`, `trivially_copyable`, no vtable
2. **Explicit construction** — `Price p = 42.0;` is a compile error; must use `Price(42.0)`
3. **Explicit extraction** — `double d = p;` is a compile error; must use `p.raw()`
4. **Type-correct arithmetic** — `Price + Price → Price`, `Price * Qty → Notional`
5. **Hot-path safe** — No allocations, no exceptions, all `noexcept constexpr`

## Semantic Types (defined in `src/core/strong_types.h`)

| Type | Underlying | Domain |
|------|-----------|--------|
| `Price` | `double` | Prices, mark prices, entry prices |
| `Qty` | `double` | Position sizes, order quantities |
| `Notional` | `double` | PnL, funding, Price × Qty products |
| `BasisPoints` | `double` | Spread, slippage, expected move |
| `OrderId` | `char[48]` | Exchange order identifiers |
| `InstrumentId` | `char[16]` | Trading pair symbols |

### Cross-Type Helpers

- `notional(Price, Qty) → Notional` — compute trade value
- `notional(Qty, Price) → Notional` — commutative overload
- `slippage_bps(Price actual, Price expected) → BasisPoints` — absolute slippage

## Modified Files

### Core Structs (`src/config/types.h`)

| Struct | Typed Fields |
|--------|-------------|
| `Signal` | `price: Price`, `qty: Qty`, `expected_pnl: Notional`, `expected_move: BasisPoints` |
| `Position` | `size: Qty`, `entry_price: Price`, `unrealized_pnl: Notional`, `realized_pnl: Notional`, `funding_impact: Notional`, `mark_price: Price` |
| `Order` | `order_id: OrderId`, `symbol: InstrumentId`, `price: Price`, `qty: Qty`, `filled_qty: Qty` |
| `RiskLimits` | `max_position_size: Qty`, `max_daily_loss: Notional` |

### Primary Engine Files

| File | Changes |
|------|---------|
| `portfolio/portfolio.h` | `update_position(Qty, Price, Side)`, `mark_to_market(Price)`, `add_realized_pnl(Notional)`, `add_funding(Notional)`, `net_pnl() → Notional`, `snapshot() → typed Position` |
| `risk_engine/risk_engine.h` | `.raw()` on Signal/Position/RiskLimits fields, `update_pnl(Notional, Notional)` |
| `risk_engine/enhanced_risk_engine.h` | Same pattern, regime-aware typed checks |
| `strategy/adaptive_position_sizer.h` | `compute() → Qty`, `.raw()` on Position fields |
| `strategy/fill_probability.h` | `estimate(Side, Price, Qty, ...)`, `optimal_price() → Price` |
| `execution_engine/order_state_machine.h` | `ManagedOrder` uses `OrderId`, `InstrumentId`, `Price`, `Qty` |
| `execution_engine/smart_execution.h` | Full typed signal → order → fill flow |
| `execution_engine/execution_engine.h` | Same pattern (v1 engine) |

### Application & Integration

| File | Changes |
|------|---------|
| `app/application.h` | Wrap raw doubles with `Price()`, `Qty()`, `Notional()`, `BasisPoints()` at signal generation; `.raw()` for cold-path logging |
| `persistence/research_recorder.h` | `.raw()` on Signal fields for `snprintf` |
| `networking/ws_manager.h` | Wrap `Notional()`, `Qty()`, `Price()` for portfolio API calls |

### Secondary Files

| File | Changes |
|------|---------|
| `rl/ppo_sac_hybrid.h` | `.raw()` on Position fields for RL state vector |
| `bridge/trading_core_api.cpp` | `bybit::Qty()`, `bybit::Notional()` for C API config; `.raw()` for position extraction |
| `config/config_loader.h` | `Qty()`, `Notional()` wrapping JSON config values |

### Test Files Updated

| File | Changes |
|------|---------|
| `tests/test_all_modules.cpp` | All RiskEngine/Portfolio tests use typed constructors and `.raw()` assertions |
| `tests/test_comprehensive.cpp` | EnhancedRiskEngine/E2E tests use typed fields |
| `tests/test_ai_modules.cpp` | FillProb/PositionSizer/Signal tests use typed args |
| `tests/test_config_persistence.cpp` | Config loader assertions use `.raw()` |
| `tests/test_order_state_machine.cpp` | OrderManager tests use `.set()`/`.c_str()` for OrderId |
| `tests/test_ppo_sac_hybrid.cpp` | RL state builder uses typed Position |
| `tests/bench_execution.cpp` | OrderId benchmark uses `.set()` |
| `tests/bench_rl.cpp` | RL benchmark uses typed Position |

### New Files

| File | Description |
|------|-------------|
| `tests/test_stage5_semantic_types.cpp` | 55 integration tests across 12 suites |

## Test Coverage

### New Stage 5 Tests (55 tests, 12 suites)

1. **TypedStructs** (6) — Default init, explicit construction for Signal, Position, RiskLimits, Order
2. **CrossType** (5) — `notional()`, `slippage_bps()` helpers, zero-price guard
3. **Portfolio** (6) — Round-trip typed APIs: update, mark-to-market, realized, funding, net_pnl, has_position
4. **Risk** (4) — Typed check_order, position exceeded, update_pnl, enhanced risk
5. **Sizer** (2) — `compute() → Qty`, volatility reduces size
6. **FillProb** (2) — Typed estimate, optimal_price returns Price
7. **Execution** (1) — Signal → Order typed flow with partial fill
8. **StringIds** (6) — OrderId/InstrumentId set, get, truncation, clear, empty
9. **ZeroCost** (12) — sizeof == double, trivially_copyable, alignof checks
10. **Arithmetic** (8) — Add, subtract, scale, compare, predicates, is_finite
11. **Integration** (2) — Full Signal → Risk → Portfolio round-trip, OrderManager typed fields
12. **CompileTimeSafety** (1) — Documents compile-error patterns

### Existing Tests — All Pass

| Suite | Tests | Status |
|-------|-------|--------|
| test_all_modules | 69 | ✅ |
| test_comprehensive | 55 | ✅ |
| test_ai_modules | 61 | ✅ |
| test_config_persistence | 15 | ✅ |
| test_order_state_machine | 55 | ✅ |
| test_ppo_sac_hybrid | 39 | ✅ |
| test_strong_types | 102 | ✅ |
| test_stage4_hot_path | 36 | ✅ |
| test_stage2_integration | 31 | ✅ |
| test_stage3_md_correctness | 33 | ✅ |
| test_chaos_engine | 33 | ✅ |
| test_deterministic_replay | 27 | ✅ |
| **test_stage5_semantic_types** | **55** | **✅** |
| **Total** | **611** | **0 failures** |

## Migration Pattern

### For typed API boundaries:
```cpp
// Before (raw doubles)
void update_position(double qty, double price, Side side);

// After (semantic types)
void update_position(Qty qty, Price price, Side side) noexcept;
```

### For internal calculations:
```cpp
// Extract raw value for arithmetic
double raw_qty = signal.qty.raw();
double raw_price = signal.price.raw();
```

### For logging/formatting:
```cpp
// spdlog cannot format semantic types directly
spdlog::info("qty={} price={}", signal.qty.raw(), signal.price.raw());
```

### For legacy/C bridge:
```cpp
// Wrap incoming raw doubles
cfg.risk.max_position_size = bybit::Qty(config->max_position_size);
// Extract for C structs
result->position_size = pos.size.raw();
```

## Performance Impact

- **Zero runtime cost** — All types are `sizeof(double)`, `trivially_copyable`
- **Constexpr arithmetic** — Optimizes identically to raw `double` operations
- **No heap allocation** — `OrderId`/`InstrumentId` are fixed-size `char[]`
- **Cache-friendly** — `Signal` and `Order` maintain `alignas(128)` for Apple Silicon
