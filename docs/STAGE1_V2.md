# Stage 1 Refactoring Specification v2

## 1. Executive Summary

This document replaces the previous Stage 1 package. The previous version identified the correct
priorities but was underspecified where it mattered most. This version is stricter:

- **Type system**: distinct structs per semantic domain, not template instantiations. TimestampNs + TimestampNs is a compile error. Price * Qty produces Notional, not a forbidden operation.
- **Hot path**: enforceable contract with compile-time, runtime, CI, and review enforcement. Not documentation.
- **Memory model**: formal ownership table, single-writer classification, static_assert policy. Not advice.
- **Backpressure**: correctness-preserving market data handling with sequence gap detection and OB invalidation. Not drop-oldest.
- **Determinism**: decomposed into 5 categories with distinct guarantees and costs.
- **FSM**: production-ready with fault classification, degraded-mode behavior, and operator overrides.

Scope: 6 priorities, 5 implementation headers, 1 test suite, 13 engineering days.

---

## 2. Critique of the Previous Stage 1 Package

### What was good

- Correct identification of 6 priorities in the right dependency order.
- Cache-line awareness throughout (alignas(128), BYBIT_PREFETCH).
- Working code skeletons that compile, pass 66 tests, and cause zero regressions.
- Recognition that most infrastructure already exists (FSM, arena, SPSC, SeqLock).

### What was weak

**W1. Shared tag for TimestampNs and DurationNs.**
Both used `StrongNumeric<NanosTag, uint64_t>`. This means `TimestampNs + TimestampNs` compiles.
Adding two absolute timestamps is semantically meaningless and a correctness bug in the type system itself.
The type system was supposed to prevent this class of bug.

**W2. No representation discussion for Price.**
The order book already uses int64 fixed-point (FixedPrice). The signal layer uses double.
The previous package wrapped double in `StrongNumeric<PriceTag>` without discussing:
when double is appropriate, when fixed-point is required, what the domain boundary is,
or what the rounding policy is at conversion points.

**W3. Hot path contract was documentation only.**
Defining `BYBIT_HOT` as `__attribute__((hot))` and writing rules in comments does not prevent
anyone from calling spdlog inside a hot function. Without enforcement, the contract decays on
first deadline pressure.

**W4. Memory model was advice, not specification.**
"Pad Metrics atomics" is correct but incomplete. No ownership table. No single-writer classification.
No policy for which memory orderings are allowed where. No static_assert checks on struct layout.

**W5. Backpressure destroyed market data integrity.**
"DropOldest for WS Message Ingestion" silently drops order book deltas. If delta N+1 is dropped,
the OB is silently inconsistent from that point forward. There is no sequence gap detection,
no invalidation, no resync. This is a correctness bug in the architecture.

**W6. Determinism was monolithic.**
"Inject clock, record decisions, replay" conflates 5 distinct guarantees:
event ordering, decision reproducibility, numeric identity, replay fidelity, and audit explainability.
Each has different costs and different enforcement mechanisms.

**W7. Virtual dispatch in hot path clock.**
IClock vtable lookup costs 2-5ns. For a 50ns OB validate budget, that is 4-10% of budget per call.
The previous version acknowledged this but offered no alternative.

**W8. Feature flag disables type safety entirely.**
`BYBIT_STRONG_TYPES 0` reverts everything to raw double/uint64_t. The "off" path is untested
and will rot. Feature flags should gate migration scope, not disable the type system.

**W9. Global clock singleton.**
`g_clock` is a mutable global pointer. Hidden dependency. Breaks testability. Breaks reasoning about
which clock a function uses. Every function that calls `global_clock()` has an implicit dependency
on whoever last called `set_global_clock()`.

**W10. DeferredWork was 80 bytes.**
Wastes half a cache line on Apple Silicon (128B lines). Should be 64 bytes to pack 2 per cache line,
or 128 bytes to own a full line. 80 bytes is the worst of both worlds.

### What should be redesigned before implementation

1. Type system: complete redesign with distinct structs, not template instantiations.
2. Hot path enforcement: real mechanisms, not annotations.
3. Memory ownership: formal table with static verification.
4. Backpressure: sequence-aware, correctness-preserving.
5. Clock: static dispatch via template parameter, not virtual.
6. Remove global clock singleton. Pass clock explicitly.

---

## 3. Improved Stage 1 Architecture

### Dependency order (unchanged)

    P3 (Types) -> P1 (Hot Path) -> P2 (Memory) -> P4 (FSM) -> P5 (Backpressure) -> P6 (Determinism)

### Key design decisions

