#pragma once

// Thread-safe UI snapshot system using SeqLock (lock-free, zero allocation).
// Engine thread publishes snapshots at end of each tick.
// GUI thread reads snapshots without blocking the engine.

#include "../config/types.h"
#include "../core/system_control.h"
#include "../analytics/strategy_metrics.h"
#include "../analytics/strategy_health.h"
#include "../analytics/feature_importance.h"
#include "../monitoring/system_monitor.h"
#include "../rl/rl_optimizer.h"
#include "../model_engine/accuracy_tracker.h"

#include <atomic>
#include <cstring>

namespace bybit {

static constexpr int UI_OB_LEVELS = 20;

struct OBLevelSnap {
    double price = 0.0;
    double qty   = 0.0;
};

// ─── Complete UI Snapshot ────────────────────────────────────────────────────
// All data the GUI needs, captured atomically from the engine thread.
// ~4KB — trivially copyable, no heap allocations.

struct UISnapshot {
    // ── OrderBook ────────────────────────────────────────────────────────
    OBLevelSnap bids[UI_OB_LEVELS]{};
    OBLevelSnap asks[UI_OB_LEVELS]{};
    int      bid_count        = 0;
    int      ask_count        = 0;
    double   best_bid         = 0.0;
    double   best_ask         = 0.0;
    double   mid_price        = 0.0;
    double   spread           = 0.0;
    double   microprice       = 0.0;
    uint64_t ob_last_update   = 0;
    bool     ob_valid         = false;

    // ── Position ─────────────────────────────────────────────────────────
    double   pos_size         = 0.0;
    double   pos_entry        = 0.0;
    double   pos_unrealized   = 0.0;
    double   pos_realized     = 0.0;
    double   pos_funding      = 0.0;
    bool     pos_is_long      = true;

    // ── Metrics (counters + latency percentiles) ─────────────────────────
    uint64_t ob_updates       = 0;
    uint64_t trades_total     = 0;
    uint64_t signals_total    = 0;
    uint64_t orders_sent      = 0;
    uint64_t orders_filled    = 0;
    uint64_t orders_cancelled = 0;
    uint64_t ws_reconnects    = 0;
    uint64_t e2e_p50_ns       = 0;
    uint64_t e2e_p99_ns       = 0;
    uint64_t feat_p50_ns      = 0;
    uint64_t feat_p99_ns      = 0;
    uint64_t model_p50_ns     = 0;
    uint64_t model_p99_ns     = 0;

    // ── Features (25 doubles) ────────────────────────────────────────────
    Features features{};

    // ── Regime ────────────────────────────────────────────────────────────
    RegimeState regime{};

    // ── Model Prediction ─────────────────────────────────────────────────
    ModelOutput prediction{};

    // ── Adaptive Threshold ───────────────────────────────────────────────
    AdaptiveThresholdState threshold{};

    // ── Circuit Breaker ──────────────────────────────────────────────────
    bool     cb_tripped       = false;
    bool     cb_cooldown      = false;
    int      cb_consec_losses = 0;
    double   cb_peak_pnl      = 0.0;
    double   cb_drawdown      = 0.0;

    // ── Accuracy ─────────────────────────────────────────────────────────
    AccuracyMetrics accuracy{};
    bool     using_onnx       = false;

    // ── Strategy Metrics ─────────────────────────────────────────────────
    StrategyMetricsSnapshot strategy_metrics{};

    // ── Strategy Health ──────────────────────────────────────────────────
    StrategyHealthSnapshot strategy_health{};

    // ── System Monitor ───────────────────────────────────────────────────
    SystemMonitorSnapshot system_monitor{};

    // ── RL Optimizer ─────────────────────────────────────────────────────
    RLOptimizerSnapshot rl_state{};

    // ── Feature Importance ───────────────────────────────────────────────
    FeatureImportanceSnapshot feature_importance{};

    // ── Stage 6: Control Plane State ─────────────────────────────────────
    uint8_t  risk_state       = 0;   // RiskState enum
    uint8_t  exec_state       = 0;   // ExecState enum
    uint8_t  system_mode      = 0;   // SystemMode enum
    double   ctrl_position_scale  = 1.0;  // combined risk + health scale
    double   ctrl_throttle_factor = 1.0;  // exec throttle
    bool     ctrl_allows_new_orders = true;
    bool     ctrl_allows_increase   = true;
    uint32_t ctrl_total_transitions = 0;
    uint64_t ctrl_audit_depth       = 0;

    // ── Engine state ─────────────────────────────────────────────────────
    char     inference_backend[32] = "CPU";
    bool     engine_running   = false;
    uint64_t ws_rtt_us        = 0;   // E5: WebSocket round-trip time in microseconds

    // ── Timestamp of this snapshot ───────────────────────────────────────
    uint64_t snapshot_ns      = 0;
};

// ─── SeqLock ─────────────────────────────────────────────────────────────────
// Single-writer / multiple-reader lock-free synchronization.
// Writer: engine thread calls publish() at end of each strategy_tick().
// Reader: GUI thread calls read() at 10-30 FPS polling rate.
// No mutex, no allocation, no syscall. Pure atomic fence.

class SnapshotSeqLock {
public:
    // Called by engine thread only (single writer)
    void publish(const UISnapshot& snap) noexcept {
        seq_.fetch_add(1, std::memory_order_release);   // odd → writing
        data_ = snap;
        seq_.fetch_add(1, std::memory_order_release);   // even → done
    }

    // Called by GUI thread (may be called concurrently with publish)
    UISnapshot read() const noexcept {
        UISnapshot tmp;
        uint64_t s1, s2;
        int spins = 0;
        do {
            s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1) {
                // Writer active — brief spin (typically < 1µs)
                if (++spins > 1000) return tmp;  // safety: return stale data
                continue;
            }
            tmp = data_;
            std::atomic_thread_fence(std::memory_order_acquire);
            s2 = seq_.load(std::memory_order_acquire);
        } while (s1 != s2);
        return tmp;
    }

    uint64_t version() const noexcept {
        return seq_.load(std::memory_order_acquire) / 2;
    }

private:
    UISnapshot data_{};
    std::atomic<uint64_t> seq_{0};
};

} // namespace bybit
