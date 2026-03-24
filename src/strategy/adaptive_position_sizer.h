#pragma once

#include "../config/types.h"
#include <cmath>
#include <algorithm>

namespace bybit {

// ─── Adaptive Position Sizer ────────────────────────────────────────────────
// Dynamically adjusts order size based on:
//   1. Current volatility (inverse relationship)
//   2. Market liquidity (proportional)
//   3. Model confidence (proportional)
//   4. Risk limits (hard cap)
//   5. Current drawdown (reduce on drawdown)

class AdaptivePositionSizer {
public:
    explicit AdaptivePositionSizer(const AppConfig& cfg) noexcept
        : base_qty_(cfg.base_order_qty)
        , min_qty_(cfg.min_order_qty)
        , max_qty_(cfg.max_order_qty)
        , max_position_(cfg.risk.max_position_size.raw())
        , lot_size_(cfg.lot_size)
    {}

    // Compute optimal order quantity given current conditions
    Qty compute(double confidence, double volatility,
                double liquidity, double spread_bps,
                const Position& pos, MarketRegime regime) const noexcept {

        double qty = base_qty_;

        // ── 1. Confidence scaling ────────────────────────────────────────
        // Scale linearly: low confidence -> small size, high confidence -> larger
        double conf_scale = std::clamp((confidence - 0.3) / 0.5, 0.2, 2.0);
        qty *= conf_scale;

        // ── 2. Volatility scaling (inverse) ──────────────────────────────
        // High vol -> reduce size to control risk
        double vol_scale = 1.0;
        if (volatility > 1e-12) {
            vol_scale = VOL_BASELINE / std::max(volatility, VOL_BASELINE * 0.1);
            vol_scale = std::clamp(vol_scale, 0.2, 2.0);
        }
        qty *= vol_scale;

        // ── 3. Liquidity scaling ─────────────────────────────────────────
        // Low liquidity -> reduce size to avoid impact
        double liq_scale = std::clamp(liquidity, 0.3, 1.5);
        qty *= liq_scale;

        // ── 4. Spread scaling ────────────────────────────────────────────
        // Wide spread -> reduce size (higher execution cost)
        double spread_scale = 1.0;
        if (spread_bps > SPREAD_BASELINE) {
            spread_scale = SPREAD_BASELINE / spread_bps;
            spread_scale = std::clamp(spread_scale, 0.3, 1.0);
        }
        qty *= spread_scale;

        // ── 5. Regime scaling (branchless via LUT) ─────────────────────
        qty *= regime_position_scale(regime);

        // ── 6. Position limit check ──────────────────────────────────────
        // Don't exceed max position size
        double current_pos = std::abs(pos.size.raw());
        double remaining = max_position_ - current_pos;
        if (remaining <= 0.0) return Qty(0.0);
        qty = std::min(qty, remaining);

        // ── 7. Drawdown reduction ────────────────────────────────────────
        // If in drawdown, reduce position size progressively
        double net_pnl = pos.realized_pnl.raw() + pos.unrealized_pnl.raw() + pos.funding_impact.raw();
        if (net_pnl < -DRAWDOWN_THRESHOLD) {
            double dd_scale = std::max(0.1, 1.0 + net_pnl / (DRAWDOWN_THRESHOLD * 5.0));
            qty *= dd_scale;
        }

        // ── Clamp to bounds ──────────────────────────────────────────────
        qty = std::clamp(qty, min_qty_, max_qty_);

        // Round to lot size
        qty = std::round(qty / lot_size_) * lot_size_;

        return Qty(qty);
    }

private:
    static constexpr double VOL_BASELINE       = 0.0003;
    static constexpr double SPREAD_BASELINE    = 1.5;   // bps
    static constexpr double DRAWDOWN_THRESHOLD = 100.0;  // USD

    double base_qty_;
    double min_qty_;
    double max_qty_;
    double max_position_;
    double lot_size_;
};

} // namespace bybit
