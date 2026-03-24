# Stage 1 + Stage 2 Implementation Summary

## Overview

Incremental refactoring of timing, monitoring, and replay infrastructure to use:
- **Strong semantic types** (`TimestampNs`, `DurationNs`, `TscTicks`)
- **Clock injection** via template parameters (static dispatch, zero vtable overhead)
- **Memory policy** integration (`BYBIT_CACHELINE`, `BYBIT_VERIFY_TRIVIAL`)
- **Pipeline stage separation** (`WatchdogStage` for monitoring vs `PipelineStage` for hot-path instrumentation)

All 118+ existing `Clock::now_ns()` call sites remain valid. Zero breaking changes.

---

## Files Modified

### Stage 1 — Foundational Headers (pre-existing, integrated)

| File | Purpose |
|------|---------|
| `src/core/strong_types.h` | `TimestampNs`, `DurationNs`, `TscTicks` with `.raw()` accessor |
| `src/core/memory_policy.h` | `BYBIT_CACHELINE`, `BYBIT_VERIFY_TRIVIAL`, alignment macros |
| `src/core/clock_source.h` | `TscClockSource`, `ReplayClockSource`, `MockClockSource`, `ClockFn`, `ClockSourceConcept` |
| `src/core/hot_path.h` | `PipelineStage` (12 stages), `ScopedStageTimer`, `TickLatency`, `BYBIT_HOT` |

### Stage 2 — Refactored Files

| File | Changes |
|------|---------|
| `src/utils/tsc_clock.h` | Added `now_typed()`, `ticks_typed()`, `elapsed_typed()` wrappers |
| `src/utils/clock.h` | Added `Clock::now_typed()`, `Clock::ticks_typed()`, `Clock::elapsed_typed()` overloads |
| `src/config/types.h` | `#include memory_policy.h`, `BYBIT_CACHELINE` macro compat alias |
| `src/monitoring/blackbox_recorder.h` | Templated on `ClockSource`, `clock_.now().raw()`, `BYBIT_VERIFY_TRIVIAL(EventRecord)` |
| `src/monitoring/watchdog.h` | `PipelineStage` → `WatchdogStage`, templated `HeartbeatRegistry<ClockSource>` and `Watchdog<ClockSource>` |
| `src/monitoring/perf_signpost.h` | All `PipelineStage` refs → `WatchdogStage`, `stage_name()` → `watchdog_stage_name()` |
| `src/monitoring/deterministic_replay.h` | Templated `ReplayEngine<WallClock>`, `wall_clock_.now().raw()` for pacing |
| `src/app/application.h` | `#include hot_path.h`, `ScopedStageTimer` instrumentation in `strategy_tick()`, `TickLatency` tracking |

### Tests Modified/Created

| File | Changes |
|------|---------|
| `tests/test_chaos_engine.cpp` | `PipelineStage` → `WatchdogStage` (20 references) |
| `tests/test_stage2_integration.cpp` | **NEW** — 31 tests covering clock injection, templates, typed API |

### Build Config

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Added `test_stage2_integration` target |

---

## Key Design Decisions

### 1. Two Distinct Stage Enums
- **`WatchdogStage`** (8 values) — coarse monitoring stages (WebSocket, Parser, OrderBook, etc.)
- **`PipelineStage`** (12 values) — fine-grained hot-path stages (OBValidate, FeatureCompute, MLInference, etc.)
- No alias between them — prevents accidental collision.

### 2. Clock Injection via Templates (not virtuals)
```cpp
template <typename ClockSource = TscClockSource>
class HeartbeatRegistry { ... };

template <typename ClockSource = TscClockSource>
class Watchdog { ... };

template <size_t Capacity = 65536, typename ClockSource = TscClockSource>
class BlackBoxRecorder { ... };

template <typename WallClock = TscClockSource>
class ReplayEngine { ... };
```
- Default template args = zero call-site changes for production code.
- `MockClockSource` for deterministic unit tests.
- `ReplayClockSource` (atomic) for thread-safe replay injection.

### 3. Backward Compatibility
- All 118+ `Clock::now_ns()` call sites unchanged.
- Raw `uint64_t` APIs preserved alongside typed overloads.
- `BYBIT_CACHELINE` macro kept as alias to `memory_policy::kCacheLineSize`.

### 4. Strategy Tick Instrumentation
- `ScopedStageTimer` wraps FeatureCompute, RegimeDetect, MLInference, MarkToMarket.
- `TickLatency` struct tracks per-stage ns breakdown + budget exceeded flag.
- `last_tick_latency_` member for UI snapshot publishing.

---

## Test Results

```
12/12 test suites passed, 0 failures
Total: ~420 individual tests across all suites

New Stage 2 integration tests: 31/31 passed
  - ClockTypedAPI (5)
  - BlackBoxRecorderMock (4)
  - HeartbeatRegistryMock (4)
  - ReplayEngineMock (1)
  - TickLatencyTest (3)
  - StageEnumTest (2)
  - ReplayClockSourceTest (3)
  - MockClockSourceTest (3)
  - ClockFnTest (3)
  - EventRecordTest (3)
```

---

## Migration Path (Stage 3+)

1. **Remaining `TscClock::now_ns()` direct calls** (60 across 15 files) — migrate to `Clock::now_typed()` or inject `ClockSource` template parameter.
2. **Remaining `Clock::now_ns()` calls** (118 across 37 files) — gradually replace with typed overloads where type safety adds value.
3. **UISnapshot** — add `TickLatency` field for per-stage breakdown in GUI.
4. **Replay integration** — wire `ReplayClockSource` through `BlackBoxRecorder<N, ReplayClockSource>` + `ReplayEngine<>` for full deterministic replay.
5. **CI enforcement** — add `nm` scan for allocation symbols in hot-path `.o` files.
