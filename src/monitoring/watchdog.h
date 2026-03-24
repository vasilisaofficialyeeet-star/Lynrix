#pragma once

// ─── Watchdog Thread v2 + Jitter-Based Auto-Restart ─────────────────────────
// Monitors all pipeline stages for:
//   1. Liveness (heartbeat timeout)
//   2. Jitter (>3 µs stage-to-stage latency triggers restart)
//   3. Per-stage latency tracking (EMA smoothed)
//
// Uses TSC timing for <2 ns measurement overhead.
// Runs on E-core with Utility QoS to avoid interfering with hot path.

#include "../utils/tsc_clock.h"
#include "../utils/clock.h"
#include "../core/strong_types.h"
#include "../core/memory_policy.h"
#include "../core/clock_source.h"
#include "hdr_histogram.h"
#include <atomic>
#include <array>
#include <cstdint>
#include <cmath>
#include <functional>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>

#include <spdlog/spdlog.h>

namespace bybit {

// ─── Watchdog Stage Enum ────────────────────────────────────────────────────
// Renamed from PipelineStage to avoid collision with hot_path.h::PipelineStage.
// These are coarse-grained MONITORING stages (8), not the fine-grained
// pipeline timing stages (12) in hot_path.h.

enum class WatchdogStage : uint8_t {
    WebSocket   = 0,
    Parser      = 1,
    OrderBook   = 2,
    Features    = 3,
    Model       = 4,
    Signal      = 5,
    Risk        = 6,
    Execution   = 7,
    COUNT       = 8,
};

// NOTE: Previously 'using PipelineStage = WatchdogStage;' existed here,
// but it collides with hot_path.h::PipelineStage (the fine-grained 12-stage enum).
// Callers should migrate to WatchdogStage directly.
// perf_signpost.h is updated to use WatchdogStage.

inline const char* watchdog_stage_name(WatchdogStage s) noexcept {
    constexpr const char* names[] = {
        "WebSocket", "Parser", "OrderBook", "Features",
        "Model", "Signal", "Risk", "Execution"
    };
    auto idx = static_cast<size_t>(s);
    return idx < 8 ? names[idx] : "Unknown";
}

// Backward-compat overload.
inline const char* stage_name(WatchdogStage s) noexcept {
    return watchdog_stage_name(s);
}

// ─── Per-Stage Latency Stats ────────────────────────────────────────────────
// Tracks last/min/max/ema latency per stage. Lock-free, single-writer.

struct alignas(64) StageLatencyStats {
    std::atomic<uint64_t> last_ns{0};
    std::atomic<uint64_t> min_ns{UINT64_MAX};
    std::atomic<uint64_t> max_ns{0};
    std::atomic<uint64_t> ema_ns{0};       // EMA-smoothed latency (α=0.05)
    std::atomic<uint64_t> jitter_count{0}; // times latency exceeded threshold
    std::atomic<uint64_t> sample_count{0};

    void record(uint64_t latency_ns) noexcept {
        last_ns.store(latency_ns, std::memory_order_relaxed);
        sample_count.fetch_add(1, std::memory_order_relaxed);

        // Update min/max atomically (relaxed is fine for monitoring)
        uint64_t cur_min = min_ns.load(std::memory_order_relaxed);
        while (latency_ns < cur_min &&
               !min_ns.compare_exchange_weak(cur_min, latency_ns,
                   std::memory_order_relaxed)) {}

        uint64_t cur_max = max_ns.load(std::memory_order_relaxed);
        while (latency_ns > cur_max &&
               !max_ns.compare_exchange_weak(cur_max, latency_ns,
                   std::memory_order_relaxed)) {}

        // EMA update: ema = α * sample + (1-α) * ema, α = 0.05
        // Using fixed-point: multiply by 13/256 ≈ 0.0508
        uint64_t old_ema = ema_ns.load(std::memory_order_relaxed);
        uint64_t new_ema = old_ema == 0 ? latency_ns
                           : (latency_ns * 13 + old_ema * 243) / 256;
        ema_ns.store(new_ema, std::memory_order_relaxed);
    }