| Decision | Previous | V2 | Rationale |
|----------|----------|----|-----------|
| Type system | Template StrongNumeric | Distinct structs per type | Prevents TimestampNs+TimestampNs, enables Price*Qty->Notional |
| Clock dispatch | Virtual (IClock*) | Template parameter + type-erased wrapper | Zero cost in production, virtual only when needed |
| Hot path enforcement | Documentation | nm symbol scan + clang-tidy + runtime assert | Multiple defense layers |
| Backpressure | Drop oldest | Sequence-tracked with OB invalidation | Correctness over convenience |
| Determinism | Monolithic | 5 decomposed categories | Different costs, different guarantees |
| Feature flag | On/off for all types | Per-type migration with compat aliases | No untested path |
| Global clock | g_clock pointer | Explicit parameter | Testable, no hidden state |

---

## 4. Priority 1: Hot-Path Contract and Enforcement

### 4.1 Forbidden operations in hot stages

| Category | Examples | Why forbidden |
|----------|----------|---------------|
| Heap allocation | new, delete, malloc, free, std::string ctor, std::vector resize | Unbounded latency, kernel involvement |
| Logging | spdlog::*, printf, std::cout, fstream write | I/O syscalls, formatting, buffering |
| Exception throw/catch | throw, try/catch | Stack unwinding |
| Virtual dispatch | vtable call on non-devirtualizable path | Branch mispredict + indirection |
| System calls | clock_gettime, gettimeofday, mmap, mprotect | Kernel transition |
| Mutex/condvar | std::mutex, pthread_mutex_lock | Blocking, priority inversion |
| JSON parsing | nlohmann::json::parse, simdjson::parse | Allocation, unbounded work |
| std::function | Assignment, copy, invocation | May allocate |
| unordered_map/set | Insert, rehash | Allocation, unbounded |

### 4.2 Allowed operations in hot stages

| Category | Examples |
|----------|----------|
| Fixed-size array access | std::array, C array, pointer arithmetic |
| Arithmetic | int, double, NEON intrinsics |
| Atomic load/store | relaxed, acquire, release (NOT seq_cst in tight loops) |
| SPSC queue push/pop | Lock-free, bounded |
| TSC read | mach_absolute_time, cntvct_el0 |
| Branch-free LUT | Indexed array lookup |
| memcpy/memset | Fixed-size, compiler-optimized |
| Arena allocation | Bump pointer, pre-mapped |

### 4.3 Compile-time enforcement

**Banned-API headers.** In hot-path translation units, include a poison header:

    // src/core/hot_path_poison.h
    // Include AFTER all real headers in hot-path .cpp files.
    // Redefines forbidden APIs as deleted functions to cause compile errors.
    #pragma once
    namespace bybit::hot_path_banned {
        [[deprecated("FORBIDDEN in hot path")]] void* operator new(size_t) = delete;
        // Cannot actually delete global operator new, but we can:
        // 1. Use -fno-exceptions in hot-path TUs
        // 2. Use clang-tidy to flag allocation
    }

**Practical compile-time enforcement: use a separate translation unit for hot path code
compiled with `-fno-exceptions -fno-rtti`. This prevents throw/catch and dynamic_cast.**

### 4.4 Runtime enforcement (debug builds)

**Arena watermark check.** After each tick, assert that the arena cursor did not advance
(meaning no arena allocation happened during the tick):

    uint64_t cursor_before = arena_.save();
    run_hot_stages();
    assert(arena_.save() == cursor_before && "hot path allocated from arena");

**Latency budget assertion.** ScopedStageTimer checks elapsed time against budget.
In debug builds, log violations. In CI, fail if p99 exceeds 3x budget.

### 4.5 CI enforcement

