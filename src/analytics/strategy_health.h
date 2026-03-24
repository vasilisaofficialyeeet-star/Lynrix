#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include "strategy_metrics.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace bybit {

// ─── Strategy Health Monitor ────────────────────────────────────────────────
// Monitors strategy performance and automatically adjusts trading activity.
// Detects regime degradation, accuracy decay, and performance slumps.
// Actions: reduce position size, widen thresholds, or pause trading.

enum class StrategyHealthLevel : uint8_t {
    Excellent = 0,   // All green — full activity
    Good      = 1,   // Minor issues — slight reduction
    Warning   = 2,   // Degradation detected — reduce activity 50%
    Critical  = 3,   // Severe — reduce activity 80%
    Halted    = 4,   // Strategy failed — stop trading
};

struct StrategyHealthSnapshot {
    StrategyHealthLevel level = StrategyHealthLevel::Good;
    double health_score       = 1.0;   // [0, 1] composite score
    double activity_scale     = 1.0;   // multiplier for position sizing [0, 1]
    double threshold_offset   = 0.0;   // added to signal threshold when degrading

    // Component scores [0, 1]
    double accuracy_score     = 0.5;
    double pnl_score          = 0.5;
    double drawdown_score     = 1.0;
    double sharpe_score       = 0.5;
    double consistency_score  = 0.5;
    double fill_rate_score    = 1.0;

    // Diagnostics
    bool   accuracy_declining = false;
    bool   pnl_declining      = false;
    bool   drawdown_warning   = false;
    int    regime_changes_1h  = 0;
    uint64_t last_update_ns   = 0;
};

class StrategyHealthMonitor {
public:
    static constexpr size_t ACCURACY_WINDOW = 64;
    static constexpr size_t PNL_WINDOW = 64;

    StrategyHealthMonitor() noexcept { reset(); }

    void reset() noexcept {
        snapshot_ = {};
        accuracy_history_.fill(0.5);
        pnl_history_.fill(0.0);
        acc_head_ = 0;
        pnl_head_ = 0;
        acc_count_ = 0;
        pnl_count_ = 0;
        regime_change_times_.fill(0);
        regime_change_head_ = 0;
        regime_change_count_ = 0;
    }

    // Update with latest data (call every few seconds or on significant events)
    void update(const StrategyMetricsSnapshot& metrics,
                double rolling_accuracy,
                double fill_rate,
                MarketRegime current_regime) noexcept {

        uint64_t now = Clock::now_ns();
        snapshot_.last_update_ns = now;

        // ── Track accuracy trend ────────────────────────────────────────
        push_accuracy(rolling_accuracy);
        snapshot_.accuracy_score = compute_accuracy_score(rolling_accuracy);
        snapshot_.accuracy_declining = is_declining(accuracy_history_, acc_count_);

        // ── Track PnL trend ─────────────────────────────────────────────
        push_pnl(metrics.total_pnl);
        snapshot_.pnl_score = compute_pnl_score(metrics);
        snapshot_.pnl_declining = is_pnl_declining();

        // ── Drawdown score ──────────────────────────────────────────────
        snapshot_.drawdown_score = compute_drawdown_score(metrics.max_drawdown_pct);
        snapshot_.drawdown_warning = metrics.current_drawdown > 0.03;

        // ── Sharpe score ────────────────────────────────────────────────
        snapshot_.sharpe_score = compute_sharpe_score(metrics.sharpe_ratio);

        // ── Consistency score ───────────────────────────────────────────
        snapshot_.consistency_score = compute_consistency_score(metrics);

        // ── Fill rate score ─────────────────────────────────────────────
        snapshot_.fill_rate_score = std::clamp(fill_rate, 0.0, 1.0);

        // ── Track regime changes ────────────────────────────────────────
        if (current_regime != last_regime_) {
            regime_change_times_[regime_change_head_ % 32] = now;
            regime_change_head_++;
            if (regime_change_count_ < 32) regime_change_count_++;
            last_regime_ = current_regime;
        }

        // Count regime changes in last hour
        uint64_t one_hour_ns = 3600ULL * 1'000'000'000ULL;
        int recent_changes = 0;
        for (size_t i = 0; i < regime_change_count_; ++i) {
            size_t idx = (regime_change_head_ - 1 - i) % 32;
            if (now - regime_change_times_[idx] < one_hour_ns) {
                recent_changes++;
            }
        }
        snapshot_.regime_changes_1h = recent_changes;

        // ── Composite health score ──────────────────────────────────────
        snapshot_.health_score =
            snapshot_.accuracy_score * 0.25 +
            snapshot_.pnl_score * 0.20 +
            snapshot_.drawdown_score * 0.20 +
            snapshot_.sharpe_score * 0.15 +
            snapshot_.consistency_score * 0.10 +
            snapshot_.fill_rate_score * 0.10;

        // ── Determine health level ──────────────────────────────────────
        double hs = snapshot_.health_score;
        if (hs >= 0.7) {
            snapshot_.level = StrategyHealthLevel::Excellent;
            snapshot_.activity_scale = 1.0;
            snapshot_.threshold_offset = 0.0;
        } else if (hs >= 0.5) {
            snapshot_.level = StrategyHealthLevel::Good;
            snapshot_.activity_scale = 0.9;
            snapshot_.threshold_offset = 0.02;
        } else if (hs >= 0.3) {
            snapshot_.level = StrategyHealthLevel::Warning;
            snapshot_.activity_scale = 0.5;
            snapshot_.threshold_offset = 0.05;
        } else if (hs >= 0.15) {
            snapshot_.level = StrategyHealthLevel::Critical;
            snapshot_.activity_scale = 0.2;
            snapshot_.threshold_offset = 0.10;
        } else {
            snapshot_.level = StrategyHealthLevel::Halted;
            snapshot_.activity_scale = 0.0;
            snapshot_.threshold_offset = 0.20;
        }

        // Override: if many regime changes, reduce activity
        if (recent_changes > 10) {
            snapshot_.activity_scale *= 0.5;
            snapshot_.threshold_offset += 0.03;
        }
    }

