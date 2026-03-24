#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace bybit {

// ─── Market Regime Detector ─────────────────────────────────────────────────
// Online regime detection using rolling statistics and k-means clustering.
// Classifies market into 5 regimes:
//   0: LowVolatility   - tight spread, low vol, good for market making
//   1: HighVolatility   - wide spread, high vol, reduce size
//   2: Trending         - sustained directional moves
//   3: MeanReverting    - oscillating around a mean
//   4: LiquidityVacuum  - thin book, wide spread, dangerous

class RegimeDetector {
public:
    RegimeDetector() noexcept {
        reset();
        init_default_regime_params();
    }

    void reset() noexcept {
        vol_ewma_ = 0.0;
        momentum_ewma_ = 0.0;
        spread_ewma_ = 0.0;
        depth_ewma_ = 1.0;
        autocorr_sum_ = 0.0;
        returns_.fill(0.0);
        return_head_ = 0;
        return_count_ = 0;
        tick_count_ = 0;
        state_.current = MarketRegime::LowVolatility;
        state_.previous = MarketRegime::LowVolatility;
        state_.regime_start_ns = Clock::now_ns();
    }

    // Update regime detection with new feature vector.
    // Call every feature tick.
    RegimeState update(const Features& f) noexcept {
        ++tick_count_;
        if (tick_count_ < MIN_WARMUP_TICKS) {
            state_.timestamp_ns = f.timestamp_ns;
            return state_;
        }

        // ── Compute regime indicators ────────────────────────────────────

        // 1. Volatility (EWMA of absolute returns)
        double vol = f.volatility;
        vol_ewma_ = ALPHA * vol + (1.0 - ALPHA) * vol_ewma_;

        // 2. Momentum (EWMA of signed returns)
        double mom = f.mid_momentum;
        momentum_ewma_ = ALPHA * mom + (1.0 - ALPHA) * momentum_ewma_;

        // 3. Spread (EWMA)
        spread_ewma_ = ALPHA * f.spread_bps + (1.0 - ALPHA) * spread_ewma_;

        // 4. Depth / liquidity
        double depth = (f.bid_depth_total + f.ask_depth_total) * 0.5;
        depth_ewma_ = ALPHA * depth + (1.0 - ALPHA) * depth_ewma_;

        // 5. Autocorrelation of returns (mean-reversion indicator)
        double ret = f.mid_momentum;
        size_t idx = return_head_ % AUTOCORR_WINDOW;
        if (return_count_ >= 2) {
            size_t prev_idx = (return_head_ - 1) % AUTOCORR_WINDOW;
            autocorr_sum_ = ALPHA * (ret * returns_[prev_idx]) + (1.0 - ALPHA) * autocorr_sum_;
        }
        returns_[idx] = ret;
        ++return_head_;
        if (return_count_ < AUTOCORR_WINDOW) ++return_count_;

        // ── Classify regime ──────────────────────────────────────────────

        state_.volatility = vol_ewma_;
        state_.trend_score = std::abs(momentum_ewma_);
        state_.mr_score = -autocorr_sum_; // negative autocorr = mean reverting
        state_.liq_score = depth_ewma_;

        MarketRegime detected = classify();

        // Regime change hysteresis: require persistence for N ticks
        if (detected != state_.current) {
            if (detected == pending_regime_) {
                ++pending_count_;
                if (pending_count_ >= REGIME_PERSISTENCE) {
                    state_.previous = state_.current;
                    state_.current = detected;
                    state_.regime_start_ns = f.timestamp_ns;
                    pending_count_ = 0;
                }
            } else {
                pending_regime_ = detected;
                pending_count_ = 1;
            }
        } else {
            pending_count_ = 0;
        }

        // Compute confidence: how clearly separated from other regimes
        state_.confidence = compute_confidence(detected);
        state_.timestamp_ns = f.timestamp_ns;

        return state_;
    }

    const RegimeState& state() const noexcept { return state_; }

    // Get regime-specific parameters
    const RegimeState::RegimeParams& current_params() const noexcept {
        return state_.params[static_cast<size_t>(state_.current)];
    }

