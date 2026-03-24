#pragma once
// ---- Hot-Path Contract and Enforcement --------------------------------------
//
// Rules for functions marked BYBIT_HOT:
//   1. No heap allocation (new/delete/malloc/free/std::string/std::vector resize)
//   2. No logging (spdlog/printf/iostream)
//   3. No exception throw/catch
//   4. No virtual dispatch on non-devirtualizable path
//   5. No system calls (clock_gettime, mmap, mprotect)
//   6. No mutex/condvar
//   7. No JSON parsing
//   8. No std::function
//   9. No unordered_map/set
//   10. Must complete within declared latency budget
//
// Enforcement layers:
//   Compile-time: -fno-exceptions -fno-rtti on hot-path TUs; hot_path_poison.h
//   Runtime:      ScopedStageTimer budget check (debug); arena watermark (debug)
//   CI:           nm symbol scan for _Znwm/_Znam/spdlog/__cxa_throw in hot .o files
//   Review:       PR checklist (does this allocate/log/throw?)

#include "strong_types.h"
#include "memory_policy.h"
#include "../utils/tsc_clock.h"

#include <cstdint>
#include <cstddef>
#include <array>
#include <atomic>
#include <cstring>

namespace bybit {

// ---- Compiler Annotations ---------------------------------------------------

#if defined(__GNUC__) || defined(__clang__)
#define BYBIT_HOT   __attribute__((hot))
#define BYBIT_COLD  __attribute__((cold))
#define BYBIT_LIKELY(x)   __builtin_expect(!!(x), 1)
#define BYBIT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define BYBIT_FORCE_INLINE __attribute__((always_inline)) inline
#else
#define BYBIT_HOT
#define BYBIT_COLD
#define BYBIT_LIKELY(x)   (x)
#define BYBIT_UNLIKELY(x) (x)
#define BYBIT_FORCE_INLINE inline
#endif

// ---- HotResult --------------------------------------------------------------
// Returned by hot-path functions instead of throwing. Error is a static literal.

struct HotResult {
    bool ok;
    const char* error; // static string only, never heap

    static constexpr HotResult success() noexcept { return {true, nullptr}; }
    static constexpr HotResult fail(const char* reason) noexcept { return {false, reason}; }

    explicit constexpr operator bool() const noexcept { return ok; }
};

// ---- Pipeline Stages --------------------------------------------------------

enum class PipelineStage : uint8_t {
    OBValidate      = 0,
    FeatureCompute  = 1,
    RegimeDetect    = 2,
    MLInference     = 3,
    SignalGenerate  = 4,
    RiskCheck       = 5,
    ExecutionDecide = 6,
    OrderSubmit     = 7,  // Cold boundary
    MarkToMarket    = 8,
    LogDeferred     = 9,  // Cold
    AnalyticsRL     = 10, // Cold
    UIPublish       = 11,
    COUNT           = 12,
};

inline const char* stage_name(PipelineStage s) noexcept {
    constexpr const char* names[] = {
        "OBValidate", "FeatureCompute", "RegimeDetect", "MLInference",
        "SignalGenerate", "RiskCheck", "ExecutionDecide", "OrderSubmit",
        "MarkToMarket", "LogDeferred", "AnalyticsRL", "UIPublish"
    };
    auto idx = static_cast<size_t>(s);
    return idx < 12 ? names[idx] : "Unknown";
}

// ---- Per-Stage Latency Budgets (nanoseconds) --------------------------------
// p99 targets. Debug builds warn on 3x violation. CI fails on sustained breach.

static constexpr std::array<uint64_t, static_cast<size_t>(PipelineStage::COUNT)>
STAGE_BUDGET_NS = {{
        50,         // OBValidate:      50 ns
     5'000,         // FeatureCompute:   5 us
       500,         // RegimeDetect:   500 ns
    50'000,         // MLInference:    50 us (ONNX) / 10 us (GRU)
       500,         // SignalGenerate: 500 ns
       500,         // RiskCheck:      500 ns
     2'000,         // ExecutionDecide: 2 us
 5'000'000,         // OrderSubmit:     5 ms (REST, cold)
       100,         // MarkToMarket:  100 ns
         0,         // LogDeferred:    unbounded (cold)
         0,         // AnalyticsRL:    unbounded (cold)
       200,         // UIPublish:     200 ns (SeqLock)
}};

// Total hot-path budget: stages 0-6 + 8 + 11 = ~58.85 us typical, 100 us hard cap.
static constexpr uint64_t TOTAL_HOT_BUDGET_NS = 100'000;

// ---- ScopedStageTimer -------------------------------------------------------
// RAII timer. Writes elapsed ns to caller-provided location.
// Debug-mode 3x budget violation tracked in atomic counter.

class ScopedStageTimer {
public:
    BYBIT_FORCE_INLINE
    ScopedStageTimer(PipelineStage stage, uint64_t& out_ns) noexcept
        : stage_(stage), out_ns_(out_ns), start_(TscClock::now()) {}

