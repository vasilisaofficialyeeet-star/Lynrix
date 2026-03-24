#pragma once

// #11: Realistic paper trading fill simulation
// Models queue position, slippage, partial fills, and latency to produce
// more accurate paper trading results than instant-fill assumption.

#include "../config/types.h"
#include "../orderbook/orderbook.h"
#include "../utils/clock.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <algorithm>
#include <array>

namespace bybit {

// ─── Fill Simulation Config ─────────────────────────────────────────────────

struct PaperFillConfig {
    double queue_position_pct  = 0.5;   // Assumed queue position (0=front, 1=back)
    double slippage_mean_bps   = 0.5;   // Mean slippage in basis points
    double slippage_std_bps    = 0.3;   // Std dev of slippage
    double partial_fill_prob   = 0.15;  // Probability of partial fill
    double partial_fill_min    = 0.3;   // Min fraction filled on partial
    double partial_fill_max    = 0.8;   // Max fraction filled on partial
    uint64_t fill_latency_ns   = 5000;  // Simulated fill latency (5µs base)
    uint64_t fill_latency_jitter_ns = 2000; // Jitter range
    bool     enable_queue_model = true;  // Use queue position model
    bool     enable_slippage    = true;  // Add random slippage
    bool     enable_partials    = true;  // Allow partial fills
};

// ─── Fill Result ─────────────────────────────────────────────────────────────

struct PaperFillResult {
    bool     filled        = false;
    bool     partial       = false;
    double   fill_price    = 0.0;
    double   fill_qty      = 0.0;
    double   slippage_bps  = 0.0;
    uint64_t fill_delay_ns = 0;
    double   queue_ahead   = 0.0;   // Estimated volume ahead in queue
};

// ─── Paper Fill Simulator ────────────────────────────────────────────────────

class PaperFillSimulator {
public:
    explicit PaperFillSimulator(const PaperFillConfig& cfg = {}, double tick_size = TICK_SIZE_BTCUSDT)
        : cfg_(cfg)
        , tick_size_(tick_size)
        , rng_(std::random_device{}())
        , slippage_dist_(cfg.slippage_mean_bps, cfg.slippage_std_bps)
        , uniform_(0.0, 1.0)
        , latency_dist_(0.0, static_cast<double>(cfg.fill_latency_jitter_ns))
    {}

    void set_tick_size(double ts) noexcept { tick_size_ = ts; }

    // Simulate whether a limit order would fill given current orderbook state
    PaperFillResult simulate_limit_fill(
            Side side, double order_price, double order_qty,
            const OrderBook& ob, uint64_t order_age_ns) const {
        PaperFillResult result;

        if (!ob.valid()) return result;

        double best_bid = ob.best_bid();
        double best_ask = ob.best_ask();

        // Check if order would cross the spread (immediate fill)
        bool crosses = (side == Side::Buy && order_price >= best_ask) ||
                       (side == Side::Sell && order_price <= best_bid);

        if (crosses) {
            // Market-crossing limit order — fills immediately with possible slippage
            result.filled = true;
            result.fill_qty = order_qty;
            result.fill_price = (side == Side::Buy) ? best_ask : best_bid;

            if (cfg_.enable_slippage) {
                double slip = std::abs(slippage_dist_(rng_));
                result.slippage_bps = slip;
                double slip_price = result.fill_price * slip * 1e-4;
                result.fill_price += (side == Side::Buy) ? slip_price : -slip_price;
            }
        } else if (cfg_.enable_queue_model) {
            // Order resting in book — check queue position
            double queue_level_qty = estimate_queue_qty(side, order_price, ob);
            double queue_ahead = queue_level_qty * cfg_.queue_position_pct;
            result.queue_ahead = queue_ahead;

            // Estimate volume traded through this level based on order age
            // Rough model: ~1 BTC/second trades through top levels on BTCUSDT
            double trade_rate_per_sec = 1.0; // BTC/s approximate
            double age_sec = static_cast<double>(order_age_ns) / 1e9;
            double volume_through = age_sec * trade_rate_per_sec;

            if (volume_through > queue_ahead) {
                result.filled = true;
                result.fill_price = order_price; // Limit price honored
                result.fill_qty = order_qty;

                // Partial fill check
                if (cfg_.enable_partials && uniform_(rng_) < cfg_.partial_fill_prob) {
                    double frac = cfg_.partial_fill_min +
                        uniform_(rng_) * (cfg_.partial_fill_max - cfg_.partial_fill_min);
                    result.fill_qty = order_qty * frac;
                    result.partial = true;
                }
            }
        } else {
            // Simple model: fill if price touched
            double touch_price = (side == Side::Buy) ? best_ask : best_bid;
            if ((side == Side::Buy && touch_price <= order_price) ||
                (side == Side::Sell && touch_price >= order_price)) {
                result.filled = true;
                result.fill_price = order_price;
                result.fill_qty = order_qty;
            }
        }

        // Simulate fill latency
        if (result.filled) {
            result.fill_delay_ns = cfg_.fill_latency_ns +
                static_cast<uint64_t>(std::abs(latency_dist_(rng_)));
        }

        return result;
    }

    // Simulate market order fill
    PaperFillResult simulate_market_fill(
            Side side, double order_qty, const OrderBook& ob) const {
        PaperFillResult result;

        if (!ob.valid()) return result;

        result.filled = true;
        result.fill_price = (side == Side::Buy) ? ob.best_ask() : ob.best_bid();
        result.fill_qty = order_qty;

        // Walk the book for large orders
        if (order_qty > 0.0) {
            double remaining = order_qty;
            double total_cost = 0.0;
            const PriceLevel* levels = (side == Side::Buy) ? ob.asks() : ob.bids();
            size_t count = (side == Side::Buy) ? ob.ask_count() : ob.bid_count();

            for (size_t i = 0; i < count && remaining > 0.0; ++i) {
                double fill_at = std::min(remaining, levels[i].qty);
                total_cost += fill_at * levels[i].price;
                remaining -= fill_at;
            }

            if (remaining < order_qty) {
                double filled = order_qty - remaining;
                result.fill_price = total_cost / filled; // VWAP
                result.fill_qty = filled;
                if (remaining > 0.0) result.partial = true;
            }
        }

        // Add slippage
        if (cfg_.enable_slippage) {
            double slip = std::abs(slippage_dist_(rng_));
            result.slippage_bps = slip;
            double slip_price = result.fill_price * slip * 1e-4;
            result.fill_price += (side == Side::Buy) ? slip_price : -slip_price;
        }

        result.fill_delay_ns = cfg_.fill_latency_ns +
            static_cast<uint64_t>(std::abs(latency_dist_(rng_)));

        return result;
    }

    const PaperFillConfig& config() const { return cfg_; }

private:
    double estimate_queue_qty(Side side, double price, const OrderBook& ob) const {
        const PriceLevel* levels = (side == Side::Buy) ? ob.bids() : ob.asks();
        size_t count = (side == Side::Buy) ? ob.bid_count() : ob.ask_count();

        for (size_t i = 0; i < count; ++i) {
            if (std::abs(levels[i].price - price) < tick_size_ * 0.5) {
                return levels[i].qty;
            }
        }
        return 0.0; // Price level not in book
    }

    PaperFillConfig cfg_;
    double tick_size_ = TICK_SIZE_BTCUSDT;
    mutable std::mt19937 rng_;
    mutable std::normal_distribution<double> slippage_dist_;
    mutable std::uniform_real_distribution<double> uniform_;
    mutable std::normal_distribution<double> latency_dist_;
};

} // namespace bybit
