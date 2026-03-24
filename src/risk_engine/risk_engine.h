#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include <array>
#include <cstdint>
#include <cmath>

namespace bybit {

// Ultra-low-latency risk engine.
// All checks must complete in < 10µs.
class RiskEngine {
public:
    explicit RiskEngine(const RiskLimits& limits) noexcept : limits_(limits) {
        reset();
    }

    RiskEngine() noexcept { reset(); }

    void set_limits(const RiskLimits& limits) noexcept { limits_ = limits; }

    void reset() noexcept {
        daily_pnl_ = 0.0;
        peak_equity_ = 0.0;
        current_equity_ = 0.0;
        order_timestamps_.fill(0);
        order_ts_head_ = 0;
    }

    struct RiskCheck {
        bool passed = false;
        const char* reason = nullptr;
    };

    // Pre-order risk check. Must be < 10µs.
    RiskCheck check_order(const Signal& signal, const Position& position) noexcept {
        // Check max position size
        double new_size = std::abs(position.size.raw());
        bool same_side = (signal.side == Side::Buy && position.size.raw() >= 0.0) ||
                         (signal.side == Side::Sell && position.size.raw() <= 0.0);
        if (same_side) {
            new_size += signal.qty.raw();
        }

        if (new_size > limits_.max_position_size.raw() && limits_.max_position_size.raw() > 0.0) {
            return {false, "max_position_size exceeded"};
        }

        // Check max daily loss
        if (daily_pnl_ < -limits_.max_daily_loss.raw() && limits_.max_daily_loss.raw() > 0.0) {
            return {false, "max_daily_loss exceeded"};
        }

        // Check max drawdown
        double dd = peak_equity_ > 0.0 ? (peak_equity_ - current_equity_) / peak_equity_ : 0.0;
        if (dd > limits_.max_drawdown && limits_.max_drawdown > 0.0) {
            return {false, "max_drawdown exceeded"};
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
                return {false, "max_orders_per_sec exceeded"};
            }
        }

        return {true, nullptr};
    }

    void record_order() noexcept {
        order_timestamps_[order_ts_head_ % MAX_RATE_WINDOW] = Clock::now_ns();
        ++order_ts_head_;
    }

    void update_pnl(Notional realized_pnl, Notional equity) noexcept {
        daily_pnl_ += realized_pnl.raw();
        current_equity_ = equity.raw();
        if (current_equity_ > peak_equity_) {
            peak_equity_ = current_equity_;
        }
    }

    // Legacy compat for raw callers
    void update_pnl_raw(double realized_pnl, double equity) noexcept {
        update_pnl(Notional(realized_pnl), Notional(equity));
    }

    void reset_daily() noexcept {
        daily_pnl_ = 0.0;
    }

    double daily_pnl() const noexcept { return daily_pnl_; }
    const RiskLimits& limits() const noexcept { return limits_; }

private:
    static constexpr size_t MAX_RATE_WINDOW = 64;

    RiskLimits limits_{};
    double daily_pnl_ = 0.0;
    double peak_equity_ = 0.0;
    double current_equity_ = 0.0;
    std::array<uint64_t, MAX_RATE_WINDOW> order_timestamps_{};
    size_t order_ts_head_ = 0;
};

} // namespace bybit