    void reset() noexcept {
        last_ns.store(0, std::memory_order_relaxed);
        min_ns.store(UINT64_MAX, std::memory_order_relaxed);
        max_ns.store(0, std::memory_order_relaxed);
        ema_ns.store(0, std::memory_order_relaxed);
        jitter_count.store(0, std::memory_order_relaxed);
        sample_count.store(0, std::memory_order_relaxed);
    }
};

// ─── Heartbeat Registry v2 ──────────────────────────────────────────────────
// Each pipeline stage calls heartbeat() with its processing latency.
// Watchdog checks liveness AND jitter.
// Template on ClockSource: TscClockSource (production) or ReplayClockSource (replay).

template <typename ClockSource = TscClockSource>
class HeartbeatRegistry {
public:
    HeartbeatRegistry() noexcept = default;
    explicit HeartbeatRegistry(ClockSource clock) noexcept : clock_(std::move(clock)) {}

    // Simple heartbeat (liveness only)
    void heartbeat(WatchdogStage stage) noexcept {
        auto idx = static_cast<size_t>(stage);
        if (__builtin_expect(idx < static_cast<size_t>(WatchdogStage::COUNT), 1)) {
            last_heartbeat_[idx].store(clock_.now().raw(), std::memory_order_relaxed);
        }
    }

    // Heartbeat with latency report (liveness + jitter tracking + histogram)
    void heartbeat(WatchdogStage stage, uint64_t processing_latency_ns) noexcept {
        auto idx = static_cast<size_t>(stage);
        if (__builtin_expect(idx < static_cast<size_t>(WatchdogStage::COUNT), 1)) {
            last_heartbeat_[idx].store(clock_.now().raw(), std::memory_order_relaxed);
            latency_stats_[idx].record(processing_latency_ns);
            histograms_[idx].record(static_cast<int64_t>(processing_latency_ns));
        }
    }

    uint64_t last_heartbeat_ns(WatchdogStage stage) const noexcept {
        auto idx = static_cast<size_t>(stage);
        if (__builtin_expect(idx < static_cast<size_t>(WatchdogStage::COUNT), 1)) {
            return last_heartbeat_[idx].load(std::memory_order_relaxed);
        }
        return 0;
    }

    // Check if a stage is stale (no heartbeat within timeout_ns)
    bool is_stale(WatchdogStage stage, uint64_t timeout_ns) const noexcept {
        uint64_t now = clock_.now().raw();
        uint64_t last = last_heartbeat_ns(stage);
        if (last == 0) return false; // never started, not stale
        return (now - last) > timeout_ns;
    }

    // Access per-stage latency stats (for monitoring/UI)
    const StageLatencyStats& latency(WatchdogStage stage) const noexcept {
        return latency_stats_[static_cast<size_t>(stage)];
    }

    StageLatencyStats& latency(WatchdogStage stage) noexcept {
        return latency_stats_[static_cast<size_t>(stage)];
    }

    // Access per-stage HdrHistogram (for detailed latency distribution)
    const HdrHistogram& histogram(WatchdogStage stage) const noexcept {
        return histograms_[static_cast<size_t>(stage)];
    }

    HdrHistogram& histogram(WatchdogStage stage) noexcept {
        return histograms_[static_cast<size_t>(stage)];
    }

    // Export all per-stage histograms to CSV
    bool export_histograms(const std::string& path) const noexcept {
        std::ofstream f(path);
        if (!f.is_open()) return false;

        f << "stage,count,mean_us,p50_us,p90_us,p95_us,p99_us,p999_us,max_us,stddev_us\n";
        for (size_t i = 0; i < static_cast<size_t>(WatchdogStage::COUNT); ++i) {
            auto& h = histograms_[i];
            if (h.count() == 0) continue;
            f << watchdog_stage_name(static_cast<WatchdogStage>(i)) << ","
              << h.count() << ","
              << h.mean_us() << ","
              << h.p50_us() << ","
              << h.p90_us() << ","
              << h.p95_us() << ","
              << h.p99_us() << ","
              << h.p999_us() << ","
              << static_cast<double>(h.max()) / 1000.0 << ","
              << h.stddev() / 1000.0 << "\n";
        }
        return f.good();
    }

    void reset_histograms() noexcept {
        for (auto& h : histograms_) h.reset();
    }

