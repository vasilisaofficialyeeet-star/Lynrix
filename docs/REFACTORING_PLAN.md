# Lynrix C++ Trading Engine - Refactoring Plan

## Executive Summary

### Current State (Post-Audit)

The engine is a single-threaded, timer-driven pipeline running inside a Boost.Asio io_context.
The hot path (strategy_tick in application.h) executes 15 stages sequentially on every 10 ms tick:

OB validate - Feature compute - Regime detect - ML inference - Accuracy track -
Adaptive threshold - Signal gen - Fill prob - Position size - Smart exec -
Requote/cancel - Mark-to-market - Log - Analytics/RL - UI snapshot publish

#### What already works well

- Cache-line alignment (alignas(128)) on Apple Silicon throughout
- FixedPrice int64 representation in OrderBook (eliminates epsilon comparisons)
- Branchless regime LUTs, NEON-vectorized imbalance/qty sums
- TSC-based timing (mach_absolute_time / cntvct_el0, ~2 ns)
- Lock-free primitives: SPSC queue, SeqLock, TripleBuffer, PipelineConnector
- Pre-allocated order slots, arena allocator, object pools
- Order FSM with compile-time transition table (O(1) branchless)
- BlackBox recorder + deterministic replay with CRC32

#### What needs hardening

| # | Priority | Current Gap | Risk |
|---|----------|-------------|------|
| 1 | Hot path contract | No compile-time enforcement; spdlog, nlohmann json parse, std string in callbacks live inside hot path | Silent latency blowups; 1ms+ spikes |
| 2 | Memory model | double prices in Signal/Order/Position cross cache lines; OrderBook bids() mutates mutable legacy buffers in const; Metrics atomics not padded | False sharing; torn reads |
| 3 | Strong types | Prices, quantities, timestamps, order IDs are raw double/uint64_t/char[] | Unit mismatch bugs; wrong-argument-order |
| 4 | FSM design | ManagedOrder FSM exists but SmartExecutionEngine uses raw Order + manual active_order_count; risk is procedural | State desync; missed edge cases |
| 5 | Backpressure | PipelineConnector drops silently; no per-stage overload policy | Undetected message loss |
| 6 | Dual-mode determinism | ReplayEngine doesnt inject into pipeline; Clock leaks wall-time into replay | Replay divergence |

---

## Stage 1 Implementation Order

### Dependency Graph

P3 (Strong Types) is the foundation - all others depend on it.

Phase 0: P3 Strong Types (2 days) - no dependencies
Phase 1: P1 Hot Path Contract (2 days) - depends on P3
Phase 2: P2 Memory Model (1 day) - depends on P1
Phase 3: P4 FSM Design (3 days) - depends on P3, P1
Phase 4: P5 Backpressure (2 days) - depends on P4
Phase 5: P6 Determinism (3 days) - depends on all above

Total: ~13 engineering days for Stage 1.

### Incremental Rollout Strategy

1. Each priority is behind a feature flag (constexpr bool)
2. Old types coexist via using-alias toggle
3. Each phase has: code changes + new tests + benchmark comparison
4. No phase breaks existing 234 tests

---

## Priority 1: Hot Path Contract

### Problem

strategy_tick() is 250 lines mixing hot-path computation with cold-path logging,
persistence, analytics, and RL. There is no compile-time enforcement that hot stages
do not allocate, throw, or block.

### Specific Violations Found

1. spdlog::debug/info calls in every stage (hot path logging)
2. nlohmann::json::parse in submit_live_order callback (heap alloc + exception)
3. std::string construction in rest callback lambdas
4. recorder_.log_* calls interleaved with computation
5. strategy_metrics_.snapshot() called 4x in RL section (copies struct each time)
6. feature_importance_.record_sample + compute every 100/500 ticks in hot loop

### Design

#### 1.1 Hot Path Annotation Macros

File: src/core/hot_path.h

```cpp
#pragma once
#define BYBIT_HOT   __attribute__((hot, noinline))
#define BYBIT_COLD  __attribute__((cold, noinline))

// Contract: functions marked BYBIT_HOT must:
// - Return HotResult or void
// - Never call new/delete/malloc/free
// - Never throw or catch exceptions
// - Never call spdlog/printf/iostream
// - Never call std::string constructors
// - Never perform blocking I/O
// - Complete within stated latency budget

struct HotResult {
    bool ok;
    const char* error; // static literal only
};
```

