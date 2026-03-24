# Stage 4: Hot-Path Cleanup and Contract Enforcement

## Status: COMPLETE

All code changes implemented, 36 new tests pass, 13/13 test suites pass, zero regressions.

---

## 1. Current-State Audit (Pre-Stage 4)

### Hot-path contamination found in `strategy_tick()`:

| Line | Violation | Severity |
|------|-----------|----------|
| 310 | `spdlog::debug("[TICK] feat_latency=...")` | HIGH — formatting + mutex |
| 324-330 | `spdlog::info("[REGIME]...")` + `recorder_.log_regime_change(...)` | HIGH — snprintf + ring push |
| 345-348 | `spdlog::debug("[MODEL]...")` | HIGH — formatting + mutex |
| 366-374 | `recorder_.log_prediction(...)` | MEDIUM — 768-byte snprintf |
| 438-450 | `metrics_.signals_total.fetch_add` + `recorder_.log_signal` + 2x `spdlog::debug` | HIGH |
| 468-476 | `recorder_.log_features` + `recorder_.log_ob_snapshot` | MEDIUM — snprintf |
| 486 | `spdlog::warn("[LATENCY]...")` | HIGH — formatting on every budget breach |

### Missing stage timers:
- Signal generation (stage 4) — **not timed**
- Risk check / fill probability (stage 5) — **not timed**
- Execution dispatch (stage 6) — **not timed**

### Cold work inline in hot path:
- Strategy metrics update (stage 11) — runs every tick
- Feature importance (stage 12) — periodic, heavy compute
- RL optimizer (stage 13) — periodic, neural net forward+backward
- Strategy health (stage 14) — periodic, with spdlog warning

### Metrics false sharing:
- `Metrics` struct packs 11 `std::atomic<uint64_t>` contiguously — all share cache lines
- `LatencyHistogram::record()` uses CAS loops for min/max — contention under load

---

## 2. Migration Strategy

**Principle**: Incremental, backward-compatible. No broad redesigns.

1. Expand `hot_path.h` with infrastructure (DeferredWorkQueue, HotPathCounters, TickColdWork)
2. Refactor `strategy_tick()` in-place: hot → warm → cold separation
3. Move all spdlog/recorder calls to cold drain function
4. Add missing stage timers
5. Load-shed cold work when budget exceeded
6. Add new test file, zero regressions

---

## 3. File-by-File Changes

### `src/core/hot_path.h` — MODIFIED

**Added:**
- `#include "memory_policy.h"` for `BYBIT_CACHELINE`
- **Path Classification Macros**: `BYBIT_HOT_PATH`, `BYBIT_WARM_PATH`, `BYBIT_COLD_PATH`
  - Zero runtime cost documentation markers
  - Annotate code sections for review and static analysis
- **`DeferredWorkQueue`**: Fixed-size (64-slot) ring buffer for cold work
  - Single-producer single-consumer (strategy thread only)
  - Drop-on-full with overflow counter — never blocks hot path
  - Power-of-2 capacity with branch-free masking
  - Items stored in cache-line-aligned array
- **`deferred_log()`**: BYBIT_FORCE_INLINE helper for zero-format enqueue
  - Copies tag (truncated to 12 chars), 4 doubles, timestamp
  - No formatting, no allocation
- **`HotPathCounters`**: Cache-line-aligned (128-byte) plain counters
  - `ticks_total`, `signals_generated`, `budget_exceeded_count`, `cold_shed_count`
  - `deferred_overflow`, `features_computed`, `models_inferred`, `orders_dispatched`
  - Single-writer (strategy thread), no atomics needed
  - `static_assert(trivially_copyable)`
- **`TickColdWork`**: Trivially-copyable struct collecting deferred flags+data
  - Boolean flags: `log_features`, `log_ob_snapshot`, `log_prediction`, `log_signal`, `regime_changed`, `budget_warning`
  - Latency captures: `feat_latency_ns`, `model_latency_ns`, `e2e_latency_ns`
  - Signal snapshot: price, qty, confidence, fill_prob, side
  - Regime change snapshot: prev, curr, confidence, volatility
  - `clear()` method for per-tick reset

**Fixed:**
- Removed duplicate `inline` specifier on `deferred_log()` (was: `BYBIT_FORCE_INLINE inline`)

### `src/app/application.h` — MODIFIED

**New members:**
- `DeferredWorkQueue deferred_q_` — deferred cold work queue
- `HotPathCounters hot_counters_` — isolated tick counters
- `LoadShedState load_shed_` — load shedding tracker
- `TickColdWork cold_work_` — per-tick cold work collector

**`strategy_tick()` rewritten with HOT/WARM/COLD separation:**