    static const char* regime_name(MarketRegime r) noexcept {
        switch (r) {
            case MarketRegime::LowVolatility:  return "LOW_VOL";
            case MarketRegime::HighVolatility:  return "HIGH_VOL";
            case MarketRegime::Trending:        return "TRENDING";
            case MarketRegime::MeanReverting:   return "MEAN_REV";
            case MarketRegime::LiquidityVacuum: return "LIQ_VACUUM";
        }
        return "UNKNOWN";
    }

private:
    static constexpr double ALPHA = 0.02;            // EWMA decay
    static constexpr size_t MIN_WARMUP_TICKS = 50;
    static constexpr size_t AUTOCORR_WINDOW = 64;
    static constexpr int    REGIME_PERSISTENCE = 10;  // ticks to confirm regime change

    // Thresholds for classification
    static constexpr double VOL_HIGH_THRESHOLD    = 0.0005;  // annualized vol proxy
    static constexpr double VOL_LOW_THRESHOLD     = 0.0001;
    static constexpr double TREND_THRESHOLD       = 0.0002;
    static constexpr double MR_THRESHOLD          = 0.00005;
    static constexpr double LIQ_VACUUM_THRESHOLD  = 0.3;     // depth ratio
    static constexpr double SPREAD_WIDE_THRESHOLD = 5.0;     // bps

    MarketRegime classify() const noexcept {
        // Priority 1: Liquidity vacuum (dangerous, detect first)
        if (depth_ewma_ < LIQ_VACUUM_THRESHOLD || spread_ewma_ > SPREAD_WIDE_THRESHOLD * 2) {
            return MarketRegime::LiquidityVacuum;
        }

        // Priority 2: High volatility
        if (vol_ewma_ > VOL_HIGH_THRESHOLD) {
            // Sub-classify: trending vs choppy high vol
            if (state_.trend_score > TREND_THRESHOLD) {
                return MarketRegime::Trending;
            }
            return MarketRegime::HighVolatility;
        }

        // Priority 3: Trending (can happen in low vol too)
        if (state_.trend_score > TREND_THRESHOLD * 0.7) {
            return MarketRegime::Trending;
        }

        // Priority 4: Mean reverting
        if (state_.mr_score > MR_THRESHOLD && vol_ewma_ < VOL_HIGH_THRESHOLD) {
            return MarketRegime::MeanReverting;
        }

        // Default: Low volatility
        return MarketRegime::LowVolatility;
    }

    double compute_confidence(MarketRegime r) const noexcept {
        // Distance from decision boundary normalized to [0,1]
        double conf = 0.5;
        switch (r) {
            case MarketRegime::LowVolatility:
                conf = std::clamp(1.0 - vol_ewma_ / VOL_LOW_THRESHOLD, 0.0, 1.0);
                break;
            case MarketRegime::HighVolatility:
                conf = std::clamp(vol_ewma_ / VOL_HIGH_THRESHOLD - 1.0, 0.0, 1.0) * 0.5 + 0.5;
                break;
            case MarketRegime::Trending:
                conf = std::clamp(state_.trend_score / TREND_THRESHOLD - 1.0, 0.0, 1.0) * 0.5 + 0.5;
                break;
            case MarketRegime::MeanReverting:
                conf = std::clamp(state_.mr_score / MR_THRESHOLD - 1.0, 0.0, 1.0) * 0.5 + 0.5;
                break;
            case MarketRegime::LiquidityVacuum:
                conf = std::clamp(1.0 - depth_ewma_ / LIQ_VACUUM_THRESHOLD, 0.0, 1.0) * 0.5 + 0.5;
                break;
        }
        return conf;
    }

    void init_default_regime_params() noexcept {
        // LowVolatility: aggressive, tight entry
        state_.params[0] = {0.55, 1.2, 0.5, 200.0};
        // HighVolatility: conservative, wide entry
        state_.params[1] = {0.75, 0.5, 3.0, 150.0};
        // Trending: momentum-following
        state_.params[2] = {0.60, 1.0, 1.5, 500.0};
        // MeanReverting: fade moves
        state_.params[3] = {0.50, 1.3, 0.5, 200.0};
        // LiquidityVacuum: very conservative or stop
        state_.params[4] = {0.90, 0.1, 5.0, 50.0};
    }

    RegimeState state_;
    double vol_ewma_ = 0.0;
    double momentum_ewma_ = 0.0;
    double spread_ewma_ = 0.0;
    double depth_ewma_ = 1.0;
    double autocorr_sum_ = 0.0;

    std::array<double, AUTOCORR_WINDOW> returns_{};
    size_t return_head_ = 0;
    size_t return_count_ = 0;
    uint64_t tick_count_ = 0;

    MarketRegime pending_regime_ = MarketRegime::LowVolatility;
    int pending_count_ = 0;
};

} // namespace bybit