#### 1.2 Pipeline Stage Decomposition

Split strategy_tick() into discrete stages with latency budgets:

| Stage | Function | Budget | Hot/Cold |
|-------|----------|--------|----------|
| 0 | ob_validate() | 50 ns | HOT |
| 1 | compute_features() | 5 us | HOT |
| 2 | detect_regime() | 1 us | HOT |
| 3 | ml_inference() | 50 us ONNX / 10 us GRU | HOT |
| 4 | generate_signal() | 1 us | HOT |
| 5 | risk_check() | 1 us | HOT |
| 6 | compute_execution() | 5 us | HOT |
| 7 | submit_order() | varies (REST) | COLD boundary |
| 8 | mark_to_market() | 100 ns | HOT |
| --- | --- | --- | --- |
| 9 | log_tick() | unbounded | COLD |
| 10 | update_analytics() | unbounded | COLD |
| 11 | update_rl() | unbounded | COLD |
| 12 | publish_ui_snapshot() | 500 ns | HOT (SeqLock) |

Key: Stages 0-8 and 12 are the hot contract. Stages 9-11 are deferred to cold path.

#### 1.3 Deferred Cold Work Queue

```cpp
// Instead of inline spdlog/recorder calls in hot path:
struct DeferredWork {
    enum Type : uint8_t { Log, Record, Analytics, RL };
    Type type;
    uint64_t timestamp_ns;
    double values[8];
};

// SPSC queue drained after hot path completes or on separate timer
SPSCQueue<DeferredWork, 4096> cold_work_queue_;
```

#### 1.4 Enforcement

- clang-tidy custom check: warn on new/delete/malloc in functions with BYBIT_HOT
- CI benchmark gate: strategy_tick p99 must stay below 100 us
- Debug-mode assertion: arena high-water check after each tick

### Migration

1. Extract stages 0-8 into free functions taking explicit parameters
2. Move all spdlog calls to post-tick cold section
3. Replace inline recorder calls with cold_work_queue_.try_push()
4. Add ScopedTscTimer to each stage for per-stage latency tracking

---

## Priority 2: Memory Model and Cache Discipline

### Problem

Several hot-path structs have suboptimal layout:

1. **Metrics struct**: atomic counters not on separate cache lines = false sharing
   when WS thread increments ob_updates while strategy thread reads signals_total

2. **OrderBook::bids()/asks()**: const methods mutate mutable legacy_bids_ arrays.
   If called from UI thread while strategy thread updates, this is a data race.

3. **Signal/Order/Features**: all alignas(128) but contain double fields that may
   straddle padding boundaries without clear hot/cold separation.

4. **Application class**: 30+ member variables with no padding between hot and cold
   sections. strategy_tick touches ob_, tf_, feature_engine_, risk_, exec_ all in
   sequence - good locality, but metrics_ atomics sit between them.

### Design

#### 2.1 Metrics Struct Padding

```cpp
struct alignas(128) Metrics {
    // -- Cache line 0: WS thread writes --
    alignas(128) std::atomic<uint64_t> ob_updates_total{0};
    alignas(128) std::atomic<uint64_t> trades_total{0};
    alignas(128) std::atomic<uint64_t> ws_reconnects_total{0};

    // -- Cache line 3+: Strategy thread writes --
    alignas(128) std::atomic<uint64_t> signals_total{0};
    alignas(128) std::atomic<uint64_t> orders_sent_total{0};
    alignas(128) std::atomic<uint64_t> orders_filled_total{0};
    alignas(128) std::atomic<uint64_t> orders_cancelled_total{0};

    // -- Histograms (single-writer, strategy thread) --
    LatencyHistogram end_to_end_latency;
    LatencyHistogram feature_calc_latency;
    LatencyHistogram model_inference_latency;
    LatencyHistogram risk_check_latency;
    LatencyHistogram order_submit_latency;
    LatencyHistogram ws_message_latency;
    LatencyHistogram ob_update_latency;
};
```

#### 2.2 Eliminate mutable Legacy Buffers

OrderBook::bids()/asks() currently mutate mutable arrays - a data race if called
concurrently. Solution: remove legacy accessors entirely; all consumers use
fill_bids()/fill_asks() with caller-owned buffers, or compact_bids()/compact_asks().