    BYBIT_FORCE_INLINE
    ~ScopedStageTimer() noexcept {
        uint64_t elapsed = TscClock::elapsed_ns(start_);
        out_ns_ = elapsed;
#ifndef NDEBUG
        uint64_t budget = STAGE_BUDGET_NS[static_cast<size_t>(stage_)];
        if (budget > 0 && elapsed > budget * 3) {
            budget_violations_.fetch_add(1, std::memory_order_relaxed);
        }
#endif
    }

    ScopedStageTimer(const ScopedStageTimer&) = delete;
    ScopedStageTimer& operator=(const ScopedStageTimer&) = delete;

    static uint64_t budget_violations() noexcept {
        return budget_violations_.load(std::memory_order_relaxed);
    }
    static void reset_budget_violations() noexcept {
        budget_violations_.store(0, std::memory_order_relaxed);
    }

private:
    PipelineStage stage_;
    uint64_t& out_ns_;
    uint64_t start_;
    static inline std::atomic<uint64_t> budget_violations_{0};
};

// ---- TickLatency ------------------------------------------------------------
// Per-tick breakdown of all stage latencies. Published in UISnapshot.

struct alignas(128) TickLatency {
    std::array<uint64_t, static_cast<size_t>(PipelineStage::COUNT)> stage_ns{};
    uint64_t total_ns = 0;
    uint64_t tick_id = 0;
    bool budget_exceeded = false;
    bool load_shed = false;

    uint64_t hot_path_ns() const noexcept {
        uint64_t sum = 0;
        // Hot stages: 0-6, 8, 11
        for (size_t i = 0; i <= static_cast<size_t>(PipelineStage::MarkToMarket); ++i) {
            sum += stage_ns[i];
        }
        sum += stage_ns[static_cast<size_t>(PipelineStage::UIPublish)];
        return sum;
    }
};

// ---- DeferredWork -----------------------------------------------------------
// Exactly 64 bytes. Fits one x86 cache line, half an Apple Silicon line.
// Hot-path stages push these instead of calling spdlog/recorder inline.
// Queue is drained after hot path completes.

struct alignas(64) DeferredWork {
    uint64_t timestamp_ns;      // 8
    double   values[4];         // 32
    char     tag[13];           // 13
    uint8_t  type;              // 1  (DeferredWorkType)
    uint8_t  severity;          // 1
    uint8_t  _pad;              // 1
};
static_assert(sizeof(DeferredWork) == 64, "DeferredWork must be exactly 64 bytes");

enum class DeferredWorkType : uint8_t {
    LogDebug       = 0,
    LogInfo        = 1,
    LogWarn        = 2,
    LogError       = 3,
    RecordEvent    = 4,
    RecordSignal   = 5,
    RecordPnL      = 6,
    RecordRegime   = 7,
};

// ---- LoadShedState ----------------------------------------------------------
// Tracks how often analytics/RL are skipped due to hot-path overrun.

struct LoadShedState {
    uint64_t shed_count = 0;
    uint64_t total_ticks = 0;
    uint64_t last_shed_tick = 0;