    // Access the clock source
    ClockSource& clock() noexcept { return clock_; }
    const ClockSource& clock() const noexcept { return clock_; }

private:
    ClockSource clock_{};
    alignas(128) std::array<std::atomic<uint64_t>,
        static_cast<size_t>(WatchdogStage::COUNT)> last_heartbeat_{};
    alignas(128) std::array<StageLatencyStats,
        static_cast<size_t>(WatchdogStage::COUNT)> latency_stats_{};
    std::array<HdrHistogram,
        static_cast<size_t>(WatchdogStage::COUNT)> histograms_{};
};

// ─── Watchdog Configuration ─────────────────────────────────────────────────

struct WatchdogConfig {
    uint64_t check_interval_ms    = 100;     // How often watchdog checks (was 500)
    uint64_t stage_timeout_ms     = 5000;    // Max allowed silence per stage
    uint64_t ws_timeout_ms        = 10000;   // WebSocket specific (reconnect)
    uint64_t critical_timeout_ms  = 30000;   // Trigger emergency stop
    uint64_t jitter_threshold_ns  = 3000;    // 3 µs jitter → trigger restart
    uint64_t jitter_window        = 100;     // Check last N samples for jitter
    bool     auto_restart         = true;    // Auto-restart stalled stages
    bool     emergency_cancel     = true;    // Cancel orders on critical stall
    bool     jitter_restart       = true;    // Auto-restart on sustained jitter
};

// ─── Watchdog Callbacks ─────────────────────────────────────────────────────

struct WatchdogCallbacks {
    std::function<void()> on_ws_stall;          // WebSocket stalled
    std::function<void()> on_stage_stall;       // Any stage stalled
    std::function<void()> on_emergency_stop;    // Critical timeout reached
    std::function<void()> on_restart;           // Hot restart initiated
    std::function<void(WatchdogStage, uint64_t)> on_jitter; // Jitter detected (stage, latency_ns)
};

// ─── Watchdog Thread v2 ─────────────────────────────────────────────────────

template <typename ClockSource = TscClockSource>
class Watchdog {
public:
    Watchdog(HeartbeatRegistry<ClockSource>& registry, const WatchdogConfig& cfg,
             WatchdogCallbacks callbacks = {})
        : registry_(registry), cfg_(cfg), callbacks_(std::move(callbacks))
    {}

    ~Watchdog() { stop(); }

    void start() noexcept {
        if (running_.exchange(true)) return;
        thread_ = std::thread([this] { run(); });
    }

    void stop() noexcept {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }

    bool is_running() const noexcept { return running_.load(); }

    // Manual health query
    bool all_healthy() const noexcept {
        uint64_t timeout_ns = cfg_.stage_timeout_ms * 1'000'000ULL;
        for (size_t i = 0; i < static_cast<size_t>(WatchdogStage::COUNT); ++i) {
            auto stage = static_cast<WatchdogStage>(i);
            if (registry_.is_stale(stage, timeout_ns)) return false;
        }
        return true;
    }

    // Check if any stage has jitter above threshold
    bool has_jitter() const noexcept {
        for (size_t i = 0; i < static_cast<size_t>(WatchdogStage::COUNT); ++i) {
            auto stage = static_cast<WatchdogStage>(i);
            uint64_t ema = registry_.latency(stage).ema_ns.load(std::memory_order_relaxed);
            if (ema > cfg_.jitter_threshold_ns) return true;
        }
        return false;
    }

    uint64_t restart_count() const noexcept { return restart_count_; }
    uint64_t stall_count() const noexcept { return stall_count_; }
    uint64_t jitter_restart_count() const noexcept { return jitter_restart_count_; }

private:
    void run() noexcept {
        // Note: thread affinity should be set by caller via configure_background_thread()
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(cfg_.check_interval_ms));

            if (!running_.load(std::memory_order_relaxed)) break;