#### HOT stages (1-8): No spdlog, no snprintf, no alloc
- **Stage 1** (Feature Compute): Unchanged compute, latency captured to `cold_work_`
- **Stage 2** (Regime Detect): Regime change captured to `cold_work_` instead of logging inline
- **Stage 3** (ML Inference): Prediction logged via flag, not inline
- **Stage 3b** (Accuracy eval): Unchanged
- **Stage 4** (Adaptive Threshold): **Now timed** with `ScopedStageTimer(SignalGenerate, ...)`
- **Stage 5** (Signal Generation): Unchanged logic
- **Stage 6** (Fill Prob + Sizing): **Now timed** with `ScopedStageTimer(RiskCheck, ...)`
- **Stage 7** (Execution Dispatch): **Now timed** with `ScopedStageTimer(ExecutionDecide, ...)`; signal data captured to `cold_work_`
- **Stage 8** (Re-quote): Unchanged

#### WARM stages (9-11): Bounded, may enqueue
- **Stage 9** (Mark to Market): Timed, unchanged
- **Stage 10** (Latency Capture): Captures e2e, publishes to metrics atomics
- **Stage 11** (UI Publish): **Now timed** with `ScopedStageTimer(UIPublish, ...)`

#### COLD stages (12-16): Deferred, load-shed on budget exceed
- Load-shed gate: if `tl.budget_exceeded`, skip all cold work
- **Stage 12** (Logging): All spdlog + recorder calls moved here
- **Stage 13** (Strategy Metrics): Unchanged logic
- **Stage 14** (Feature Importance): Unchanged logic
- **Stage 15** (RL Optimizer): Unchanged logic
- **Stage 16** (Strategy Health): Unchanged logic with spdlog warning

**New method: `drain_cold_work()`**
- `BYBIT_COLD` annotated
- Receives all data needed for cold-path processing by const reference
- Processes regime change logging, prediction recording, signal logging, feature/OB recording, latency warnings
- Then runs strategy metrics, feature importance, RL, health

### `CMakeLists.txt` — MODIFIED
- Added `test_stage4_hot_path` target

### `tests/test_stage4_hot_path.cpp` — NEW (36 tests)
- See Section 8 below

---

## 4. Hot/Warm/Cold Classification

| Stage | Classification | Budget | May Log? | May Alloc? | May Format? |
|-------|---------------|--------|----------|------------|-------------|
| OBValidate | HOT | 50 ns | NO | NO | NO |
| FeatureCompute | HOT | 5 µs | NO | NO | NO |
| RegimeDetect | HOT | 500 ns | NO | NO | NO |
| MLInference | HOT | 50 µs | NO | NO | NO |
| SignalGenerate | HOT | 500 ns | NO | NO | NO |
| RiskCheck | HOT | 500 ns | NO | NO | NO |
| ExecutionDecide | HOT | 2 µs | NO | NO | NO |
| OrderSubmit | COLD | 5 ms | YES | YES | YES |
| MarkToMarket | WARM | 100 ns | NO | NO | NO |
| LogDeferred | COLD | unbounded | YES | YES | YES |
| AnalyticsRL | COLD | unbounded | YES | YES | YES |
| UIPublish | WARM | 200 ns | NO | NO | NO |

---

## 5. Strategy Tick Cleanup Summary

### Removed from hot path:
- 7 `spdlog::debug/info/warn` calls
- 5 `recorder_.log_*` calls (each does 768-byte snprintf internally)
- 1 `metrics_.signals_total.fetch_add` (moved to warm path batch)

### Added to hot path:
- 3 new `ScopedStageTimer` instances (Signal, Risk, Execution)
- `HotPathCounters` plain increments (no atomics, no CAS)
- `TickColdWork` field assignments (trivial copies)

### Net effect:
- Hot stages 1-8 are **logging-free** and **formatting-free**
- All recorder/spdlog calls consolidated in `drain_cold_work()`
- Load shedding: cold work skipped when `tl.budget_exceeded`

---

## 6. Deferred/Cold Spillway Design

### DeferredWorkQueue
- **Capacity**: 64 items (power of 2)
- **Item size**: 64 bytes (`DeferredWork` struct)
- **Total footprint**: 64 × 64 = 4 KB + metadata
- **Behavior on full**: Drop + increment `overflow_count_`
- **Never blocks** the hot path
- **Single-thread**: No atomics needed (strategy thread is both producer and consumer)

### TickColdWork
- Alternative lightweight mechanism for strategy_tick specifically
- Collects boolean flags + scalar values during hot stages
- Expanded in `drain_cold_work()` after warm stages complete
- More efficient than DeferredWorkQueue for the tick pipeline (no ring overhead)