    void record_tick(bool shed, uint64_t tick_id) noexcept {
        ++total_ticks;
        if (shed) {
            ++shed_count;
            last_shed_tick = tick_id;
        }
    }

    double shed_rate_pct() const noexcept {
        return total_ticks > 0
            ? 100.0 * static_cast<double>(shed_count) / static_cast<double>(total_ticks)
            : 0.0;
    }
};

// ---- Arena Watermark Check (debug) ------------------------------------------
// Call before and after hot path to detect unexpected allocations.

#ifndef NDEBUG
#define BYBIT_ARENA_GUARD_BEGIN(arena) \
    [[maybe_unused]] auto _arena_mark_ = (arena).cursor()
#define BYBIT_ARENA_GUARD_END(arena) \
    do { if ((arena).cursor() != _arena_mark_) { \
        budget_violation_detected("arena allocation in hot path"); \
    } } while(0)
#else
#define BYBIT_ARENA_GUARD_BEGIN(arena) ((void)0)
#define BYBIT_ARENA_GUARD_END(arena) ((void)0)
#endif

// Weak callback for violation reporting (overridden in debug harness if desired)
inline void budget_violation_detected(const char* /*reason*/) noexcept {
    // Default: no-op in release, breakpoint-friendly in debug
}

// ---- Path Classification Macros (Stage 4) -----------------------------------
// Documentation markers. Zero runtime cost. For code review and static analysis.
//
// BYBIT_HOT_PATH:  No logging, no allocation, no formatting, no exceptions.
//                  Must complete within declared stage budget.
//                  Examples: feature compute, regime detect, signal gen, risk check.
//
// BYBIT_WARM_PATH: Adjacent to hot path. Bounded cost but not budget-critical.
//                  May do lightweight enqueue to side channels. No formatting.
//                  Examples: mark-to-market, UI snapshot publish, counter updates.
//
// BYBIT_COLD_PATH: Deferred work. May allocate, format, log, do I/O.
//                  Runs AFTER hot path completes or on separate timer.
//                  Examples: spdlog calls, recorder formatting, RL step, analytics.

#define BYBIT_HOT_PATH   /* no alloc, no log, no format, bounded */
#define BYBIT_WARM_PATH  /* bounded, may enqueue, no format */
#define BYBIT_COLD_PATH  /* deferred, may alloc/format/log */

// ---- DeferredWorkQueue (Stage 4) --------------------------------------------
// Fixed-size ring buffer for deferred cold work items.
// Hot path pushes DeferredWork records; cold drain processes them after tick.
// Single-producer (strategy thread), single-consumer (same thread, post-tick).
// Drop-on-full with overflow counter — never blocks the hot path.

class DeferredWorkQueue {
public:
    static constexpr size_t CAPACITY = 64; // power of 2, fits one tick's deferred work
    static constexpr size_t MASK = CAPACITY - 1;

    DeferredWorkQueue() noexcept = default;

    // Push a deferred work item. Returns false if queue is full (item dropped).
    BYBIT_FORCE_INLINE
    bool push(const DeferredWork& item) noexcept {
        if (BYBIT_UNLIKELY(count_ >= CAPACITY)) {
            ++overflow_count_;
            return false;
        }
        items_[(head_ + count_) & MASK] = item;
        ++count_;
        return true;
    }

    // Pop a deferred work item. Returns false if queue is empty.
    bool pop(DeferredWork& out) noexcept {
        if (count_ == 0) return false;
        out = items_[head_ & MASK];
        ++head_;
        --count_;
        return true;
    }

    size_t size() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    void clear() noexcept { head_ = 0; count_ = 0; }