```cpp
// REMOVE these:
// const PriceLevel* bids() const noexcept { ... mutates legacy_bids_ ... }
// mutable std::array<PriceLevel, MAX_OB_LEVELS> legacy_bids_{};

// KEEP only:
const CompactLevel* compact_bids() const noexcept;
size_t fill_bids(PriceLevel* out, size_t max_n) const noexcept; // caller owns buffer
```

#### 2.3 Application Member Layout

Group by access pattern and insert padding:

```cpp
class Application {
    // -- Group 1: Hot path data (touched every tick) --
    alignas(128) OrderBook ob_;
    TradeFlowEngine tf_;
    AdvancedFeatureEngine feature_engine_;
    RegimeDetector regime_detector_;
    AdaptiveThreshold adaptive_threshold_;
    FillProbabilityModel fill_prob_model_;
    AdaptivePositionSizer position_sizer_;
    ModelOutput last_prediction_;

    // -- Padding between hot data and hot engines --
    char pad0_[128];

    // -- Group 2: Hot path engines (touched every tick with signal) --
    EnhancedRiskEngine risk_;
    SmartExecutionEngine exec_;
    Portfolio portfolio_;

    // -- Padding --
    char pad1_[128];

    // -- Group 3: Metrics (cross-thread atomics) --
    Metrics metrics_;

    // -- Group 4: Cold path (analytics, RL, persistence) --
    // ... everything else ...
};
```

### Testing

- cachegrind/perf stat to measure L1 miss rate before/after
- Benchmark: strategy_tick p50/p99 regression test
- ThreadSanitizer run to verify no data races

---

## Priority 3: Fixed-Point and Strong Types

### Problem

Every numeric parameter in the hot path is raw double or uint64_t. This allows:
- Passing a price where a quantity is expected (compiles fine, wrong at runtime)
- Mixing basis points (0.01 = 1 bps) with fractions (0.0001 = 1 bps)
- Passing millisecond timestamp where nanosecond is expected
- Order ID as char[48] with no length safety

### Design

File: src/core/strong_types.h

#### 3.1 Zero-Cost Strong Numeric Type

```cpp
template <typename Tag, typename Rep = double>
struct StrongNumeric {
    Rep value{};

    constexpr StrongNumeric() noexcept = default;
    constexpr explicit StrongNumeric(Rep v) noexcept : value(v) {}

    // Arithmetic only with same type
    constexpr StrongNumeric operator+(StrongNumeric o) const noexcept { return StrongNumeric{value + o.value}; }
    constexpr StrongNumeric operator-(StrongNumeric o) const noexcept { return StrongNumeric{value - o.value}; }
    constexpr StrongNumeric operator*(Rep scalar) const noexcept { return StrongNumeric{value * scalar}; }
    constexpr StrongNumeric operator/(Rep scalar) const noexcept { return StrongNumeric{value / scalar}; }

    // Comparison
    constexpr bool operator==(StrongNumeric o) const noexcept { return value == o.value; }
    constexpr bool operator<(StrongNumeric o) const noexcept { return value < o.value; }
    constexpr bool operator>(StrongNumeric o) const noexcept { return value > o.value; }
    constexpr bool operator<=(StrongNumeric o) const noexcept { return value <= o.value; }
    constexpr bool operator>=(StrongNumeric o) const noexcept { return value >= o.value; }
    constexpr auto operator<=>(StrongNumeric o) const noexcept = default;

    // Explicit conversion
    constexpr Rep raw() const noexcept { return value; }

    // Zero check
    constexpr bool is_zero() const noexcept {
        if constexpr (std::is_floating_point_v<Rep>)
            return value < Rep(1e-12) && value > Rep(-1e-12);
        else
            return value == Rep{};
    }
};
```

#### 3.2 Concrete Types

```cpp
// Tags
struct PriceTag {};
struct QtyTag {};
struct BpsTag {};
struct FractionTag {};
struct NanosTag {};
struct MillisTag {};

// Types
using Price       = StrongNumeric<PriceTag, double>;
using Qty         = StrongNumeric<QtyTag, double>;
using BasisPoints = StrongNumeric<BpsTag, double>;
using Fraction    = StrongNumeric<FractionTag, double>;
using TimestampNs = StrongNumeric<NanosTag, uint64_t>;
using DurationNs  = StrongNumeric<NanosTag, uint64_t>;  // same tag = interoperable
using TimestampMs = StrongNumeric<MillisTag, uint64_t>;

// Cross-type operations (explicit)
inline Price operator*(Price p, Fraction f) noexcept { return Price{p.raw() * f.raw()}; }
inline double operator/(Price a, Price b) noexcept { return a.raw() / b.raw(); } // dimensionless
inline Price operator+(Price p, BasisPoints bps) noexcept {
    return Price{p.raw() * (1.0 + bps.raw() / 10000.0)};
}
inline BasisPoints price_diff_bps(Price a, Price b) noexcept {
    return BasisPoints{(a.raw() - b.raw()) / b.raw() * 10000.0};
}
```