            check_stages();
            if (cfg_.jitter_restart) {
                check_jitter();
            }
        }
    }

    void check_stages() noexcept {
        uint64_t stage_timeout_ns = cfg_.stage_timeout_ms * 1'000'000ULL;
        uint64_t ws_timeout_ns = cfg_.ws_timeout_ms * 1'000'000ULL;
        uint64_t critical_ns = cfg_.critical_timeout_ms * 1'000'000ULL;

        bool any_stale = false;
        bool critical = false;

        for (size_t i = 0; i < static_cast<size_t>(WatchdogStage::COUNT); ++i) {
            auto stage = static_cast<WatchdogStage>(i);
            uint64_t timeout = (stage == WatchdogStage::WebSocket)
                               ? ws_timeout_ns : stage_timeout_ns;

            if (registry_.is_stale(stage, timeout)) {
                ++stall_count_;
                any_stale = true;
                spdlog::warn("[WATCHDOG] Stage {} stalled (last={}ns ago)",
                             watchdog_stage_name(stage),
                             Clock::now_ns() - registry_.last_heartbeat_ns(stage));

                if (stage == WatchdogStage::WebSocket && callbacks_.on_ws_stall) {
                    callbacks_.on_ws_stall();
                }

                // Check for critical timeout
                if (registry_.is_stale(stage, critical_ns)) {
                    critical = true;
                }
            }
        }

        if (critical) {
            spdlog::error("[WATCHDOG] CRITICAL: stage exceeded {}ms timeout",
                          cfg_.critical_timeout_ms);
            if (cfg_.emergency_cancel && callbacks_.on_emergency_stop) {
                callbacks_.on_emergency_stop();
            }
        } else if (any_stale && cfg_.auto_restart) {
            spdlog::warn("[WATCHDOG] Initiating hot restart");
            ++restart_count_;
            if (callbacks_.on_restart) {
                callbacks_.on_restart();
            }
        }

        if (any_stale && callbacks_.on_stage_stall) {
            callbacks_.on_stage_stall();
        }
    }

    void check_jitter() noexcept {
        // Check each stage's EMA latency against jitter threshold
        for (size_t i = 0; i < static_cast<size_t>(WatchdogStage::COUNT); ++i) {
            auto stage = static_cast<WatchdogStage>(i);
            auto& stats = registry_.latency(stage);

            uint64_t ema = stats.ema_ns.load(std::memory_order_relaxed);
            uint64_t last = stats.last_ns.load(std::memory_order_relaxed);

            // Skip stages that haven't reported yet
            if (stats.sample_count.load(std::memory_order_relaxed) == 0) continue;

            // Jitter detection: if last measurement OR EMA exceeds threshold
            bool jitter_detected = (last > cfg_.jitter_threshold_ns) ||
                                   (ema > cfg_.jitter_threshold_ns);

            if (jitter_detected) {
                stats.jitter_count.fetch_add(1, std::memory_order_relaxed);
                uint64_t jc = stats.jitter_count.load(std::memory_order_relaxed);

                spdlog::warn("[WATCHDOG] JITTER: stage={} last={}ns ema={}ns threshold={}ns (count={})",
                             watchdog_stage_name(stage), last, ema,
                             cfg_.jitter_threshold_ns, jc);

                // Fire callback
                if (callbacks_.on_jitter) {
                    callbacks_.on_jitter(stage, last);
                }

                // Auto-restart if sustained jitter (>5 consecutive)
                if (jc > 5 && cfg_.auto_restart) {
                    spdlog::error("[WATCHDOG] Sustained jitter on {}, initiating restart",
                                  watchdog_stage_name(stage));
                    ++jitter_restart_count_;
                    stats.jitter_count.store(0, std::memory_order_relaxed);
                    if (callbacks_.on_restart) {
                        callbacks_.on_restart();
                    }
                }
            } else {
                // Reset jitter count on clean measurement
                uint64_t jc = stats.jitter_count.load(std::memory_order_relaxed);
                if (jc > 0) {
                    // Decay jitter count slowly
                    stats.jitter_count.store(jc > 0 ? jc - 1 : 0, std::memory_order_relaxed);
                }
            }
        }
    }

    HeartbeatRegistry<ClockSource>& registry_;
    WatchdogConfig cfg_;
    WatchdogCallbacks callbacks_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    uint64_t restart_count_ = 0;
    uint64_t stall_count_ = 0;
    uint64_t jitter_restart_count_ = 0;
};

// ─── Convenience Aliases ─────────────────────────────────────────────────────
// Default production types (backward compatible — existing code just uses these names).
using DefaultHeartbeatRegistry = HeartbeatRegistry<TscClockSource>;
using DefaultWatchdog = Watchdog<TscClockSource>;

} // namespace bybit
