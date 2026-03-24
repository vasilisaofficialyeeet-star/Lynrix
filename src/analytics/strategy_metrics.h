#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace bybit {

// ─── Real-time Strategy Performance Metrics ─────────────────────────────────
// Computes Sharpe, Sortino, max drawdown, profit factor, win rate, etc.
// All computations are incremental — O(1) per trade update.

struct StrategyMetricsSnapshot {
    double sharpe_ratio      = 0.0;
    double sortino_ratio     = 0.0;
    double max_drawdown_pct  = 0.0;
    double current_drawdown  = 0.0;
    double profit_factor     = 0.0;
    double win_rate          = 0.0;
    double avg_win           = 0.0;
    double avg_loss          = 0.0;
    double expectancy        = 0.0;   // avg_win * win_rate - avg_loss * (1-win_rate)
    double total_pnl         = 0.0;
    double best_trade        = 0.0;
    double worst_trade       = 0.0;
    int    total_trades      = 0;
    int    winning_trades    = 0;
    int    losing_trades     = 0;
    int    consecutive_wins  = 0;
    int    consecutive_losses = 0;
    int    max_consecutive_wins  = 0;
    int    max_consecutive_losses = 0;
    double daily_pnl         = 0.0;
    double hourly_pnl        = 0.0;
    // Calmar ratio (annualized return / max drawdown)
    double calmar_ratio      = 0.0;
    // Recovery factor (total pnl / max drawdown)
    double recovery_factor   = 0.0;
    uint64_t last_update_ns  = 0;
};

class StrategyMetrics {
public:
    static constexpr size_t RETURN_WINDOW = 1024; // rolling window for Sharpe/Sortino
    static constexpr double RISK_FREE_RATE = 0.0; // annualized, typically 0 for HFT
    static inline const double ANNUALIZATION = std::sqrt(252.0 * 24.0 * 60.0); // per-minute returns

    StrategyMetrics() noexcept { reset(); }

    void reset() noexcept {
        snapshot_ = {};
        peak_equity_ = 0.0;
        total_equity_ = 0.0;
        gross_profit_ = 0.0;
        gross_loss_ = 0.0;
        sum_returns_ = 0.0;
        sum_sq_returns_ = 0.0;
        sum_neg_sq_returns_ = 0.0;
        return_count_ = 0;
        return_head_ = 0;
        returns_.fill(0.0);
        current_streak_ = 0;
        session_start_ns_ = Clock::now_ns();
        last_equity_snapshot_ = 0.0;
        last_snapshot_ns_ = session_start_ns_;
    }

    // Record a completed trade PnL
    void record_trade(double pnl) noexcept {
        auto& s = snapshot_;
        s.total_trades++;
        s.total_pnl += pnl;

        if (pnl > 0.0) {
            s.winning_trades++;
            gross_profit_ += pnl;
            s.best_trade = std::max(s.best_trade, pnl);

            if (current_streak_ >= 0) {
                current_streak_++;
            } else {
                s.max_consecutive_losses = std::max(s.max_consecutive_losses, -current_streak_);
                current_streak_ = 1;
            }
            s.consecutive_wins = current_streak_;
            s.consecutive_losses = 0;
            s.max_consecutive_wins = std::max(s.max_consecutive_wins, current_streak_);
        } else if (pnl < 0.0) {
            s.losing_trades++;
            gross_loss_ += std::abs(pnl);
            s.worst_trade = std::min(s.worst_trade, pnl);

            if (current_streak_ <= 0) {
                current_streak_--;
            } else {
                s.max_consecutive_wins = std::max(s.max_consecutive_wins, current_streak_);
                current_streak_ = -1;
            }
            s.consecutive_losses = -current_streak_;
            s.consecutive_wins = 0;
            s.max_consecutive_losses = std::max(s.max_consecutive_losses, -current_streak_);
        }

        // Win rate
        s.win_rate = s.total_trades > 0
            ? static_cast<double>(s.winning_trades) / s.total_trades : 0.0;

        // Average win/loss
        s.avg_win = s.winning_trades > 0 ? gross_profit_ / s.winning_trades : 0.0;
        s.avg_loss = s.losing_trades > 0 ? gross_loss_ / s.losing_trades : 0.0;

        // Profit factor
        s.profit_factor = gross_loss_ > 1e-12 ? gross_profit_ / gross_loss_ : 999.0;

        // Expectancy
        s.expectancy = s.avg_win * s.win_rate - s.avg_loss * (1.0 - s.win_rate);

        s.last_update_ns = Clock::now_ns();
    }