#### 3.3 OrderId and InstrumentId

```cpp
struct OrderId {
    char data[48] = {};

    OrderId() noexcept = default;
    explicit OrderId(const char* s) noexcept {
        std::strncpy(data, s, sizeof(data) - 1);
    }
    bool operator==(const OrderId& o) const noexcept {
        return std::strcmp(data, o.data) == 0;
    }
    bool empty() const noexcept { return data[0] == '\0'; }
    const char* c_str() const noexcept { return data; }
};

struct InstrumentId {
    char data[16] = {};
    // same interface as OrderId
};
```

#### 3.4 Migration Strategy

Phase 1: Create strong_types.h with all type definitions
Phase 2: Add using aliases in types.h (default to raw types via feature flag)
Phase 3: Convert Signal, Order, Position, Features structs one at a time
Phase 4: Enable strong types, fix all compile errors
Phase 5: Run full test suite, benchmark comparison

### Zero-Cost Verification

static_assert(sizeof(Price) == sizeof(double));
static_assert(alignof(Price) == alignof(double));
// Compiler generates identical assembly for Price{x} + Price{y} vs x + y

---

## Priority 4: State Machine Design

### Problem

SmartExecutionEngine has a ManagedOrder type with proper FSM but does not use it.
Instead, it operates on raw Order structs with manual active_order_count_ tracking.
The risk engine is purely procedural (check_order returns pass/fail) with no state
machine for the risk lifecycle itself.

### Design

#### 4.1 Migrate SmartExecutionEngine to OrderManager

The OrderManager class already exists in order_state_machine.h with proper FSM.
SmartExecutionEngine should replace its internal Order array with OrderManager:

```cpp
// BEFORE (smart_execution.h):
std::array<Order, MAX_OPEN_ORDERS> active_orders_{};
size_t active_order_count_ = 0;

// AFTER:
OrderManager order_mgr_;
```

Every order state change flows through apply_event():
- on_signal: alloc() + apply_event(Submit)
- REST ACK: apply_event(Ack)
- Fill report: apply_event(Fill) or apply_event(PartialFill)
- Cancel: apply_event(CancelReq), then apply_event(CancelAck)
- Timeout: apply_event(Timeout)

#### 4.2 Risk Engine FSM

Current risk engine is stateless check_order(). Add a proper state machine:

```cpp
enum class RiskState : uint8_t {
    Normal       = 0,  // All limits nominal
    Elevated     = 1,  // Approaching limits (>70% of any threshold)
    Restricted   = 2,  // Reduce-only mode (>90% of threshold)
    Halted       = 3,  // Circuit breaker tripped
    Cooldown     = 4,  // Post-halt recovery period
    Emergency    = 5,  // External emergency stop
};

enum class RiskEvent : uint8_t {
    Tick         = 0,  // Periodic PnL update
    OrderPass    = 1,  // Order passed risk check
    OrderReject  = 2,  // Order rejected
    ThresholdWarn= 3,  // Approaching a threshold
    ThresholdHit = 4,  // Threshold breached
    CooldownEnd  = 5,  // Cooldown timer expired
    ManualReset  = 6,  // Operator override
    EmergencyStop= 7,  // External emergency
};
```

The transition table maps (RiskState, RiskEvent) to RiskState, identical pattern
to the order FSM. Each state determines what actions are allowed:

- Normal: all orders pass (subject to per-order checks)
- Elevated: warning logs, position scaling reduced to 50%
- Restricted: reduce-only orders, no new positions
- Halted: all orders rejected, cancel-all fired
- Cooldown: all orders rejected, timer running
- Emergency: all orders rejected, require manual reset

#### 4.3 Circuit Breaker Integration

