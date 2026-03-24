#pragma once

// ─── Performance Signposts + Counters ──────────────────────────────────────────
// Integration with Apple Instruments via os_signpost for all pipeline stages.
// Provides:
//   1. os_signpost intervals for each pipeline stage (visible in Instruments)
//   2. Per-stage HdrHistogram latency tracking
//   3. Flamegraph-compatible export (collapsed stack format)
//   4. Zero overhead when signposts disabled (compile-time or runtime)
//
// Usage:
//   PerfSignpostEngine perf;
//   {
//       auto guard = perf.begin_stage(WatchdogStage::OrderBook);
//       // ... hot path code ...
//   } // automatically ends signpost interval and records latency
//
// Instruments integration:
//   Profile with: Product → Profile → Time Profiler
//   Filter by: "bybit" subsystem, "pipeline" category

#include "hdr_histogram.h"
#include "watchdog.h"
#include "../utils/tsc_clock.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#ifdef __APPLE__
#include <os/log.h>
#include <os/signpost.h>
#endif

namespace bybit {

// ─── Signpost Configuration ────────────────────────────────────────────────────

struct PerfSignpostConfig {
    bool     signposts_enabled = true;   // os_signpost emission
    bool     histograms_enabled = true;  // per-stage HdrHistogram tracking
    uint64_t export_interval_ns = 60'000'000'000ULL; // 60s auto-export
    bool     auto_export        = false; // periodic flamegraph export
};

// ─── Per-Stage Perf Counter ────────────────────────────────────────────────────

struct alignas(128) StagePerfCounter {
    HdrHistogram histogram;
    std::atomic<uint64_t> invocation_count{0};
    std::atomic<uint64_t> total_ns{0};
    std::atomic<uint64_t> last_ns{0};

    void record(uint64_t duration_ns) noexcept {
        histogram.record(static_cast<int64_t>(duration_ns));
        invocation_count.fetch_add(1, std::memory_order_relaxed);
        total_ns.fetch_add(duration_ns, std::memory_order_relaxed);
        last_ns.store(duration_ns, std::memory_order_relaxed);
    }

    void reset() noexcept {
        histogram.reset();
        invocation_count.store(0, std::memory_order_relaxed);
        total_ns.store(0, std::memory_order_relaxed);
        last_ns.store(0, std::memory_order_relaxed);
    }

    struct Summary {
        const char* name;
        int64_t     count;
        double      mean_us;
        double      p50_us;
        double      p99_us;
        double      p999_us;
        double      max_us;
    };

    Summary summarize(const char* name) const noexcept {
        return {
            name,
            histogram.count(),
            histogram.mean_us(),
            histogram.p50_us(),
            histogram.p99_us(),
            histogram.p999_us(),
            static_cast<double>(histogram.max()) / 1000.0
        };
    }
};

// ─── Scoped Signpost Guard ─────────────────────────────────────────────────────
// RAII guard: begins os_signpost interval on construction, ends on destruction.
// Also records latency to HdrHistogram.

class ScopedSignpost {
public:
    ScopedSignpost(StagePerfCounter* counter
#ifdef __APPLE__
                   , os_log_t log, os_signpost_id_t spid, bool signpost_active
#endif
                   ) noexcept
        : counter_(counter)
#ifdef __APPLE__
        , log_(log), spid_(spid), signpost_active_(signpost_active)
#endif
    {
        start_ns_ = TscClock::now_ns();
#ifdef __APPLE__
        if (signpost_active_) {
            os_signpost_interval_begin(log_, spid_, "stage");
        }
#endif
    }

    ~ScopedSignpost() noexcept {
        uint64_t elapsed = TscClock::now_ns() - start_ns_;
#ifdef __APPLE__
        if (signpost_active_) {
            os_signpost_interval_end(log_, spid_, "stage");
        }
#endif
        if (counter_) {
            counter_->record(elapsed);
        }
    }

    // Non-copyable, non-movable
    ScopedSignpost(const ScopedSignpost&) = delete;
    ScopedSignpost& operator=(const ScopedSignpost&) = delete;
    ScopedSignpost(ScopedSignpost&&) = delete;
    ScopedSignpost& operator=(ScopedSignpost&&) = delete;

    uint64_t elapsed_ns() const noexcept {
        return TscClock::now_ns() - start_ns_;
    }

private:
    StagePerfCounter* counter_;
    uint64_t start_ns_;
#ifdef __APPLE__
    os_log_t log_;
    os_signpost_id_t spid_;
    bool signpost_active_;
#endif
};

// ─── Performance Signpost Engine ───────────────────────────────────────────────

class PerfSignpostEngine {
    static constexpr size_t STAGE_COUNT = static_cast<size_t>(WatchdogStage::COUNT);

public:
    explicit PerfSignpostEngine(const PerfSignpostConfig& cfg = {}) noexcept
        : cfg_(cfg)
    {
#ifdef __APPLE__
        log_ = os_log_create("com.bybit.trader", "pipeline");
        for (size_t i = 0; i < STAGE_COUNT; ++i) {
            spids_[i] = os_signpost_id_generate(log_);
        }
#endif
    }

    // ─── Begin Stage Measurement ────────────────────────────────────────────
    // Returns a scoped guard that:
    //   - Emits os_signpost_interval_begin (if enabled)
    //   - Records TSC start time
    //   - On destruction: emits os_signpost_interval_end + records to histogram

