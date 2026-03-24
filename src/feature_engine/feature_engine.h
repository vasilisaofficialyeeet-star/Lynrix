#pragma once

#include "../config/types.h"
#include "../orderbook/orderbook.h"
#include "../trade_flow/trade_flow_engine.h"
#include "../utils/clock.h"
#include <cmath>
#include <cstdint>

namespace bybit {

class FeatureEngine {
public:
    FeatureEngine() noexcept { reset(); }

    void reset() noexcept {
        prev_microprice_ = 0.0;
        ewma_var_ = 0.0;
        ewma_alpha_ = 0.06; // ~50ms tick with decay
        prev_mid_ = 0.0;
        tick_count_ = 0;
    }

    // Compute all features from current orderbook and trade flow state.
    // Must be called every FEATURE_TICK_MS.
    Features compute(const OrderBook& ob, const TradeFlowEngine& tf) noexcept {
        Features f;
        f.timestamp_ns = Clock::now_ns();

        // ─── Order Book Features ────────────────────────────────────────────

        f.imbalance_5  = ob.imbalance(5);
        f.imbalance_20 = ob.imbalance(20);
        f.ob_slope = ob.liquidity_slope(20);
        f.cancel_spike = ob.cancel_spike();
        f.spread_change_rate = ob.spread_change_rate();

        // ─── Trade Flow Features ────────────────────────────────────────────

        f.aggression_ratio = tf.aggression_ratio(TradeFlowEngine::WINDOW_500MS);
        f.volume_accel = tf.volume_acceleration();
        f.trade_velocity = tf.trade_velocity();

        // ─── Derived Features ───────────────────────────────────────────────

        double mp = ob.microprice();
        double mid = ob.mid_price();

        // Microprice deviation from mid
        if (std::abs(mid) > 1e-12) {
            f.microprice_dev = (mp - mid) / mid;
        }

        // EWMA volatility (variance of mid-price returns)
        if (tick_count_ > 0 && std::abs(prev_mid_) > 1e-12) {
            double ret = (mid - prev_mid_) / prev_mid_;
            ewma_var_ = ewma_alpha_ * (ret * ret) + (1.0 - ewma_alpha_) * ewma_var_;
            f.volatility = std::sqrt(ewma_var_);
        }

        // Short-term pressure: microprice momentum
        if (tick_count_ > 0 && std::abs(prev_microprice_) > 1e-12) {
            f.short_term_pressure = (mp - prev_microprice_) / prev_microprice_;
        }

        prev_microprice_ = mp;
        prev_mid_ = mid;
        ++tick_count_;

        last_features_ = f;
        return f;
    }

    const Features& last() const noexcept { return last_features_; }

private:
    Features last_features_{};
    double prev_microprice_ = 0.0;
    double ewma_var_ = 0.0;
    double ewma_alpha_ = 0.06;
    double prev_mid_ = 0.0;
    uint64_t tick_count_ = 0;
};

} // namespace bybit