CircuitBreaker currently has an ad-hoc update() method that calls trip().
Replace with explicit state transitions fed by the risk FSM:

```
CircuitBreaker.update(pnl, delta) =>
  if drawdown > warn_threshold => RiskEvent::ThresholdWarn
  if drawdown > trip_threshold => RiskEvent::ThresholdHit
  if cooldown_expired          => RiskEvent::CooldownEnd
```

### Testing

- Property test: no sequence of valid events produces an illegal state
- Fuzz test: random event sequences never crash or leak orders
- Replay test: recorded production event stream reproduces exact state sequence

---

## Priority 5: Backpressure and Overload Handling

### Problem

PipelineConnector silently drops messages when the SPSC queue is full. There is no:
- Per-stage overload policy (drop oldest? drop newest? apply backpressure?)
- Load shedding (skip analytics/RL when behind)
- Burst absorption (WS message burst can overwhelm OB processing)
- Monitoring of queue fill levels

### Design

#### 5.1 Overload Policy per Stage

```cpp
enum class OverloadPolicy : uint8_t {
    DropNewest,     // Discard incoming (current behavior)
    DropOldest,     // Advance tail, discard stale data
    Backpressure,   // Block producer (only for non-critical paths)
    LoadShed,       // Skip non-essential work
};

struct StageConfig {
    const char* name;
    OverloadPolicy policy;
    size_t warn_threshold_pct;  // log warning at this % full
    size_t shed_threshold_pct;  // begin load shedding at this %
};
```

Per-stage policies:

| Stage | Policy | Rationale |
|-------|--------|-----------|
| WS Message Ingestion | DropOldest | Stale OB deltas are worthless |
| Feature Compute | LoadShed | Skip analytics features, keep core 7 |
| ML Inference | LoadShed | Use cached prediction if behind |
| Signal Generation | DropNewest | Never generate stale signals |
| Order Submission | Backpressure | Orders must not be lost |
| Analytics/RL | DropNewest | Non-critical, catch up later |

#### 5.2 Load Shedding in strategy_tick

```cpp
void strategy_tick() {
    uint64_t start_ns = Clock::now_ns();

    // Core hot path (always runs)
    auto features = compute_features(ob_, tf_);     // Stage 1
    auto regime = detect_regime(features);           // Stage 2
    auto prediction = ml_inference(features);        // Stage 3
    auto signal = generate_signal(prediction, regime, features); // Stage 4

    if (signal.has_value()) {
        risk_check_and_execute(signal.value(), ob_); // Stage 5-7
    }

    mark_to_market();                                // Stage 8

    // Load-shed boundary: skip if over budget
    uint64_t elapsed = Clock::now_ns() - start_ns;
    bool time_budget_ok = elapsed < 80'000; // 80 us of 100 us budget

    if (time_budget_ok) {
        update_analytics();                          // Stage 10
        update_rl();                                 // Stage 11
    } else {
        ++shed_count_;
    }

    publish_ui_snapshot();                           // Stage 12 (always)
    log_tick_deferred();                             // Stage 9 (queued)
}
```

#### 5.3 Queue Health Monitoring

```cpp
struct QueueHealth {
    size_t current_size;
    size_t capacity;
    uint64_t total_sent;
    uint64_t total_dropped;
    double fill_pct() const { return 100.0 * current_size / capacity; }
    double drop_rate() const {
        uint64_t total = total_sent + total_dropped;
        return total > 0 ? 100.0 * total_dropped / total : 0.0;
    }
};
```

Expose queue health in UISnapshot for the System Monitor view.

---

## Priority 6: Dual-Mode Determinism

### Problem

The replay system can replay BlackBox events but cannot inject them into the live
pipeline for bit-exact verification. Clock::now_ns() always reads wall-clock,
making replay non-deterministic. There is no audit log of trading decisions.

### Design

#### 6.1 Injected Clock

```cpp
class IClock {
public:
    virtual ~IClock() = default;
    virtual uint64_t now_ns() const noexcept = 0;
    virtual uint64_t now_ticks() const noexcept = 0;
};

class WallClock final : public IClock {
public:
    uint64_t now_ns() const noexcept override { return TscClock::now_ns(); }
    uint64_t now_ticks() const noexcept override { return TscClock::now(); }
};

class ReplayClock final : public IClock {
    uint64_t current_ns_ = 0;
public:
    void advance_to(uint64_t ns) noexcept { current_ns_ = ns; }
    uint64_t now_ns() const noexcept override { return current_ns_; }
    uint64_t now_ticks() const noexcept override { return current_ns_; }
};
```

