#pragma once

#include "../config/types.h"
#include "../utils/clock.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace bybit {

// ─── Adaptive Signal Threshold ──────────────────────────────────────────────
// Dynamically adjusts the signal threshold based on:
//   1. Current volatility
//   2. Recent model accuracy
//   3. Market liquidity
//   4. Current spread
// Higher threshold = fewer but higher quality signals.

class AdaptiveThreshold {
public:
    explicit AdaptiveThreshold(double base = 0.6, double min_t = 0.45, double max_t = 0.85) noexcept
        : base_threshold_(base), min_threshold_(min_t), max_threshold_(max_t) {
        state_.base_threshold = base;
        state_.current_threshold = base;
    }

    // Update threshold based on current market conditions.
    // Call every feature tick.
    double update(const Features& f, const RegimeState& regime) noexcept {
        // ── 1. Volatility adjustment ─────────────────────────────────────
        // High volatility -> raise threshold (be more selective)
        double vol = f.volatility;
        double vol_norm = std::clamp(vol / VOL_BASELINE, 0.0, 5.0);
        state_.volatility_adj = (vol_norm - 1.0) * VOL_WEIGHT;

        // ── 2. Accuracy adjustment ───────────────────────────────────────
        // High recent accuracy -> can lower threshold (model is working)
        // Low accuracy -> raise threshold
        double acc = state_.recent_accuracy;
        state_.accuracy_adj = (0.5 - acc) * ACCURACY_WEIGHT; // negative when accurate

        // ── 3. Liquidity adjustment ──────────────────────────────────────
        // Low liquidity -> raise threshold (harder to execute)
        double liq = (f.bid_depth_total + f.ask_depth_total) * 0.5;
        double liq_norm = std::clamp(liq, 0.1, 2.0);
        state_.liquidity_adj = (1.0 - liq_norm) * LIQ_WEIGHT;

        // ── 4. Spread adjustment ─────────────────────────────────────────
        // Wide spread -> raise threshold (higher cost of execution)
        double spread_norm = std::clamp(f.spread_bps / SPREAD_BASELINE, 0.0, 5.0);
        state_.spread_adj = (spread_norm - 1.0) * SPREAD_WEIGHT;

        // ── 5. Regime overlay ────────────────────────────────────────────
        double regime_adj = regime.params[static_cast<size_t>(regime.current)].signal_threshold
                           - base_threshold_;

        // ── Combine adjustments ──────────────────────────────────────────
        double adjusted = base_threshold_
                        + state_.volatility_adj
                        + state_.accuracy_adj
                        + state_.liquidity_adj
                        + state_.spread_adj
                        + regime_adj * 0.5; // partial regime influence

        state_.current_threshold = std::clamp(adjusted, min_threshold_, max_threshold_);
        return state_.current_threshold;
    }

    // Record signal outcome for accuracy tracking
    void record_outcome(bool was_correct) noexcept {
        if (was_correct) ++state_.correct_count;
        ++state_.total_count;

        // Sliding window accuracy
        outcomes_[outcome_head_ % OUTCOME_WINDOW] = was_correct ? 1.0 : 0.0;
        ++outcome_head_;

        size_t window = std::min(static_cast<size_t>(state_.total_count),
                                  static_cast<size_t>(OUTCOME_WINDOW));
        double sum = 0.0;
        for (size_t i = 0; i < window; ++i) {
            sum += outcomes_[(outcome_head_ - 1 - i) % OUTCOME_WINDOW];
        }
        state_.recent_accuracy = (window > 0) ? sum / window : 0.5;
    }

    double current() const noexcept { return state_.current_threshold; }
    const AdaptiveThresholdState& state() const noexcept { return state_; }

private:
    static constexpr double VOL_BASELINE    = 0.0003;
    static constexpr double SPREAD_BASELINE = 1.5;  // bps
    static constexpr double VOL_WEIGHT      = 0.08;
    static constexpr double ACCURACY_WEIGHT = 0.15;
    static constexpr double LIQ_WEIGHT      = 0.05;
    static constexpr double SPREAD_WEIGHT   = 0.04;
    static constexpr size_t OUTCOME_WINDOW  = 100;

    double base_threshold_;
    double min_threshold_;
    double max_threshold_;

    AdaptiveThresholdState state_;
    std::array<double, OUTCOME_WINDOW> outcomes_{};
    size_t outcome_head_ = 0;
};

} // namespace bybit