    // Update equity for drawdown tracking (call every tick or on PnL change)
    void update_equity(double equity) noexcept {
        total_equity_ = equity;

        // Track peak and drawdown
        if (equity > peak_equity_) {
            peak_equity_ = equity;
        }

        double dd = 0.0;
        if (peak_equity_ > 1e-12) {
            dd = (peak_equity_ - equity) / peak_equity_;
        }
        snapshot_.current_drawdown = dd;
        snapshot_.max_drawdown_pct = std::max(snapshot_.max_drawdown_pct, dd);

        // Recovery factor
        if (snapshot_.max_drawdown_pct > 1e-12) {
            double max_dd_abs = peak_equity_ * snapshot_.max_drawdown_pct;
            snapshot_.recovery_factor = max_dd_abs > 1e-12
                ? snapshot_.total_pnl / max_dd_abs : 0.0;
        }
    }

    // Record periodic return for Sharpe/Sortino (call every minute or every N ticks)
    void record_return(double ret) noexcept {
        // Remove oldest return from running sums if buffer is full
        if (return_count_ >= RETURN_WINDOW) {
            double old = returns_[return_head_];
            sum_returns_ -= old;
            sum_sq_returns_ -= old * old;
            if (old < 0.0) sum_neg_sq_returns_ -= old * old;
        }

        // Add new return
        returns_[return_head_] = ret;
        sum_returns_ += ret;
        sum_sq_returns_ += ret * ret;
        if (ret < 0.0) sum_neg_sq_returns_ += ret * ret;

        return_head_ = (return_head_ + 1) % RETURN_WINDOW;
        if (return_count_ < RETURN_WINDOW) return_count_++;

        // Compute Sharpe
        if (return_count_ >= 10) {
            double n = static_cast<double>(return_count_);
            double mean = sum_returns_ / n;
            double var = (sum_sq_returns_ / n) - (mean * mean);
            double std_dev = std::sqrt(std::max(var, 1e-20));

            snapshot_.sharpe_ratio = (std_dev > 1e-12)
                ? (mean - RISK_FREE_RATE / (252.0 * 24.0 * 60.0)) / std_dev * ANNUALIZATION
                : 0.0;

            // Sortino (only downside deviation)
            double neg_var = sum_neg_sq_returns_ / n;
            double downside_dev = std::sqrt(std::max(neg_var, 1e-20));
            snapshot_.sortino_ratio = (downside_dev > 1e-12)
                ? (mean - RISK_FREE_RATE / (252.0 * 24.0 * 60.0)) / downside_dev * ANNUALIZATION
                : 0.0;
        }

        // Calmar
        if (snapshot_.max_drawdown_pct > 1e-12 && return_count_ >= 10) {
            double n = static_cast<double>(return_count_);
            double mean = sum_returns_ / n;
            double annualized_ret = mean * 252.0 * 24.0 * 60.0;
            snapshot_.calmar_ratio = annualized_ret / snapshot_.max_drawdown_pct;
        }
    }

    // Convenience: update from portfolio state (call periodically)
    void tick(double equity, double prev_equity) noexcept {
        update_equity(equity);
        if (std::abs(prev_equity) > 1e-12) {
            double ret = (equity - prev_equity) / std::abs(prev_equity);
            record_return(ret);
        }

        // Daily/hourly PnL
        uint64_t now = Clock::now_ns();
        double elapsed_h = static_cast<double>(now - session_start_ns_) / 3.6e12;
        if (elapsed_h > 0.001) {
            snapshot_.hourly_pnl = snapshot_.total_pnl / elapsed_h;
            snapshot_.daily_pnl = snapshot_.hourly_pnl * 24.0;
        }
    }

    const StrategyMetricsSnapshot& snapshot() const noexcept { return snapshot_; }

private:
    StrategyMetricsSnapshot snapshot_;
    double peak_equity_ = 0.0;
    double total_equity_ = 0.0;
    double gross_profit_ = 0.0;
    double gross_loss_ = 0.0;

    // Rolling returns for Sharpe/Sortino
    std::array<double, RETURN_WINDOW> returns_{};
    double sum_returns_ = 0.0;
    double sum_sq_returns_ = 0.0;
    double sum_neg_sq_returns_ = 0.0;
    size_t return_count_ = 0;
    size_t return_head_ = 0;

    int current_streak_ = 0;
    uint64_t session_start_ns_ = 0;
    double last_equity_snapshot_ = 0.0;
    uint64_t last_snapshot_ns_ = 0;
};

} // namespace bybit