IMPORTANT: The virtual dispatch cost (~2-5 ns) is acceptable because:
1. Clock is called ~15x per tick, so total overhead is ~30-75 ns on 100 us budget
2. In production mode, LTO + devirtualization eliminates it entirely
3. Alternatively, use a function pointer (1 indirection) instead of vtable

#### 6.2 Decision Audit Log

Every trading decision is recorded with inputs and outputs:

```cpp
struct DecisionRecord {
    TimestampNs timestamp;
    uint32_t    tick_id;

    // Inputs
    Price       best_bid;
    Price       best_ask;
    double      model_prob_up;
    double      model_prob_down;
    double      threshold;
    MarketRegime regime;

    // Decision
    bool        signal_generated;
    Side        signal_side;
    Price       signal_price;
    Qty         signal_qty;

    // Risk
    bool        risk_passed;
    const char* risk_reject_reason;
};
```

Written to a separate ring buffer (not BlackBox - that is for raw events).
On replay, compare DecisionRecord sequences for bit-exact match.

#### 6.3 Replay-Through-Pipeline

```cpp
class PipelineReplay {
public:
    void load(const std::string& event_file, const std::string& decision_file);

    // Feed events through the actual pipeline stages
    void run(Application& app) {
        ReplayClock clock;
        app.set_clock(&clock);

        for (const auto& event : events_) {
            clock.advance_to(event.timestamp_ns);

            switch (event.type) {
                case EventType::OBDelta:
                    app.inject_ob_delta(event);
                    break;
                case EventType::Trade:
                    app.inject_trade(event);
                    break;
                // ... etc
            }
        }

        // Compare decisions
        auto& actual = app.decision_log();
        bool match = DecisionLog::compare(actual, expected_decisions_);
        assert(match && "Replay divergence detected");
    }
};
```

### Testing

- Record 1000 ticks in production mode
- Replay with ReplayClock: verify bit-exact DecisionRecord match
- Inject synthetic clock drift: verify detection
- Fuzz: random event streams produce consistent decisions across runs

---

## Target Module Structure

```
src/
  core/                     # NEW: foundational types and contracts
    strong_types.h          # Price, Qty, BasisPoints, TimestampNs, etc.
    hot_path.h              # BYBIT_HOT, BYBIT_COLD, HotResult
    clock_interface.h       # IClock, WallClock, ReplayClock
    decision_log.h          # DecisionRecord, DecisionLog
    pipeline_stage.h        # Stage enum, StageConfig, latency budgets
    backpressure.h          # OverloadPolicy, QueueHealth, LoadShedder

  config/
    types.h                 # MODIFIED: use strong types, Metrics padding
    config_loader.h         # unchanged

  utils/
    arena_allocator.h       # unchanged
    lockfree_pipeline.h     # MODIFIED: add OverloadPolicy to PipelineConnector
    ring_buffer.h           # unchanged
    spinlock.h              # unchanged
    clock.h                 # MODIFIED: delegate to IClock interface
    tsc_clock.h             # unchanged
    thread_affinity.h       # unchanged
    memory_pool.h           # unchanged
    fast_double.h           # unchanged
    hmac.h                  # unchanged

  orderbook/
    orderbook.h             # MODIFIED: remove mutable legacy buffers, use Price type
    orderbook_v3.h          # unchanged

  execution_engine/
    order_state_machine.h   # MODIFIED: use strong types in ManagedOrder
    smart_execution.h       # MODIFIED: use OrderManager, remove raw Order array
    execution_engine.h      # DEPRECATED: superseded by smart_execution.h

  risk_engine/
    risk_engine.h           # DEPRECATED: superseded by enhanced
    enhanced_risk_engine.h  # MODIFIED: add RiskState FSM
    var_engine.h            # unchanged

  monitoring/
    blackbox_recorder.h     # unchanged
    deterministic_replay.h  # MODIFIED: add PipelineReplay mode
    chaos_engine.h          # unchanged
    others...               # unchanged

  (all other modules unchanged in Stage 1)
```

---

## Implementation-Ready Artifacts

### Artifact 1: src/core/strong_types.h (Phase 0)

See Priority 3 section above for complete implementation.

