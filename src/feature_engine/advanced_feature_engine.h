#pragma once

#include "../config/types.h"
#include "../orderbook/orderbook.h"
#include "../trade_flow/trade_flow_engine.h"
#include "../utils/clock.h"
#include "../utils/tsc_clock.h"
#include "simd_indicators.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace bybit {

// ─── Temporal Ring Buffer ───────────────────────────────────────────────────
// Stores last N feature snapshots for derivative computation and ML sequence input

class FeatureRingBuffer {
public:
    static constexpr size_t CAPACITY = FEATURE_SEQ_LEN + 16; // extra headroom

    void push(const Features& f) noexcept {
        buffer_[head_ % CAPACITY] = f;
        ++head_;
        if (count_ < CAPACITY) ++count_;
    }

    // Get the i-th most recent feature (0 = latest)
    const Features& get(size_t ago) const noexcept {
        if (ago >= count_) ago = count_ > 0 ? count_ - 1 : 0;
        return buffer_[(head_ - 1 - ago) % CAPACITY];
    }

    const Features& latest() const noexcept { return get(0); }

    // Fill output array with last seq_len features, oldest first
    // Returns actual count written
    size_t fill_sequence(double* out, size_t seq_len) const noexcept {
        size_t actual = std::min(seq_len, count_);
        for (size_t i = 0; i < actual; ++i) {
            const Features& f = get(actual - 1 - i); // oldest first
            const double* arr = f.as_array();
            for (size_t j = 0; j < FEATURE_COUNT; ++j) {
                out[i * FEATURE_COUNT + j] = arr[j];
            }
        }
        // Zero-pad if not enough history
        for (size_t i = actual; i < seq_len; ++i) {
            for (size_t j = 0; j < FEATURE_COUNT; ++j) {
                out[i * FEATURE_COUNT + j] = 0.0;
            }
        }
        return actual;
    }

    size_t size() const noexcept { return count_; }

private:
    std::array<Features, CAPACITY> buffer_{};
    size_t head_  = 0;
    size_t count_ = 0;
};

// ─── Advanced Feature Engine ────────────────────────────────────────────────
// Computes 25 features from order book and trade flow data.
// Maintains temporal ring buffer for derivative computation and ML input.

class AdvancedFeatureEngine {
public:
    AdvancedFeatureEngine() noexcept { reset(); }

    void reset() noexcept {
        prev_mid_ = 0.0;
        prev_microprice_ = 0.0;
        prev_imbalance_ = 0.0;
        prev_d_imbalance_ = 0.0;
        prev_volatility_ = 0.0;
        prev_momentum_ = 0.0;
        prev_velocity_ = 0.0;
        ewma_var_ = 0.0;
        tick_count_ = 0;
    }