    const StrategyHealthSnapshot& snapshot() const noexcept { return snapshot_; }

    static const char* level_name(StrategyHealthLevel level) noexcept {
        switch (level) {
            case StrategyHealthLevel::Excellent: return "EXCELLENT";
            case StrategyHealthLevel::Good:      return "GOOD";
            case StrategyHealthLevel::Warning:    return "WARNING";
            case StrategyHealthLevel::Critical:   return "CRITICAL";
            case StrategyHealthLevel::Halted:     return "HALTED";
        }
        return "UNKNOWN";
    }

private:
    void push_accuracy(double acc) noexcept {
        accuracy_history_[acc_head_ % ACCURACY_WINDOW] = acc;
        acc_head_++;
        if (acc_count_ < ACCURACY_WINDOW) acc_count_++;
    }

    void push_pnl(double pnl) noexcept {
        pnl_history_[pnl_head_ % PNL_WINDOW] = pnl;
        pnl_head_++;
        if (pnl_count_ < PNL_WINDOW) pnl_count_++;
    }

    double compute_accuracy_score(double accuracy) const noexcept {
        // 0.6+ -> 1.0, 0.33 -> 0.0 (random = 0.33 for 3-class)
        return std::clamp((accuracy - 0.33) / 0.37, 0.0, 1.0);
    }

    double compute_pnl_score(const StrategyMetricsSnapshot& m) const noexcept {
        if (m.total_trades < 5) return 0.5; // not enough data
        double per_trade = m.total_pnl / std::max(m.total_trades, 1);
        // Map per-trade PnL: -$10 -> 0, $0 -> 0.5, $10+ -> 1.0
        return std::clamp(0.5 + per_trade / 20.0, 0.0, 1.0);
    }

    double compute_drawdown_score(double max_dd) const noexcept {
        // 0% -> 1.0, 5% -> 0.5, 10%+ -> 0.0
        return std::clamp(1.0 - max_dd / 0.10, 0.0, 1.0);
    }

    double compute_sharpe_score(double sharpe) const noexcept {
        // <0 -> 0, 0 -> 0.3, 1 -> 0.6, 2+ -> 1.0
        return std::clamp(0.3 + sharpe * 0.35, 0.0, 1.0);
    }

    double compute_consistency_score(const StrategyMetricsSnapshot& m) const noexcept {
        if (m.total_trades < 10) return 0.5;
        // Based on max consecutive losses vs total trades
        double loss_ratio = static_cast<double>(m.max_consecutive_losses)
                            / std::max(m.total_trades, 1);
        return std::clamp(1.0 - loss_ratio * 5.0, 0.0, 1.0);
    }

    template <size_t N>
    bool is_declining(const std::array<double, N>& buf, size_t count) const noexcept {
        if (count < 10) return false;
        // Compare recent half vs earlier half
        size_t half = count / 2;
        double recent_sum = 0.0, older_sum = 0.0;
        size_t head = (count == N) ? acc_head_ : 0;
        for (size_t i = 0; i < half; ++i) {
            recent_sum += buf[(head + count - 1 - i) % N];
            older_sum += buf[(head + count / 2 - 1 - i) % N];
        }
        return (recent_sum / half) < (older_sum / half) * 0.9;
    }

    bool is_pnl_declining() const noexcept {
        if (pnl_count_ < 4) return false;
        // Check if recent PnL is worse than earlier
        size_t recent = pnl_count_ / 2;
        double last_pnl = pnl_history_[(pnl_head_ - 1) % PNL_WINDOW];
        double mid_pnl = pnl_history_[(pnl_head_ - recent) % PNL_WINDOW];
        return last_pnl < mid_pnl;
    }

    StrategyHealthSnapshot snapshot_;

    std::array<double, ACCURACY_WINDOW> accuracy_history_{};
    std::array<double, PNL_WINDOW> pnl_history_{};
    size_t acc_head_ = 0, pnl_head_ = 0;
    size_t acc_count_ = 0, pnl_count_ = 0;

    std::array<uint64_t, 32> regime_change_times_{};
    size_t regime_change_head_ = 0;
    size_t regime_change_count_ = 0;
    MarketRegime last_regime_ = MarketRegime::LowVolatility;
};

} // namespace bybit