### Artifact 2: src/core/hot_path.h (Phase 1)

See Priority 1 section above for complete implementation.

### Artifact 3: Metrics padding fix (Phase 2)

See Priority 2 section above for the padded Metrics struct.

### Artifact 4: SmartExecutionEngine migration (Phase 3)

Replace active_orders_ array with OrderManager.
Each order interaction becomes: find() + apply_event() + check is_terminal().

### Artifact 5: Risk FSM (Phase 3)

Add RiskState enum and transition table to enhanced_risk_engine.h.
check_order() consults state before per-order limits.

### Artifact 6: Load shedding (Phase 4)

Add elapsed-time check after Stage 8, skip analytics/RL if over budget.
Add shed_count_ to Metrics and UISnapshot.

### Artifact 7: IClock + DecisionLog (Phase 5)

Inject IClock into Application constructor.
Record DecisionRecord after each signal generation.

---

## Tests and Benchmarks

### New Test Files

| File | What it tests |
|------|---------------|
| tests/test_strong_types.cpp | Type safety: compile-time rejection of Price+Qty |
| tests/test_hot_path_contract.cpp | Latency budgets: each stage under declared budget |
| tests/test_risk_fsm.cpp | Property: no illegal state reachable |
| tests/test_backpressure.cpp | Queue overflow policies, load shedding triggers |
| tests/test_deterministic_replay.cpp | Bit-exact decision replay |
| tests/bench_strategy_tick.cpp | E2E p50/p99 with all stages |

### CI Invariants

1. All 234 existing tests pass (zero regressions)
2. bench_strategy_tick p99 < 100 us
3. ThreadSanitizer: zero data races
4. AddressSanitizer: zero heap use in BYBIT_HOT functions (sampled)

---

## Migration Roadmap

### Phase 0: Strong Types (Days 1-2)
- Create src/core/strong_types.h
- Add feature flag to types.h
- Convert Signal and Order structs
- Run tests, fix compile errors
- Benchmark: verify zero overhead

### Phase 1: Hot Path Contract (Days 3-4)
- Create src/core/hot_path.h
- Extract stages from strategy_tick() into named functions
- Add DeferredWork queue for cold-path logging
- Move spdlog calls out of hot stages
- Add per-stage ScopedTscTimer
- Benchmark: compare p50/p99

### Phase 2: Memory Model (Day 5)
- Pad Metrics atomics
- Remove OrderBook mutable legacy buffers
- Update all bids()/asks() callers to use fill_bids()/compact_bids()
- Reorder Application members
- Run cachegrind, verify L1 improvement
- Run ThreadSanitizer

### Phase 3: FSM Design (Days 6-8)
- Migrate SmartExecutionEngine to OrderManager
- Add RiskState FSM to EnhancedRiskEngine
- Wire circuit breaker through risk FSM
- Add property tests and fuzz tests

### Phase 4: Backpressure (Days 9-10)
- Add OverloadPolicy to PipelineConnector
- Implement load shedding in strategy_tick
- Add queue health to UISnapshot
- Test under synthetic burst load

### Phase 5: Determinism (Days 11-13)
- Create IClock interface
- Inject clock into Application and all time-reading code
- Add DecisionLog recording
- Implement PipelineReplay
- Test bit-exact replay

---

## Risks and Trade-offs

| Risk | Mitigation |
|------|------------|
| Strong types increase verbosity | Provide .raw() escape hatch; IDE autocomplete mitigates |
| IClock virtual dispatch adds 2-5 ns per call | LTO devirtualizes in release; function pointer alternative |
| Stage decomposition increases function call overhead | BYBIT_HOT + LTO inlines them back; net positive from cold path removal |
| Load shedding may skip useful analytics | Shed counter in metrics; configurable thresholds |
| Legacy bids()/asks() removal breaks bridge code | Provide fill_bids() adapter; update bridge callers |
| Risk FSM adds state to manage | FSM is simpler than ad-hoc procedural checks; easier to test |

---

## Summary

This plan transforms the engine from a monolithic strategy_tick() with ad-hoc safety
into a pipeline of typed, bounded, testable stages with explicit contracts.

The key insight: **most of the infrastructure already exists** (FSM, arena, SPSC, SeqLock).
The refactoring is about wiring it consistently and adding compile-time enforcement.

No new frameworks. No over-engineering. Just disciplined application of patterns
already present in the codebase.