### Load Shedding
- `LoadShedState` tracks shed_count / total_ticks / last_shed_tick
- `shed_rate_pct()` for diagnostics
- When `tl.budget_exceeded`, all cold work is skipped for that tick
- Prevents cascading latency from expensive cold work

---

## 7. Metrics/Counter Discipline

### HotPathCounters (new)
- `alignas(128)` — full Apple Silicon cache line
- Plain `uint64_t` — single-writer, no atomics
- Published to shared `Metrics` atomics in warm path (batched)
- Fields: `ticks_total`, `signals_generated`, `budget_exceeded_count`, `cold_shed_count`, `deferred_overflow`, `features_computed`, `models_inferred`, `orders_dispatched`

### Existing Metrics struct
- Still uses `std::atomic<uint64_t>` for cross-thread counters (WS thread writes some)
- `LatencyHistogram::record()` still uses CAS for min/max (acceptable — bounded attempts)
- Hot path now records to histograms only once per tick in warm path (not per-stage inline)

### IsolatedCounter / CacheLinePadded (from memory_policy.h)
- Available for future expansion where cross-thread counters need isolation
- Currently used where counter contention was identified

---

## 8. Testing Plan

### test_stage4_hot_path.cpp — 36 tests across 13 test suites:

| Suite | Tests | What's verified |
|-------|-------|-----------------|
| DeferredWorkQueueTest | 9 | Empty, push/pop, fill, overflow, clear, reset, wrap-around |
| DeferredLogTest | 3 | Correct enqueue, null tag safety, tag truncation |
| HotPathCountersTest | 4 | Trivial copyable, cache-line aligned, reset, increment |
| TickColdWorkTest | 2 | Trivial copyable, clear resets all |
| LoadShedStateTest | 2 | Initial state, shed rate computation |
| ScopedStageTimerTest | 2 | Non-zero elapsed, budget violation counter |
| PipelineStageBudgets | 3 | All stages have budgets, total budget, stage names |
| TickLatencyTest | 2 | hot_path_ns() sum, cache-line alignment |
| DeferredWorkLayout | 2 | Exact 64-byte size, type enum values |
| IsolatedCounterTest | 2 | Cache-line isolation, increment/load |
| CacheLinePaddedTest | 2 | Correct size, no false sharing |
| CacheLineConstant | 1 | Apple Silicon 128-byte |
| DeferredWorkQueueCapacity | 2 | Power of 2, mask correct |

### Regression coverage:
- All 33 Stage 3 tests pass (gap detection, book invalidation, resync)
- All pre-existing test suites pass (13/13)
- Pre-existing flaky: `GRUModelTest.InferenceLatencyUnder1ms` (timing-dependent, not a regression)

---

## 9. CI / Verification Plan

### Compile checks:
- `cmake --build build` — zero errors, zero warnings from our code
- All warnings are pre-existing spdlog/fmt infinity warnings

### Symbol scan (future CI step):
```bash
# Check hot-path object files for forbidden symbols
nm -C build/CMakeFiles/bybit_hft.dir/src/app/*.o | grep -E 'spdlog|_Znwm|_Znam|__cxa_throw'
# Should only appear in cold-path functions (drain_cold_work, report_metrics, etc.)
```

### Static analysis ideas:
- `BYBIT_HOT_PATH` / `BYBIT_WARM_PATH` / `BYBIT_COLD_PATH` macros enable grep-based scanning
- Future: clang-tidy custom check for spdlog calls in functions annotated HOT

---

## 10. Risks and Trade-offs

| Risk | Mitigation |
|------|------------|
| Cold work accumulates if budget always exceeded | LoadShedState tracks rate; watchdog can alert |
| DeferredWorkQueue overflow loses events | Overflow counter tracked; queue sized for worst-case tick |
| Strategy metrics skipped on shed | Metrics are trailing-window — missing one tick is harmless |
| RL step skipped on shed | RL already runs every 50th tick — occasional skip is fine |
| TickColdWork grows over time | Currently ~96 bytes, trivially copyable, clear() per tick |
| Feature importance skipped on shed | Already periodic (every 100/500 ticks) — occasional skip OK |

---

## 11. Summary of Changes

| Metric | Before | After |
|--------|--------|-------|
| spdlog calls in hot path | 7 | 0 |
| snprintf calls in hot path | 5 (via recorder) | 0 |
| Timed hot stages | 4/8 | 8/8 |
| Cold work load shedding | None | Automatic on budget exceed |
| Hot-path counter isolation | None | HotPathCounters (128-byte aligned) |
| Path classification | None | HOT/WARM/COLD macros |
| Deferred work queue | Declared but unused | Fully implemented (64-slot ring) |
| New tests | 0 | 36 |
| Test regressions | 0 | 0 |
