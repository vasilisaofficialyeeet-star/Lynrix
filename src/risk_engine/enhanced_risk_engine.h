#pragma once

#include "../config/types.h"
#include "../core/system_control.h"
#include "../utils/clock.h"
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace bybit {

// ─── Circuit Breaker ────────────────────────────────────────────────────────
// Automatically halts trading when losses exceed safety thresholds.
// Implements multiple trip conditions with cooldown period.

class CircuitBreaker {
public:
    explicit CircuitBreaker(const CircuitBreakerConfig& cfg) noexcept : cfg_(cfg) {
        reset();
    }

    CircuitBreaker() noexcept { reset(); }

    void set_config(const CircuitBreakerConfig& cfg) noexcept { cfg_ = cfg; }

    void reset() noexcept {
        tripped_ = false;
        trip_time_ns_ = 0;
        cooldown_expires_ns_ = 0;
        consecutive_losses_ = 0;
        loss_history_.fill(0.0);
        loss_head_ = 0;
        loss_count_ = 0;
        loss_tail_ = 0;
        running_loss_sum_ = 0.0;
        trip_pending_log_ = false;
        peak_pnl_ = 0.0;
        trip_reason_ = nullptr;
    }

    // Check if circuit breaker is tripped. Returns true if trading should stop.
    // M3: Prefer the overload that accepts tick-level timestamp to avoid syscall.
    bool is_tripped() const noexcept {
        return is_tripped(Clock::now_ns());
    }

    // M3: Hot-path variant — caller supplies current timestamp (no Clock::now_ns() call)
    bool is_tripped(uint64_t now_ns) const noexcept {
        if (!tripped_) return false;
        if (!cfg_.enabled) return false;
        return now_ns < cooldown_expires_ns_;
    }

    // Update with new PnL data. Call after every trade or periodically.
    void update(double total_pnl, double realized_pnl_delta) noexcept {
        if (!cfg_.enabled) return;

        // Track peak PnL for drawdown calculation
        if (total_pnl > peak_pnl_) {
            peak_pnl_ = total_pnl;
        }

        // ── Check 1: Absolute loss threshold ─────────────────────────────
        if (total_pnl < -cfg_.loss_threshold) {
            trip("absolute_loss_exceeded");
            return;
        }

        // ── Check 2: Drawdown threshold ──────────────────────────────────
        double drawdown = (peak_pnl_ > 0.0) ? (peak_pnl_ - total_pnl) / peak_pnl_ : 0.0;
        if (drawdown > cfg_.drawdown_threshold) {
            trip("drawdown_exceeded");
            return;
        }

        // ── Check 3: Consecutive losses ──────────────────────────────────
        if (std::abs(realized_pnl_delta) > 1e-12) {
            if (realized_pnl_delta < 0.0) {
                ++consecutive_losses_;
                if (consecutive_losses_ >= cfg_.consecutive_losses) {
                    trip("consecutive_losses_exceeded");
                    return;
                }
            } else {
                consecutive_losses_ = 0;
            }
        }

        // ── Check 4: Loss rate ($/minute) ────────────────────────────────
        // Store recent losses with timestamps
        if (realized_pnl_delta < 0.0) {
            loss_history_[loss_head_ % LOSS_WINDOW] = realized_pnl_delta;
            loss_timestamps_[loss_head_ % LOSS_WINDOW] = Clock::now_ns();
            ++loss_head_;
            if (loss_count_ < LOSS_WINDOW) ++loss_count_;

            // M5: O(1) loss rate — running sum with lazy expiry of old entries
            uint64_t now = Clock::now_ns();
            uint64_t one_min_ago = now - 60'000'000'000ULL;
            // Expire old entries from tail
            while (loss_tail_ < loss_head_) {
                size_t tail_idx = loss_tail_ % LOSS_WINDOW;
                if (loss_timestamps_[tail_idx] > one_min_ago) break;
                running_loss_sum_ -= loss_history_[tail_idx];
                ++loss_tail_;
            }
            running_loss_sum_ += realized_pnl_delta; // add new (negative) entry
            if (std::abs(running_loss_sum_) > cfg_.max_loss_rate) {
                trip("loss_rate_exceeded");
                return;
            }
        }
    }

    // Manually reset the circuit breaker (e.g., from UI)
    void manual_reset() noexcept {
        tripped_ = false;
        trip_time_ns_ = 0;
        cooldown_expires_ns_ = 0;
        consecutive_losses_ = 0;
        trip_reason_ = nullptr;
    }

    bool tripped() const noexcept { return tripped_; }
    const char* trip_reason() const noexcept { return trip_reason_; }
    int consecutive_losses() const noexcept { return consecutive_losses_; }
    double peak_pnl() const noexcept { return peak_pnl_; }

private:
    static constexpr size_t LOSS_WINDOW = 64;

    // C4: No synchronous spdlog on risk hot path.
    // Sets flag + reason; cold-path drain_cold_work() emits the log.
    void trip(const char* reason) noexcept {
        if (tripped_) return;
        tripped_ = true;
        trip_time_ns_ = Clock::now_ns();
        cooldown_expires_ns_ = trip_time_ns_ +
            static_cast<uint64_t>(cfg_.cooldown_sec) * 1'000'000'000ULL;
        trip_reason_ = reason;
        trip_pending_log_ = true;  // C4: deferred — checked by cold path
    }

    CircuitBreakerConfig cfg_;
    bool tripped_ = false;
    uint64_t trip_time_ns_ = 0;
    uint64_t cooldown_expires_ns_ = 0;
    int consecutive_losses_ = 0;
    double peak_pnl_ = 0.0;
    const char* trip_reason_ = nullptr;

    std::array<double, LOSS_WINDOW> loss_history_{};
    std::array<uint64_t, LOSS_WINDOW> loss_timestamps_{};
    size_t loss_head_ = 0;
    size_t loss_count_ = 0;
    size_t loss_tail_ = 0;            // M5: tail pointer for O(1) sliding window
    double running_loss_sum_ = 0.0;   // M5: running sum of losses in window
    bool   trip_pending_log_ = false; // C4: deferred log flag

public:
    // C4: Cold-path log drain — call from Application::drain_cold_work()
    bool drain_trip_log(const char*& out_reason) noexcept {
        if (!trip_pending_log_) return false;
        trip_pending_log_ = false;
        out_reason = trip_reason_;
        return true;
    }
};

// ─── Enhanced Risk Engine ───────────────────────────────────────────────────
// Extends the base risk engine with circuit breaker integration,
// regime-aware position limits, and enhanced monitoring.

class EnhancedRiskEngine {
public:
    explicit EnhancedRiskEngine(const RiskLimits& limits, const CircuitBreakerConfig& cb_cfg) noexcept
        : limits_(limits), circuit_breaker_(cb_cfg) {
        reset();
    }

    EnhancedRiskEngine() noexcept { reset(); }

    void set_limits(const RiskLimits& limits) noexcept { limits_ = limits; }
    void set_circuit_breaker_config(const CircuitBreakerConfig& cfg) noexcept {
        circuit_breaker_.set_config(cfg);
    }

    void reset() noexcept {
        daily_pnl_ = 0.0;
        peak_equity_ = 0.0;
        current_equity_ = 0.0;
        order_timestamps_.fill(0);
        order_ts_head_ = 0;
        circuit_breaker_.reset();
    }

    struct RiskCheck {
        bool passed = false;
        const char* reason = nullptr;
    };

    // Pre-order risk check with regime awareness. Must be < 10µs.
    RiskCheck check_order(const Signal& signal, const Position& position,
                          MarketRegime regime = MarketRegime::LowVolatility) noexcept {
        // Check circuit breaker first
        if (circuit_breaker_.is_tripped()) {
            return {false, "circuit_breaker_tripped"};
        }

        // Liquidity vacuum: block all new orders
        if (regime == MarketRegime::LiquidityVacuum) {
            return {false, "liquidity_vacuum_regime"};
        }

        // Regime-adjusted position limit (branchless via LUT)
        double max_pos = limits_.max_position_size.raw() *
                         regime_position_scale(regime);

        // Check max position size
        double new_size = std::abs(position.size.raw());
        bool same_side = (signal.side == Side::Buy && position.size.raw() >= 0.0) ||
                         (signal.side == Side::Sell && position.size.raw() <= 0.0);
        if (same_side) {
            new_size += signal.qty.raw();
        }

        if (new_size > max_pos && max_pos > 0.0) {
            return {false, "max_position_size_exceeded"};
        }

        // Check max daily loss
        if (daily_pnl_ < -limits_.max_daily_loss.raw() && limits_.max_daily_loss.raw() > 0.0) {
            return {false, "max_daily_loss_exceeded"};
        }

        // Check max drawdown
        double dd = peak_equity_ > 0.0 ? (peak_equity_ - current_equity_) / peak_equity_ : 0.0;
        if (dd > limits_.max_drawdown && limits_.max_drawdown > 0.0) {
            return {false, "max_drawdown_exceeded"};
        }

        // Check order rate
        if (limits_.max_orders_per_sec > 0) {
            uint64_t now = Clock::now_ns();
            uint64_t one_sec_ago = now - 1'000'000'000ULL;
            int recent_orders = 0;
            for (size_t i = 0; i < MAX_RATE_WINDOW; ++i) {
                if (order_timestamps_[i] > one_sec_ago) {
                    ++recent_orders;
                }
            }
            if (recent_orders >= limits_.max_orders_per_sec) {
                return {false, "max_orders_per_sec_exceeded"};
            }
        }

        return {true, nullptr};
    }

    // Backward-compatible: 2-arg version delegates to 3-arg
    // (removed separate overload to avoid ambiguity with default param)

    void record_order() noexcept {
        order_timestamps_[order_ts_head_ % MAX_RATE_WINDOW] = Clock::now_ns();
        ++order_ts_head_;
    }

    void update_pnl(Notional realized_pnl, Notional equity) noexcept {
        double delta = realized_pnl.raw() - daily_pnl_;
        daily_pnl_ = realized_pnl.raw();
        current_equity_ = equity.raw();
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }

        // Update circuit breaker with delta
        circuit_breaker_.update(realized_pnl.raw(), delta);
    }

    void record_trade_pnl(Notional pnl) noexcept {
        circuit_breaker_.update(daily_pnl_ + pnl.raw(), pnl.raw());
    }

    // Legacy compat for raw callers
    void update_pnl_raw(double realized_pnl, double equity) noexcept {
        update_pnl(Notional(realized_pnl), Notional(equity));
    }

    void reset_daily() noexcept {
        daily_pnl_ = 0.0;
    }

    double daily_pnl() const noexcept { return daily_pnl_; }
    double drawdown() const noexcept {
        return peak_equity_ > 0.0 ? (peak_equity_ - current_equity_) / peak_equity_ : 0.0;
    }
    const RiskLimits& limits() const noexcept { return limits_; }
    CircuitBreaker& circuit_breaker() noexcept { return circuit_breaker_; }
    const CircuitBreaker& circuit_breaker() const noexcept { return circuit_breaker_; }

    // ── Stage 6: Formal risk-state evaluation ─────────────────────────────
    // Maps current risk conditions to RiskEvent and applies to ControlPlane.
    // Call per tick (or after PnL update) from Application.
    // Returns true if any risk state transition occurred.
    bool evaluate_risk_state(ControlPlane& cp, uint64_t tick_id) noexcept {
        bool changed = false;

        // Circuit breaker trip → immediate escalation
        if (circuit_breaker_.is_tripped()) {
            const char* reason = circuit_breaker_.trip_reason();
            if (!reason) reason = "circuit_breaker_tripped";

            // Map CB trip reason to specific RiskEvent
            RiskEvent ev = RiskEvent::LossRateExceeded; // default
            if (reason[0] == 'a') ev = RiskEvent::DrawdownBreached;      // absolute_loss
            else if (reason[0] == 'd') ev = RiskEvent::DrawdownBreached;  // drawdown
            else if (reason[0] == 'c') ev = RiskEvent::ConsecutiveLosses; // consecutive
            else if (reason[0] == 'l') ev = RiskEvent::LossRateExceeded;  // loss_rate

            changed |= cp.risk_event(ev, tick_id, reason);
            return changed;
        }

        // Circuit breaker cooldown expired → deescalate
        if (circuit_breaker_.tripped() && !circuit_breaker_.is_tripped()) {
            changed |= cp.risk_event(RiskEvent::CooldownExpired, tick_id,
                                     "cb_cooldown_expired");
        }

        // Drawdown warning (approaching threshold but not exceeded)
        double dd = drawdown();
        if (dd > limits_.max_drawdown * 0.7 && dd <= limits_.max_drawdown) {
            changed |= cp.risk_event(RiskEvent::DrawdownWarning, tick_id,
                                     "drawdown_approaching_threshold");
        }

        // Daily loss warning
        if (daily_pnl_ < -limits_.max_daily_loss.raw() * 0.7 &&
            daily_pnl_ >= -limits_.max_daily_loss.raw()) {
            changed |= cp.risk_event(RiskEvent::DrawdownWarning, tick_id,
                                     "daily_loss_approaching_threshold");
        }

        // All clear — PnL normal, can deescalate
        if (!circuit_breaker_.tripped() && dd < limits_.max_drawdown * 0.5 &&
            daily_pnl_ > -limits_.max_daily_loss.raw() * 0.5) {
            changed |= cp.risk_event(RiskEvent::PnlNormal, tick_id, "pnl_within_limits");
        }

        return changed;
    }

    // Stage 6: Enhanced check_order that also consults the FSM position scale
    RiskCheck check_order_controlled(const Signal& signal, const Position& position,
                                     MarketRegime regime,
                                     const RiskControlFSM& risk_fsm) noexcept {
        // FSM gate: check if new orders are allowed
        if (!risk_fsm.allows_new_orders()) {
            return {false, "risk_fsm_blocks_new_orders"};
        }

        // FSM gate: check if position increase is allowed
        bool same_side = (signal.side == Side::Buy && position.size.raw() >= 0.0) ||
                         (signal.side == Side::Sell && position.size.raw() <= 0.0);
        if (same_side && !risk_fsm.allows_increase()) {
            return {false, "risk_fsm_reduce_only"};
        }

        // Delegate to existing check_order (includes CB, regime, limits)
        return check_order(signal, position, regime);
    }

private:
    static constexpr size_t MAX_RATE_WINDOW = 64;

    RiskLimits limits_{};
    CircuitBreaker circuit_breaker_;
    double daily_pnl_ = 0.0;
    double peak_equity_ = 0.0;
    double current_equity_ = 0.0;
    std::array<uint64_t, MAX_RATE_WINDOW> order_timestamps_{};
    size_t order_ts_head_ = 0;
};

} // namespace bybit