    [[nodiscard]]
    ScopedSignpost begin_stage(WatchdogStage stage) noexcept {
        auto idx = static_cast<size_t>(stage);
        StagePerfCounter* ctr = (idx < STAGE_COUNT) ? &counters_[idx] : nullptr;

#ifdef __APPLE__
        os_signpost_id_t spid = (idx < STAGE_COUNT) ? spids_[idx] : OS_SIGNPOST_ID_NULL;
        return ScopedSignpost(ctr, log_, spid, cfg_.signposts_enabled);
#else
        return ScopedSignpost(ctr);
#endif
    }

    // ─── Manual Record (for cases where RAII doesn't fit) ───────────────────

    void record_stage(WatchdogStage stage, uint64_t duration_ns) noexcept {
        auto idx = static_cast<size_t>(stage);
        if (idx < STAGE_COUNT) {
            counters_[idx].record(duration_ns);
        }

#ifdef __APPLE__
        if (cfg_.signposts_enabled && idx < STAGE_COUNT) {
            os_signpost_event_emit(log_, spids_[idx], "latency",
                                   "stage=%zu ns=%llu", idx, duration_ns);
        }
#endif
    }

    // ─── Emit Point Event (for marking specific moments) ───────────────────

    void emit_event(WatchdogStage stage, const char* label) noexcept {
#ifdef __APPLE__
        if (cfg_.signposts_enabled) {
            auto idx = static_cast<size_t>(stage);
            if (idx < STAGE_COUNT) {
                os_signpost_event_emit(log_, spids_[idx], "event",
                                       "%{public}s", label);
            }
        }
#else
        (void)stage; (void)label;
#endif
    }

    // ─── Snapshot All Stages ────────────────────────────────────────────────

    struct PipelineSnapshot {
        StagePerfCounter::Summary stages[8];
        size_t stage_count = 0;
    };

    PipelineSnapshot snapshot() const noexcept {
        PipelineSnapshot snap;
        snap.stage_count = STAGE_COUNT;
        for (size_t i = 0; i < STAGE_COUNT; ++i) {
            snap.stages[i] = counters_[i].summarize(watchdog_stage_name(static_cast<WatchdogStage>(i)));
        }
        return snap;
    }

    // ─── Export to Collapsed Stack Format (for flamegraph) ──────────────────
    // Format: "stage1;stage2 count\n"
    // Compatible with brendangregg/FlameGraph and speedscope.

    bool export_flamegraph(const std::string& path) const noexcept {
        std::ofstream f(path);
        if (!f.is_open()) return false;

        f << "# Bybit HFT Pipeline Flamegraph (collapsed stack format)\n";
        f << "# Generated from HdrHistogram data\n";

        for (size_t i = 0; i < STAGE_COUNT; ++i) {
            auto name = watchdog_stage_name(static_cast<WatchdogStage>(i));
            auto count = counters_[i].invocation_count.load(std::memory_order_relaxed);
            if (count == 0) continue;

            // Per-bucket emission for latency distribution visualization
            // Emit at multiple latency tiers for flamegraph depth
            auto& hist = counters_[i].histogram;
            int64_t p50 = hist.p50();
            int64_t p99 = hist.p99();
            int64_t p999 = hist.p999();

            // Fast path samples (below p50)
            int64_t fast_count = count / 2;
            f << "pipeline;" << name << ";fast(<p50) " << fast_count << "\n";

            // Normal path samples (p50..p99)
            int64_t normal_count = count * 49 / 100;
            f << "pipeline;" << name << ";normal(p50-p99) " << normal_count << "\n";

            // Tail latency samples (>p99)
            int64_t tail_count = count - fast_count - normal_count;
            if (tail_count > 0) {
                f << "pipeline;" << name << ";tail(>p99) " << tail_count << "\n";
            }
        }

        return f.good();
    }

    // ─── Export Latency Summary (text) ──────────────────────────────────────

    bool export_summary(const std::string& path) const noexcept {
        std::ofstream f(path);
        if (!f.is_open()) return false;

        f << "# Bybit HFT Pipeline Latency Summary\n";
        f << "# stage, count, mean_us, p50_us, p99_us, p999_us, max_us\n";

        for (size_t i = 0; i < STAGE_COUNT; ++i) {
            auto s = counters_[i].summarize(
                watchdog_stage_name(static_cast<WatchdogStage>(i)));
            if (s.count == 0) continue;
            f << s.name << ","
              << s.count << ","
              << s.mean_us << ","
              << s.p50_us << ","
              << s.p99_us << ","
              << s.p999_us << ","
              << s.max_us << "\n";
        }

        return f.good();
    }

    // ─── Reset All ──────────────────────────────────────────────────────────

    void reset() noexcept {
        for (auto& c : counters_) c.reset();
    }

    // ─── Accessors ──────────────────────────────────────────────────────────

    const StagePerfCounter& counter(WatchdogStage stage) const noexcept {
        return counters_[static_cast<size_t>(stage)];
    }

    StagePerfCounter& counter(WatchdogStage stage) noexcept {
        return counters_[static_cast<size_t>(stage)];
    }

    const PerfSignpostConfig& config() const noexcept { return cfg_; }

private:
    PerfSignpostConfig cfg_;
    std::array<StagePerfCounter, STAGE_COUNT> counters_{};

#ifdef __APPLE__
    os_log_t log_ = OS_LOG_DISABLED;
    std::array<os_signpost_id_t, STAGE_COUNT> spids_{};
#endif
};

} // namespace bybit
