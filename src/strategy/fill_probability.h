#pragma once

#include "../config/types.h"
#include "../orderbook/orderbook.h"
#include "../trade_flow/trade_flow_engine.h"
#include "../utils/clock.h"
#include <cmath>
#include <algorithm>

namespace bybit {

// ─── Fill Probability Model ─────────────────────────────────────────────────
// Estimates the probability that a limit order at a given price will be filled
// within various time horizons (100ms, 500ms, 1s).
//
// Uses:
//   - Queue position estimate
//   - Trade flow velocity
//   - Liquidity removal rate
//   - Order book imbalance (directional pressure)

class FillProbabilityModel {
public:
    FillProbabilityModel() noexcept = default;

    // Estimate fill probability for a limit order at given price/side.
    FillProbability estimate(Side side, Price price, Qty qty,
                             const OrderBook& ob,
                             const TradeFlowEngine& tf,
                             const Features& f) const noexcept {
        FillProbability fp;

        if (!ob.valid()) return fp;

        double p = price.raw();
        double q = qty.raw();
        double mid = ob.mid_price();
        double spread = ob.spread();

        // ── Queue position estimate ──────────────────────────────────────
        // How deep in the queue is our order?
        // 0.0 = front of queue, 1.0 = back of queue
        double queue_ahead = estimate_queue_ahead(side, p, ob);
        fp.queue_position = queue_ahead;

        // ── Trade flow velocity ──────────────────────────────────────────
        // How fast are trades happening on our side?
        auto flow = tf.compute();
        double relevant_volume = (side == Side::Buy)
            ? flow.w500ms.sell_volume   // sells hit our bids
            : flow.w500ms.buy_volume;   // buys hit our asks
        double trade_rate = flow.w500ms.trade_rate;

        // ── Liquidity removal rate ───────────────────────────────────────
        // How fast is liquidity being removed from our price level?
        double cancel_rate = f.cancel_spike; // proxy for cancellation activity
        // Higher trade rate means more churn -> higher fill chance
        double trade_rate_boost = std::min(trade_rate * 0.01, 0.1);

        // ── Directional pressure ─────────────────────────────────────────
        // Imbalance in our favor increases fill probability
        double imb = f.imbalance_5;
        double direction_boost = 0.0;
        if (side == Side::Buy && imb < 0.0) {
            // More asks than bids -> price likely to come down -> bid gets filled
            direction_boost = std::abs(imb) * 0.3;
        } else if (side == Side::Sell && imb > 0.0) {
            // More bids than asks -> price likely to go up -> ask gets filled
            direction_boost = std::abs(imb) * 0.3;
        }

        // ── Price distance from mid ──────────────────────────────────────
        double dist_from_mid = 0.0;
        if (side == Side::Buy) {
            dist_from_mid = (mid - p) / spread; // >0 if below mid
        } else {
            dist_from_mid = (p - mid) / spread; // >0 if above mid
        }
        // Negative distance = aggressive (inside spread) -> higher fill prob
        double dist_factor = std::exp(-std::max(0.0, dist_from_mid) * 1.5);

        // ── Combine into fill probabilities ──────────────────────────────
        // Base fill probability from queue position and trade rate
        double base_rate = (queue_ahead > 1e-12)
            ? relevant_volume / (queue_ahead + q * 0.5)
            : 10.0; // very front of queue

        // Time-based fill probability: P(fill in T) = 1 - exp(-rate * T)
        double effective_rate = base_rate * dist_factor * (1.0 + direction_boost)
                               * (1.0 - cancel_rate * 0.5) * (1.0 + trade_rate_boost);
        effective_rate = std::max(effective_rate, 0.01);

        fp.prob_fill_100ms = 1.0 - std::exp(-effective_rate * 0.1);
        fp.prob_fill_500ms = 1.0 - std::exp(-effective_rate * 0.5);
        fp.prob_fill_1s    = 1.0 - std::exp(-effective_rate * 1.0);

        fp.liq_removal_rate = cancel_rate;

        return fp;
    }

    // Should we use a market order instead of limit?
    // Returns true if fill probability is too low and signal is strong.
    bool should_use_market(const FillProbability& fp, double confidence,
                           double market_threshold) const noexcept {
        // Use market order if:
        // 1. Fill probability within 500ms is very low
        // 2. Signal confidence is high
        return (fp.prob_fill_500ms < market_threshold) && (confidence > 0.75);
    }

    // Compute optimal limit price given fill probability target
    Price optimal_price(Side side, double target_fill_prob,
                        const OrderBook& ob,
                        const TradeFlowEngine& tf,
                        const Features& f) const noexcept {
        if (!ob.valid()) return Price((side == Side::Buy) ? ob.best_bid() : ob.best_ask());

        // Binary search for price that gives target fill probability
        double lo, hi;
        if (side == Side::Buy) {
            lo = ob.best_bid() - ob.spread() * 5.0;
            hi = ob.best_ask(); // crossing spread
        } else {
            lo = ob.best_bid();
            hi = ob.best_ask() + ob.spread() * 5.0;
        }

        for (int iter = 0; iter < 10; ++iter) {
            double mid_price = (lo + hi) * 0.5;
            FillProbability fp = estimate(side, Price(mid_price), Qty(0.001), ob, tf, f);

            if (fp.prob_fill_500ms < target_fill_prob) {
                // Need more aggressive price
                if (side == Side::Buy) lo = mid_price;
                else hi = mid_price;
            } else {
                if (side == Side::Buy) hi = mid_price;
                else lo = mid_price;
            }
        }

        return Price((lo + hi) * 0.5);
    }

private:
    // Estimate the volume ahead of us in the queue at given price level
    double estimate_queue_ahead(Side side, double price,
                                const OrderBook& ob) const noexcept {
        double ahead = 0.0;

        if (side == Side::Buy) {
            const PriceLevel* bids = ob.bids();
            size_t count = ob.bid_count();
            for (size_t i = 0; i < count; ++i) {
                if (bids[i].price > price + 1e-12) {
                    ahead += bids[i].qty; // better prices filled first
                } else if (std::abs(bids[i].price - price) < 1e-12) {
                    ahead += bids[i].qty * 0.5; // assume middle of queue at same price
                    break;
                } else {
                    break;
                }
            }
        } else {
            const PriceLevel* asks = ob.asks();
            size_t count = ob.ask_count();
            for (size_t i = 0; i < count; ++i) {
                if (asks[i].price < price - 1e-12) {
                    ahead += asks[i].qty;
                } else if (std::abs(asks[i].price - price) < 1e-12) {
                    ahead += asks[i].qty * 0.5;
                    break;
                } else {
                    break;
                }
            }
        }

        return ahead;
    }
};

} // namespace bybit