    // Compute all 25 features from current state.
    // Call every FEATURE_TICK_MS.
    Features compute(const OrderBook& ob, const TradeFlowEngine& tf) noexcept {
        Features f;
        f.timestamp_ns = TscClock::now_ns();

        if (__builtin_expect(!ob.valid(), 0)) {
            history_.push(f);
            return f;
        }

        // Prefetch OB data for upcoming depth computation
        BYBIT_PREFETCH_R(&ob);

        double mid = ob.mid_price();
        double mp  = ob.microprice();
        double spread = ob.spread();

        // ── Order book features (7) ──────────────────────────────────────

        f.imbalance_1 = ob.imbalance(1);
        f.imbalance_5 = ob.imbalance(5);
        f.imbalance_20 = ob.imbalance(20);
        f.ob_slope = ob.liquidity_slope(20);
        f.cancel_spike = ob.cancel_spike();

        // Depth concentration: what fraction of total qty is in top 5 levels
        {
            double top5_bid = 0.0, top5_ask = 0.0;
            double total_bid = 0.0, total_ask = 0.0;
            size_t bd = ob.bid_count();
            size_t ad = ob.ask_count();
            const PriceLevel* bids = ob.bids();
            const PriceLevel* asks = ob.asks();

            for (size_t i = 0; i < bd; ++i) {
                total_bid += bids[i].qty;
                if (i < 5) top5_bid += bids[i].qty;
            }
            for (size_t i = 0; i < ad; ++i) {
                total_ask += asks[i].qty;
                if (i < 5) top5_ask += asks[i].qty;
            }

            double total = total_bid + total_ask;
            double top5 = top5_bid + top5_ask;
            f.depth_concentration = (total > 1e-12) ? top5 / total : 0.0;

            // Normalized depth totals for derived features
            double norm = std::max(total_bid, total_ask);
            f.bid_depth_total = (norm > 1e-12) ? total_bid / norm : 0.0;
            f.ask_depth_total = (norm > 1e-12) ? total_ask / norm : 0.0;

            // Liquidity wall: max single-level qty / avg qty
            double max_qty = 0.0;
            for (size_t i = 0; i < std::min(bd, size_t(50)); ++i)
                max_qty = std::max(max_qty, bids[i].qty);
            for (size_t i = 0; i < std::min(ad, size_t(50)); ++i)
                max_qty = std::max(max_qty, asks[i].qty);

            size_t total_levels = std::min(bd, size_t(50)) + std::min(ad, size_t(50));
            double avg_qty = (total_levels > 0) ? total / total_levels : 1.0;
            f.liquidity_wall = (avg_qty > 1e-12) ? max_qty / avg_qty : 0.0;
        }

        // ── Trade flow features (5) ──────────────────────────────────────

        f.aggression_ratio = tf.aggression_ratio(TradeFlowEngine::WINDOW_500MS);
        f.volume_accel = tf.volume_acceleration();
        f.trade_velocity = tf.trade_velocity();

        // Average trade size
        {
            auto stats = tf.compute();
            double total_vol = stats.w500ms.buy_volume + stats.w500ms.sell_volume;
            uint32_t count = stats.w500ms.trade_count;
            f.avg_trade_size = (count > 0) ? total_vol / count : 0.0;
        }

        // Trade acceleration: d(velocity)/dt
        f.trade_acceleration = (tick_count_ > 0) ? (f.trade_velocity - prev_velocity_) : 0.0;
        prev_velocity_ = f.trade_velocity;

        // ── Price features (5) ───────────────────────────────────────────

        f.microprice = mp;
        f.spread_bps = (mid > 1e-12) ? (spread / mid) * 10000.0 : 0.0;
        f.spread_change_rate = ob.spread_change_rate();

        // Mid price momentum (return over ~500ms = 50 ticks at 10ms)
        if (tick_count_ > 0 && std::abs(prev_mid_) > 1e-12) {
            f.mid_momentum = (mid - prev_mid_) / prev_mid_;
        }

        // EWMA volatility
        if (tick_count_ > 0 && std::abs(prev_mid_) > 1e-12) {
            double ret = (mid - prev_mid_) / prev_mid_;
            ewma_var_ = EWMA_ALPHA * (ret * ret) + (1.0 - EWMA_ALPHA) * ewma_var_;
            f.volatility = std::sqrt(ewma_var_);
        }

        // ── Derived features (4) ─────────────────────────────────────────

        if (std::abs(mid) > 1e-12) {
            f.microprice_dev = (mp - mid) / mid;
        }

        if (tick_count_ > 0 && std::abs(prev_microprice_) > 1e-12) {
            f.short_term_pressure = (mp - prev_microprice_) / prev_microprice_;
        }

        // ── Temporal derivatives (4) ─────────────────────────────────────

        double cur_imb = f.imbalance_5;
        f.d_imbalance_dt = cur_imb - prev_imbalance_;
        f.d2_imbalance_dt2 = f.d_imbalance_dt - prev_d_imbalance_;
        f.d_volatility_dt = f.volatility - prev_volatility_;
        f.d_momentum_dt = f.mid_momentum - prev_momentum_;

        // ── NaN sanitization pass (branchless) ──────────────────────────────
        {
            double* arr = f.as_mutable_array();
            for (size_t i = 0; i < FEATURE_COUNT; ++i) {
                // Branchless: multiply by isfinite mask (0.0 or 1.0)
                arr[i] = std::isfinite(arr[i]) ? arr[i] : 0.0;
            }
        }

        // ── Update state ─────────────────────────────────────────────────

        prev_d_imbalance_ = f.d_imbalance_dt;
        prev_imbalance_ = cur_imb;
        prev_volatility_ = f.volatility;
        prev_momentum_ = f.mid_momentum;
        prev_microprice_ = mp;
        prev_mid_ = mid;
        ++tick_count_;

        // Store in history
        history_.push(f);
        last_features_ = f;

        return f;
    }

    const Features& last() const noexcept { return last_features_; }
    const FeatureRingBuffer& history() const noexcept { return history_; }
    FeatureRingBuffer& history() noexcept { return history_; }
    uint64_t tick_count() const noexcept { return tick_count_; }

private:
    static constexpr double EWMA_ALPHA = 0.06;

    Features last_features_{};
    FeatureRingBuffer history_;

    double prev_mid_ = 0.0;
    double prev_microprice_ = 0.0;
    double prev_imbalance_ = 0.0;
    double prev_d_imbalance_ = 0.0;
    double prev_volatility_ = 0.0;
    double prev_momentum_ = 0.0;
    double prev_velocity_ = 0.0;
    double ewma_var_ = 0.0;
    uint64_t tick_count_ = 0;
};

} // namespace bybit