    uint64_t overflow_count() const noexcept { return overflow_count_; }
    void reset_overflow() noexcept { overflow_count_ = 0; }

private:
    alignas(BYBIT_CACHELINE) DeferredWork items_[CAPACITY]{};
    size_t head_ = 0;
    size_t count_ = 0;
    uint64_t overflow_count_ = 0;
};

// ---- Deferred Logging Helpers (Stage 4) -------------------------------------
// Zero-format enqueue from hot path. Cold drain expands to spdlog/recorder.

BYBIT_FORCE_INLINE
bool deferred_log(DeferredWorkQueue& q, DeferredWorkType type,
                        const char* tag, double v0 = 0.0, double v1 = 0.0,
                        double v2 = 0.0, double v3 = 0.0) noexcept {
    DeferredWork w{};
    w.timestamp_ns = TscClock::now_ns();
    w.type = static_cast<uint8_t>(type);
    w.values[0] = v0;
    w.values[1] = v1;
    w.values[2] = v2;
    w.values[3] = v3;
    // Copy tag (up to 12 chars + null)
    if (tag) {
        size_t len = 0;
        while (len < 12 && tag[len]) { w.tag[len] = tag[len]; ++len; }
        w.tag[len] = '\0';
    }
    return q.push(w);
}

// ---- HotPathCounters (Stage 4) ----------------------------------------------
// Cache-line-isolated counters for the strategy tick hot path.
// Single-writer (strategy thread). No atomics needed — plain uint64_t.
// Published to Metrics atomics in warm path after hot stages complete.

struct alignas(BYBIT_CACHELINE) HotPathCounters {
    uint64_t ticks_total = 0;
    uint64_t signals_generated = 0;
    uint64_t budget_exceeded_count = 0;
    uint64_t cold_shed_count = 0;       // ticks where cold work was shed
    uint64_t deferred_overflow = 0;     // deferred queue overflow events
    uint64_t features_computed = 0;
    uint64_t models_inferred = 0;
    uint64_t orders_dispatched = 0;
    uint64_t inference_gated = 0;        // E4: ticks where inference was skipped by confidence gate

    // Publish accumulated deltas to shared Metrics (called in warm path)
    // Returns self for chaining.
    void reset() noexcept {
        ticks_total = 0;
        signals_generated = 0;
        budget_exceeded_count = 0;
        cold_shed_count = 0;
        deferred_overflow = 0;
        features_computed = 0;
        models_inferred = 0;
        orders_dispatched = 0;
        inference_gated = 0;
    }
};

static_assert(std::is_trivially_copyable_v<HotPathCounters>);

// ---- TickColdWork (Stage 4) -------------------------------------------------
// Collects data during the hot path that needs cold-path processing.
// Populated inline (cheap copies), expanded after hot stages complete.

struct TickColdWork {
    // Feature/model logging (populated during hot stages, logged in cold drain)
    bool     log_features = false;
    bool     log_ob_snapshot = false;
    bool     log_prediction = false;
    bool     log_signal = false;
    bool     regime_changed = false;
    bool     budget_warning = false;

    // Latency values captured during hot path for cold-path logging
    uint64_t feat_latency_ns = 0;
    uint64_t model_latency_ns = 0;
    uint64_t e2e_latency_ns = 0;

    // Signal data for cold-path recorder
    double   signal_price = 0.0;
    double   signal_qty = 0.0;
    double   signal_confidence = 0.0;
    double   signal_fill_prob = 0.0;
    uint8_t  signal_side = 0;  // 0=Buy, 1=Sell

    // Regime change data
    uint8_t  regime_prev = 0;
    uint8_t  regime_curr = 0;
    double   regime_confidence = 0.0;
    double   regime_volatility = 0.0;

    void clear() noexcept { *this = TickColdWork{}; }
};

static_assert(std::is_trivially_copyable_v<TickColdWork>);

} // namespace bybit