**Symbol scan.** After building hot-path object files, scan with nm for forbidden symbols:

    # CI script: check_hot_path_symbols.sh
    FORBIDDEN="__cxa_allocate_exception|__cxa_throw|_Znwm|_Znam|_ZdlPv|_ZdaPv|spdlog"
    nm -C build/CMakeFiles/hot_stages.dir/*.o | grep -E "$FORBIDDEN" && exit 1

`_Znwm` = operator new(size_t), `_Znam` = operator new[](size_t).
If any forbidden symbol appears in hot-path object files, CI fails.

**Benchmark gate.** CI runs bench_strategy_tick. If p99 > 100us, merge is blocked.

### 4.6 Review-time checklist

Every PR touching hot-path files must answer:
1. Does this function allocate? If yes, move to cold path.
2. Does this function log? If yes, defer via DeferredWorkQueue.
3. Does this function throw? If yes, return HotResult instead.
4. Does this function call a virtual method? If yes, justify or templatize.
5. What is the worst-case latency of this function?

### 4.7 Stage decomposition and budgets

| Stage | Budget | Hot/Cold | Owner thread |
|-------|--------|----------|--------------|
| 0: OB Validate | 50 ns | HOT | strategy |
| 1: Feature Compute | 5 us | HOT | strategy |
| 2: Regime Detect | 500 ns | HOT | strategy |
| 3: ML Inference | 50 us | HOT | strategy |
| 4: Signal Generate | 500 ns | HOT | strategy |
| 5: Risk Check | 500 ns | HOT | strategy |
| 6: Execution Decide | 2 us | HOT | strategy |
| 7: Order Submit | 5 ms | COLD | strategy (async REST) |
| 8: Mark to Market | 100 ns | HOT | strategy |
| 9: Log/Record | unbounded | COLD | deferred |
| 10: Analytics/RL | unbounded | COLD | deferred |
| 11: UI Publish | 200 ns | HOT | strategy (SeqLock) |

**Total hot budget: stages 0-6 + 8 + 11 = 58.85 us typical, 100 us hard cap.**

### 4.8 Deferred work queue

64-byte records, fits one x86 cache line or half an Apple Silicon line:

    struct alignas(64) DeferredWork {
        uint64_t timestamp_ns;      // 8
        double   values[4];         // 32
        char     tag[14];           // 14
        uint8_t  type;              // 1
        uint8_t  severity;          // 1
        // padding: 8 bytes to reach 64
    };
    static_assert(sizeof(DeferredWork) == 64);

Queue: SPSCQueue<DeferredWork, 4096>. Drained after hot path completes.
If queue is full during hot path, drop the work item and increment a counter. Never block.

---

## 5. Priority 2: Memory Model and Cache Discipline

### 5.1 Ownership table

| Object | Owner (writer) | Readers | Sharing mechanism |
|--------|---------------|---------|-------------------|
| OrderBook | strategy thread | UI thread | SeqLock snapshot |
| Features (current) | strategy thread | none | single-writer |
| ModelOutput | strategy thread | none | single-writer |
| Position | strategy thread | UI thread | SeqLock snapshot |
| Metrics counters | various | UI/logging | per-counter atomic, padded |
| WS message buffer | WS thread | strategy thread | SPSC queue |
| Trade flow buffer | WS thread | strategy thread | SPSC queue |
| Active orders | strategy thread | UI thread | SeqLock snapshot |
| Risk state | strategy thread | UI thread | SeqLock snapshot |
| DeferredWork queue | strategy thread (push) | drain thread (pop) | SPSC queue |
| UISnapshot | strategy thread (write) | UI thread (read) | TripleBuffer |
| BlackBox recorder | strategy thread (write) | dump thread (read) | ring buffer + atomics |

**Rule: every hot-path object has exactly ONE writer. No exceptions.**

### 5.2 Cache-line ownership policy

1. Every cross-thread atomic variable gets its own 128-byte aligned slot.
2. Hot-path structs that are single-writer, single-reader get one cache-line alignment.
3. No two independently-written fields share a cache line.
4. Padding between writer groups is explicit (not implicit through member order).

### 5.3 Atomic usage policy

| Ordering | When to use | When NOT to use |
|----------|------------|-----------------|
| relaxed | Counters, statistics, sequence numbers (single-writer, tolerant readers) | Cross-thread synchronization |
| acquire | Consumer side of SPSC queue, SeqLock reader | Hot-path single-thread code |
| release | Producer side of SPSC queue, SeqLock writer | Read-only accessors |
| acq_rel | CAS in concurrent pool | Anything single-threaded |
| seq_cst | NEVER in hot path | Everywhere in hot path |

**Rule: seq_cst is forbidden in hot-path code. Every atomic operation must have an explicit ordering annotation. No default (which is seq_cst).**

### 5.4 Static layout verification

Every hot-path struct must have:

    static_assert(sizeof(MyStruct) == EXPECTED_SIZE);
    static_assert(sizeof(MyStruct) % BYBIT_CACHELINE == 0);  // or explicit exception
    static_assert(alignof(MyStruct) >= BYBIT_CACHELINE);      // for cross-thread structs
    static_assert(std::is_trivially_copyable_v<MyStruct>);     // for SeqLock/memcpy

### 5.5 Metrics struct: before and after

**Before** (broken): all atomics in one struct, no padding.

    struct Metrics {
        std::atomic<uint64_t> ob_updates{0};      // WS thread writes
        std::atomic<uint64_t> trades{0};           // WS thread writes
        std::atomic<uint64_t> signals{0};          // strategy thread writes  <-- FALSE SHARING
        std::atomic<uint64_t> orders_sent{0};      // strategy thread writes
    };

**After** (correct): each writer-group on its own cache line.

    struct Metrics {
        // Group 1: WS thread writes
        struct alignas(128) {
            std::atomic<uint64_t> ob_updates{0};
            std::atomic<uint64_t> trades{0};
            std::atomic<uint64_t> ws_reconnects{0};
        } ws;

        // Group 2: Strategy thread writes
        struct alignas(128) {
            std::atomic<uint64_t> signals{0};
            std::atomic<uint64_t> orders_sent{0};
            std::atomic<uint64_t> orders_filled{0};
            std::atomic<uint64_t> orders_cancelled{0};
        } strategy;

        // Group 3: Read-mostly (computed periodically)
        struct alignas(128) {
            std::atomic<double> e2e_p50_us{0.0};
            std::atomic<double> e2e_p99_us{0.0};
        } latency;
    };

---

## 6. Priority 3: Strong Types and Numeric Semantics

### 6.1 Design principle

Each semantic type is a distinct struct with explicitly defined operators.
NOT a template instantiation. This prevents the NanosTag sharing bug and enables
cross-type operations (Price * Qty -> Notional) that a single-tag template cannot express.

### 6.2 Representation decisions

| Type | Representation | Rationale |
|------|---------------|-----------|
| Price | double | Signal/execution layer. Tick sizes vary across instruments. Fixed-point conversion at OB boundary. |
| Qty | double | Fractional quantities in crypto (0.00001 BTC). |
| Notional | double | Price * Qty product. Always floating. |
| BasisPoints | double | 1 bps = 0.01%. Range typically [-10000, +10000]. |
| TickSize | double | Instrument-specific. Used in OB fixed-point conversion. |
| TimestampNs | uint64_t | Monotonic nanoseconds. Unsigned: timestamps are always positive. |
| DurationNs | int64_t | SIGNED. Durations can be negative (deadline exceeded by X ns). |
| TscTicks | uint64_t | Raw TSC counter. Opaque, not convertible to ns without calibration. |
| SequenceNumber | uint64_t | Monotonic sequence from exchange. Gap = data loss. |
| OrderId | char[48] | Fixed-size, stack-allocated. No heap. |
| InstrumentId | char[16] | Fixed-size, stack-allocated. No heap. |

**Critical: DurationNs is int64_t (signed), not uint64_t.**
Unsigned durations cannot represent "this event arrived 5us LATE" which is (actual - deadline) < 0.

**Critical: TimestampNs and DurationNs are DISTINCT types, not aliases.**
TimestampNs + TimestampNs = compile error.
TimestampNs - TimestampNs = DurationNs.
TimestampNs + DurationNs = TimestampNs.

### 6.3 Operator table

Legend: + = defined, X = compile error (not defined)

    Operation                    Result type      Defined?
    -------------------------------------------------------
    Price + Price                Price            +  (averaging)
    Price - Price                Price            +  (spread)
    Price * double               Price            +  (scaling)
    Price / double               Price            +  (scaling)
    Price / Price                double           +  (ratio)
    Price * Qty                  X                X  (use notional() free function)
    Price + Qty                  X                X
    Price + BasisPoints          X                X  (use price_plus_bps())

    Qty + Qty                    Qty              +
    Qty - Qty                    Qty              +
    Qty * double                 Qty              +  (scaling)
    Qty / double                 Qty              +
    Qty / Qty                    double           +  (ratio)

    Notional + Notional          Notional         +
    Notional - Notional          Notional         +
    Notional * double            Notional         +  (scaling)

    BasisPoints + BasisPoints    BasisPoints      +
    BasisPoints - BasisPoints    BasisPoints      +
    BasisPoints * double         BasisPoints      +  (scaling)

    TimestampNs + DurationNs     TimestampNs      +
    TimestampNs - DurationNs     TimestampNs      +
    TimestampNs - TimestampNs    DurationNs       +
    TimestampNs + TimestampNs    X                X  (meaningless)
    TimestampNs * anything       X                X

    DurationNs + DurationNs      DurationNs       +
    DurationNs - DurationNs      DurationNs       +
    DurationNs * int64_t         DurationNs       +  (scaling)
    DurationNs / int64_t         DurationNs       +
    DurationNs / DurationNs      int64_t          +  (ratio)

    SequenceNumber + uint64_t    SequenceNumber   +  (increment)
    SequenceNumber - SequenceNumber  uint64_t     +  (gap detection)
    SequenceNumber + SequenceNumber  X            X

    TscTicks - TscTicks          TscTicks         +  (interval)
    TscTicks + TscTicks          X                X

### 6.4 Cross-type free functions

    Notional    notional(Price p, Qty q);           // p.v * q.v
    BasisPoints price_diff_bps(Price a, Price b);   // (a-b)/b * 10000
    Price       price_plus_bps(Price p, BasisPoints bps); // p * (1 + bps/10000)
    BasisPoints spread_bps(Price bid, Price ask);   // (ask-bid)/mid * 10000
    Price       mid_price(Price bid, Price ask);     // (bid+ask)/2
    Price       microprice(Price bid, Price ask, Qty bq, Qty aq);
    BasisPoints fraction_to_bps(double fraction);   // f * 10000
    double      bps_to_fraction(BasisPoints bps);   // bps / 10000
    DurationNs  ns_from_us(double us);              // us * 1000
    DurationNs  ns_from_ms(double ms);              // ms * 1000000
    double      to_us(DurationNs d);                // d / 1000.0
    double      to_ms(DurationNs d);                // d / 1000000.0

### 6.5 OB domain boundary

The order book uses int64 fixed-point prices (FixedPrice). The signal layer uses Price (double).
The conversion boundary is explicit and narrow:

    // OB -> Signal (read path, every tick)
    Price from_fixed(int64_t fixed_price, TickSize tick) noexcept;

    // Signal -> OB (write path, order submission only)
    int64_t to_fixed(Price price, TickSize tick) noexcept;

    // Rounding policy: to_fixed rounds to nearest tick. ALWAYS.
    // No implicit truncation. No floor/ceil unless explicitly requested.
    int64_t to_fixed_floor(Price price, TickSize tick) noexcept;
    int64_t to_fixed_ceil(Price price, TickSize tick) noexcept;

### 6.6 Bugs prevented

| Bug class | Example | How types prevent it |
|-----------|---------|---------------------|
| Unit swap | pass qty where price expected | Price and Qty are distinct types |
| Bps/fraction confusion | threshold 0.001 meaning 1 bps or 0.1%? | BasisPoints vs double |
| Timestamp addition | ts1 + ts2 (meaningless) | TimestampNs + TimestampNs = compile error |
| Sequence arithmetic | seq * 2 (meaningless) | SequenceNumber has no multiply |
| Duration sign loss | deadline - actual = negative | DurationNs is int64_t, not uint64_t |
| Order ID mixup | pass instrument where order_id expected | OrderId vs InstrumentId are distinct |
| TSC as nanoseconds | using raw ticks as nanoseconds | TscTicks not convertible to TimestampNs |

---

## 7. Priority 4: FSM-First Execution and Risk Design

### 7.1 Order lifecycle FSM (exists, refine)

The existing 8-state FSM in order_state_machine.h is sound. Refinements:

1. Add transition REASON to TransitionResult (for audit):

       struct TransitionResult {
           OrdState old_state;
           OrdState new_state;
           OrdEvent trigger;        // NEW: what caused this
           bool changed;
           bool is_terminal;
       };

2. Add monotonic transition counter per order (for sequencing):

       uint32_t transition_count = 0;  // in ManagedOrder

### 7.2 Execution engine FSM (new)

    enum class ExecState : uint8_t {
        Idle        = 0,  // No active strategy
        Active      = 1,  // Normal operation
        Draining    = 2,  // Cancel-all in progress, no new orders
        Halted      = 3,  // Risk halt, no orders, awaiting manual
        Degraded    = 4,  // Partial failure (e.g., REST timeout), reduce-only
        Emergency   = 5,  // Emergency cancel-all, fastest path
    };

    enum class ExecEvent : uint8_t {
        Start         = 0,
        SignalArrived = 1,
        OrderFilled   = 2,
        CancelAllDone = 3,
        RiskHalt      = 4,
        RiskResume    = 5,
        VenueFault    = 6,
        VenueRecover  = 7,
        EmergencyStop = 8,
        ManualResume  = 9,
    };

Allowed actions per state:
- Idle: nothing
- Active: submit, amend, cancel
- Draining: cancel only (in-progress cancels complete)
- Halted: nothing (wait for ManualResume)
- Degraded: cancel and reduce-only orders
- Emergency: emergency_cancel_all only

### 7.3 Risk engine FSM (new)

    enum class RiskState : uint8_t {
        Normal      = 0,  // All limits nominal
        Elevated    = 1,  // Any limit > 70% utilized
        Restricted  = 2,  // Any limit > 90%, reduce-only
        Halted      = 3,  // Circuit breaker tripped
        Cooldown    = 4,  // Post-halt timer
        Emergency   = 5,  // Operator kill switch
    };

Fault classification:
- VenueFault: exchange connectivity loss, order rejection spike
- DataFault: market data gap, stale OB (no update > threshold)
- StrategyFault: excessive loss rate, consecutive losses
- AccountFault: margin call, position limit breach
- OperatorFault: manual halt command

Each fault type maps to a RiskEvent. RiskState determines which ExecState transitions fire:
- RiskState::Halted -> ExecEvent::RiskHalt
- RiskState::Normal -> ExecEvent::RiskResume (if was halted)
- RiskState::Emergency -> ExecEvent::EmergencyStop

### 7.4 Circuit breaker integration

Circuit breaker is NOT a standalone class. It is a set of conditions that feed RiskEvents.
When drawdown > threshold: emit RiskEvent::ThresholdBreach -> transition to Halted.
When cooldown expires: emit RiskEvent::CooldownExpired -> transition to Cooldown then Normal.
When operator resets: emit RiskEvent::ManualReset -> transition to Normal.

---

## 8. Priority 5: Backpressure and Overload Handling

### 8.1 Market data correctness rules

**Rule 1: Order book deltas MUST NOT be dropped silently.**
Dropping delta N means the OB state diverges from exchange state.
All subsequent trading decisions are based on a stale/incorrect book.

**Rule 2: If any delta is missed, the OB MUST be invalidated.**
An invalidated OB cannot be used for signal generation or order pricing.
The engine must either:
(a) request a fresh snapshot from REST, or
(b) wait for the next periodic snapshot on the WS stream.

**Rule 3: Sequence numbers MUST be tracked per stream.**
The SequenceNumber type enables gap detection:

    SequenceNumber expected_seq_;

    void on_delta(SequenceNumber seq, ...) {
        uint64_t gap = seq - expected_seq_;  // SequenceNumber - SequenceNumber -> uint64_t
        if (gap != 0) {
            invalidate_ob();
            request_snapshot();
            return;
        }
        expected_seq_ = seq + 1;
        apply_delta(...);
    }

**Rule 4: Coalescing is legal ONLY for idempotent data.**
- OB snapshot: latest wins (idempotent). May coalesce.
- OB delta: NOT idempotent. Must preserve sequence. NEVER coalesce.
- Trade: NOT idempotent. Must preserve all. NEVER coalesce.
- Ticker/BBO: latest wins. May coalesce.

### 8.2 Per-stage overload policy

| Stage | Policy | Rationale |
|-------|--------|-----------|
| WS network read | Bounded buffer, oldest dropped with seq tracking | Prevent memory exhaustion; gap detected by seq check |
| WS JSON parse | Process all, no dropping | Parsing is CPU-only, fast |
| OB delta apply | MUST apply all in sequence; on gap: invalidate + resync | Correctness |
| OB snapshot | Latest-wins coalesce | Idempotent |
| Trade ingestion | SPSC queue, drop newest on full + increment counter | Trades inform but dont drive pricing |
| Feature compute | Always runs | Core hot path |
| ML inference | Always runs | Core hot path |
| Signal/Risk/Exec | Always runs | Core hot path |
| Analytics/RL | Load-shed: skip if tick > 80% of budget | Non-critical |
| Telemetry/logging | Deferred queue, drop on full | Never block hot path |
| BlackBox recorder | Ring buffer overwrites oldest | Bounded, fire-and-forget |
| UI snapshot | TripleBuffer latest-wins | Idempotent |

### 8.3 OB invalidation protocol

States:

    enum class OBHealth : uint8_t {
        Valid,           // Consistent with exchange
        StaleWarning,    // No update > stale_threshold_ms
        Invalid,         // Sequence gap detected
        Resyncing,       // Snapshot requested, waiting
    };

When OBHealth != Valid:
- Signal generation is SKIPPED (no trading on bad data).
- Existing orders are NOT cancelled (they have their own TTL).
- UI shows OB health status.
- Metrics counter incremented.

---

## 9. Priority 6: Dual-Mode Determinism

### 9.1 Five categories of determinism

**Category 1: Event Determinism**
Definition: given the same external event stream, the engine processes events in the same order.
Controlled by: single-threaded strategy loop (already satisfied), SPSC queue ordering.
Broken by: multi-threaded event processing, unordered containers, thread scheduling.
Production guarantee: YES (single-thread strategy).
Audit guarantee: YES.

**Category 2: Decision Determinism**
Definition: given the same event stream AND same state, the engine makes the same trading decisions.
Controlled by: no random generators in hot path, no wall-clock branching, no hash-map iteration.
Broken by: random number generators, unordered_map iteration order, time-dependent branching.
Production guarantee: YES if all time reads go through injected clock.
Audit guarantee: YES.

**Category 3: Numeric Determinism**
Definition: floating-point computations produce bit-identical results across runs.
Controlled by: -ffp-contract=off, no -ffast-math in hot path, deterministic reduction order.
Broken by: -ffast-math (reorders operations), FMA contraction differences, auto-vectorization
changing reduction order, vDSP non-deterministic implementations.
Production guarantee: NO. We use -ffast-math for performance. Accept epsilon variance.
Audit guarantee: OPTIONAL. Can compile without -ffast-math for exact replay at performance cost.
Validation: record model outputs, compare with epsilon tolerance (1e-6 relative).

**Category 4: Replay Fidelity**
Definition: a recorded event stream can be replayed through the pipeline producing matching decisions.
Controlled by: injected clock, recorded external events, no side-channel inputs.
Broken by: wall-clock leaks, non-recorded external state, system call results.
Production guarantee: N/A (production does not replay).
Audit guarantee: YES (this is the point of replay).
What must be recorded: all WS messages (raw bytes), all REST responses, clock values.
What is too expensive for production: recording full message bytes (record hash + parsed values instead).

**Category 5: Audit Explainability**
Definition: for any trading decision, the full input context can be reconstructed.
Controlled by: decision log recording all signal inputs, regime, threshold, model output, risk check result.
Broken by: missing input fields, logging only the decision without the inputs.
Production guarantee: YES (decision log is lightweight).
Audit guarantee: YES.

### 9.2 Clock design for determinism

**Static dispatch (production) vs virtual dispatch (replay/test):**

The clock is a template parameter on the pipeline, not a virtual interface.
In production, `TscClockSource` is inlined. Zero overhead.
In replay, `ReplayClockSource` is injected. One translation-unit boundary, no vtable.

    template <typename ClockSource = TscClockSource>
    class StrategyPipeline {
        ClockSource clock_;
    public:
        TimestampNs now() const noexcept { return clock_.now(); }
    };

For code that cannot easily be templatized (e.g., legacy callers), provide a type-erased
wrapper that stores a function pointer (8 bytes, one indirection, no vtable):

    struct ClockFn {
        using NowFn = uint64_t(*)() noexcept;
        NowFn now_ns;
    };

This costs one indirect call (~2ns) instead of vtable lookup (~3-5ns).

### 9.3 Nondeterminism sources and mitigations

| Source | Category affected | Production mitigation | Replay mitigation |
|--------|------------------|----------------------|-------------------|
| Wall clock | Decision | Use injected clock | Replay clock |
| TSC variability | Replay fidelity | Record TSC values | Use recorded values |
| std::rand / random | Decision | Seed from config, record seed | Replay seed |
| unordered_map iteration | Decision | Forbidden in hot path | Same |
| -ffast-math | Numeric | Accept epsilon | Compile without for exact |
| Thread scheduling | Event | Single-thread strategy | Same |
| External ACK order | Event | Record arrival order | Replay in recorded order |
| Boost.Asio callback order | Event | post() ordering is FIFO per strand | Same |
| Floating-point NaN propagation | Numeric | Sanitize inputs | Same |

---

## 10. Ideal Module/File Structure

    src/core/
        strong_types.h         # Price, Qty, Notional, TimestampNs, DurationNs, etc.
        hot_path.h             # BYBIT_HOT, HotResult, PipelineStage, budgets, DeferredWork
        hot_path_poison.h      # Deleted-function traps for forbidden APIs
        clock_source.h         # TscClockSource, ReplayClockSource, ClockFn
        memory_policy.h        # Ownership macros, padding helpers, static_assert templates
        sequence_tracker.h     # SequenceNumber gap detection, OBHealth

    (all other directories unchanged in Stage 1)

---

## 11. Ideal Specification: strong_types.h

See Section 6 for the full design. Implementation requirements:

- Each type is a distinct struct with a single member `v`.
- No inheritance, no virtual, trivially copyable, trivially destructible.
- sizeof(Price) == sizeof(double), sizeof(TimestampNs) == sizeof(uint64_t).
- All operators defined as friend functions inside the struct (ADL-discoverable).
- Cross-type operations defined as free functions in namespace bybit.
- Every type has explicit `.raw()` accessor for escape-hatch interop.
- Every floating-point type has `.is_zero(eps)`, `.is_finite()`, `.abs()`.
- Every comparison type has full relational operators.
- static_assert on sizeof and alignof for every type.
- No feature flag. Types are always strong. Migration uses compat aliases at call sites.

---

## 12. Ideal Specification: hot_path.h

See Section 4 for the full design. Implementation requirements:

- BYBIT_HOT, BYBIT_COLD, BYBIT_FORCE_INLINE macros.
- HotResult with static success()/fail(const char*).
- PipelineStage enum (12 stages).
- STAGE_BUDGET_NS array (constexpr).
- ScopedStageTimer (RAII, writes to uint64_t&).
- DeferredWork struct (exactly 64 bytes, static_assert).
- DeferredWorkQueue typedef (SPSCQueue<DeferredWork, 4096>).
- LoadShedState struct with shed_count, total_ticks, shed_rate.
- TickLatency struct with per-stage array and hot_path_total() method.

---

## 13. Ideal Specification: clock_source.h

NOTE: renamed from clock_interface.h. No "interface" because primary mode is static dispatch.

- TscClockSource: struct with static now() returning TimestampNs. Inlineable.
- ReplayClockSource: struct with advance_to(TimestampNs). now() returns stored value. Atomic for thread safety.
- MockClockSource: struct with set(TimestampNs), advance(DurationNs). Non-atomic (single-thread tests).
- ClockFn: type-erased function pointer wrapper. 8 bytes. One indirection.
- No global singleton. No g_clock. Pass clock explicitly or via template parameter.
- No IClock virtual base class in the primary design. Virtual wrapper available as opt-in for legacy callers.

---

## 14. Suggested Additional Headers

### 14.1 hot_path_poison.h

Compile-time enforcement header included in hot-path TUs. Contains deprecated/deleted
declarations for forbidden APIs. Lightweight, zero runtime cost.

### 14.2 memory_policy.h

- BYBIT_CACHELINE constant (128 for Apple Silicon).
- BYBIT_PAD_TO(alignment) macro for explicit padding.
- BYBIT_SINGLE_WRITER annotation (documentation + static analysis hint).
- BYBIT_VERIFY_LAYOUT(Type, expected_size, expected_align) macro.
- CacheLinePadded<T> wrapper that pads T to a full cache line.

### 14.3 sequence_tracker.h

- SequenceTracker class: tracks expected sequence, detects gaps, reports health.
- OBHealth enum.
- Resync protocol interface.

---

## 15. Tests

### Unit tests (test_strong_types.cpp)

- sizeof/alignof verification for every type.
- Same-type arithmetic correctness.
- Cross-type compile errors (static_assert with concepts or SFINAE).
- Cross-type free function correctness (notional, price_diff_bps, etc.).
- TimestampNs + TimestampNs must NOT compile (negative test via requires-clause check).
- DurationNs sign behavior (negative durations).
- OrderId/InstrumentId construction, comparison, truncation, null safety.
- Rounding: to_fixed round-trip fidelity.

### FSM legality tests (test_risk_fsm.cpp)

- Exhaustive: every (state, event) pair produces a valid state.
- No terminal state is reachable without going through appropriate path.
- Property test: random event sequences never produce illegal transitions.

### Overload policy tests (test_backpressure.cpp)

- Sequence gap detection: inject gap, verify OB invalidated.
- Resync protocol: inject snapshot after gap, verify OB valid.
- Queue full behavior: fill SPSC queue, verify drop counter incremented, no block.

### Replay tests (test_replay_determinism.cpp)

- Record 100 ticks, replay, verify decision log match.
- Inject clock skew in replay, verify divergence detected.

### Latency regression tests (bench_strategy_tick.cpp)

- Full pipeline tick benchmark. CI gate at p99 < 100us.
- Per-stage breakdown.

---

## 16. Benchmarks

| Benchmark | What it measures | Acceptable |
|-----------|-----------------|------------|
| strong_type_arithmetic | Price+Price, Qty*scalar vs raw double | < 0.5ns overhead |
| scoped_stage_timer | TSC read + store overhead | < 5ns |
| clock_fn_dispatch | Function-pointer clock call | < 3ns |
| deferred_work_push | SPSC push of 64-byte record | < 10ns |
| sequence_gap_check | SequenceNumber comparison | < 1ns |
| hot_path_e2e | Full strategy_tick pipeline | p99 < 100us |

---

## 17. CI Invariants

1. All existing tests pass (zero regressions).
2. test_strong_types passes (type system correctness).
3. nm scan of hot-path objects: no forbidden symbols.
4. bench_strategy_tick p99 < 100us.
5. ThreadSanitizer: zero data races.
6. static_assert compilation: all layout checks pass.
7. Clang-tidy: no warnings in hot-path files.

---

## 18. Migration Roadmap

### Phase 0: Types (Days 1-2)

1. Create src/core/strong_types.h with all types.
2. Create src/core/memory_policy.h with layout helpers.
3. Create test_strong_types.cpp.
4. Build and test. No existing code changes yet.
5. Verify zero overhead via benchmark.

### Phase 1: Hot Path (Days 3-4)

1. Create src/core/hot_path.h with contract infrastructure.
2. Create src/core/clock_source.h with clock types.
3. Extract strategy_tick stages into named functions.
4. Move spdlog calls to DeferredWorkQueue.
5. Add ScopedStageTimer to each stage.
6. Benchmark comparison.

### Phase 2: Memory (Day 5)

1. Apply memory_policy.h macros to Metrics, OB, Orders.
2. Remove OrderBook mutable legacy buffers.
3. Add static_assert checks.
4. Run ThreadSanitizer.

### Phase 3: FSM (Days 6-8)

1. Migrate SmartExecutionEngine to OrderManager.
2. Add ExecState FSM.
3. Add RiskState FSM.
4. Wire circuit breaker through risk FSM.
5. Add FSM tests.

### Phase 4: Backpressure (Days 9-10)

1. Add SequenceTracker to WS message handler.
2. Add OBHealth state machine.
3. Add load-shed logic to strategy_tick.
4. Test under synthetic burst.

### Phase 5: Determinism (Days 11-13)

1. Templatize pipeline on ClockSource.
2. Add DecisionLog recording.
3. Implement replay-through-pipeline.
4. Test bit-exact decision replay.

### Rollback strategy

Each phase is a separate PR. If a phase causes regressions:
1. Revert the PR.
2. Fix the issue on a branch.
3. Re-merge.

No phase depends on partial completion of another phase.
Each PR must pass all CI invariants before merge.

---

## 19. Risks and Trade-offs

| Risk | Mitigation |
|------|------------|
| Distinct structs are verbose | IDE autocomplete. Fewer bugs outweighs more typing. |
| Template clock parameter infects call sites | Limit to pipeline top-level. Use ClockFn for leaves. |
| OB invalidation on sequence gap halts trading | Better than trading on wrong prices. Resync is fast (< 1s). |
| Load shedding skips analytics | Tracked in metrics. Configurable threshold. |
| DurationNs signed causes uint64 interop friction | Explicit cast at boundary. Safer than silent underflow. |
| Legacy code uses raw double | Compat aliases at call sites during migration. No feature flag. |
| nm symbol scan is fragile | Supplement with clang-tidy. Multiple enforcement layers. |
| Numeric determinism not guaranteed with -ffast-math | Accepted tradeoff. Document epsilon tolerance. Audit mode disables -ffast-math. |
