#pragma once

#include "../config/types.h"
#include "../orderbook/orderbook.h"
#include "../trade_flow/trade_flow_engine.h"
#include "../feature_engine/advanced_feature_engine.h"
#include "../analytics/strategy_metrics.h"
#include "../utils/clock.h"
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <functional>
#include <string>
#include <spdlog/spdlog.h>

namespace bybit {

// ─── Backtest Event Types ───────────────────────────────────────────────────

struct BacktestTick {
    uint64_t timestamp_ns = 0;
    double   bid_price    = 0.0;
    double   ask_price    = 0.0;
    double   bid_qty      = 0.0;
    double   ask_qty      = 0.0;
    double   last_price   = 0.0;
    double   last_qty     = 0.0;
    bool     is_buyer_maker = false;
};

struct BacktestOrderBookSnapshot {
    uint64_t timestamp_ns = 0;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

struct SimulatedFill {
    uint64_t timestamp_ns = 0;
    Side     side         = Side::Buy;
    double   price        = 0.0;
    double   qty          = 0.0;
    double   fee          = 0.0;
    double   slippage_bps = 0.0;
    bool     is_partial   = false;
};

struct SimulatedOrder {
    uint64_t order_id      = 0;
    Side     side          = Side::Buy;
    OrderType type         = OrderType::Limit;
    double   price         = 0.0;
    double   qty           = 0.0;
    double   filled_qty    = 0.0;
    uint64_t submit_time_ns = 0;
    OrderStatus status     = OrderStatus::New;
    double   queue_position = 0.0; // estimated queue position [0..1]
    int      latency_ticks = 0;   // simulated latency in ticks
};

// ─── Backtest Configuration ─────────────────────────────────────────────────

struct BacktestConfig {
    // Fees
    double maker_fee_rate   = 0.0001;  // 1 bps maker
    double taker_fee_rate   = 0.0006;  // 6 bps taker
    double funding_rate_8h  = 0.0001;  // 1 bps per 8h

    // Slippage model
    double base_slippage_bps = 0.5;    // base slippage
    double impact_factor     = 0.1;    // price impact per unit volume

    // Latency simulation
    int    order_latency_ticks = 3;    // ticks delay before order appears in book
    int    cancel_latency_ticks = 2;

    // Queue model
    double queue_priority    = 0.5;    // 0 = back of queue, 1 = front
    bool   partial_fills     = true;
    double min_fill_qty      = 0.001;

    // Funding
    bool   simulate_funding  = true;
    int    funding_interval_ns_shift = 28800; // 8 hours in seconds
};

// ─── Backtest Results ───────────────────────────────────────────────────────

struct BacktestResult {
    StrategyMetricsSnapshot metrics;

    double total_pnl        = 0.0;
    double total_fees       = 0.0;
    double total_funding    = 0.0;
    double total_slippage   = 0.0;
    double net_pnl          = 0.0;
    int    total_fills      = 0;
    int    total_orders     = 0;
    int    cancelled_orders = 0;

    std::vector<SimulatedFill> fills;
    std::vector<double> equity_curve;
    std::vector<double> drawdown_curve;

    uint64_t start_ns = 0;
    uint64_t end_ns   = 0;
    double   duration_hours = 0.0;
};

// ─── Tick-by-Tick Backtesting Engine ────────────────────────────────────────
// Realistic simulation with:
//   - Order queue position modeling
//   - Partial fills
//   - Latency simulation
//   - Fee and slippage accounting
//   - Funding payments

class Backtester {
public:
    using SignalCallback = std::function<void(const Features&, const OrderBook&,
                                              std::vector<SimulatedOrder>&)>;

    explicit Backtester(const BacktestConfig& cfg = {}) noexcept
        : cfg_(cfg) {}

    // Run backtest on tick data with strategy callback
    BacktestResult run(const std::vector<BacktestTick>& ticks,
                       SignalCallback strategy_fn) {
        reset();
        result_.start_ns = ticks.empty() ? 0 : ticks.front().timestamp_ns;
        result_.end_ns = ticks.empty() ? 0 : ticks.back().timestamp_ns;

        for (size_t i = 0; i < ticks.size(); ++i) {
            const auto& tick = ticks[i];
            current_time_ = tick.timestamp_ns;

            // Update order book with tick
            update_ob_from_tick(tick);

            // Update trade flow
            if (tick.last_qty > 0.0) {
                Trade t;
                t.timestamp_ns = tick.timestamp_ns;
                t.price = tick.last_price;
                t.qty = tick.last_qty;
                t.is_buyer_maker = tick.is_buyer_maker;
                tf_.on_trade(t);
            }

            // Match pending orders against current book
            match_orders(tick);

            // Compute features
            Features f = feature_engine_.compute(ob_, tf_);

            // Call strategy to generate new orders
            std::vector<SimulatedOrder> new_orders;
            strategy_fn(f, ob_, new_orders);

            for (auto& ord : new_orders) {
                ord.order_id = next_order_id_++;
                ord.submit_time_ns = current_time_;
                ord.latency_ticks = cfg_.order_latency_ticks;

                // Estimate queue position
                if (ord.type == OrderType::Limit) {
                    ord.queue_position = estimate_queue_position(ord);
                }

                pending_orders_.push_back(ord);
                result_.total_orders++;
            }

            // Simulate funding payments
            if (cfg_.simulate_funding) {
                simulate_funding(tick.timestamp_ns);
            }

            // Mark to market
            double mid = ob_.mid_price();
            if (mid > 0.0 && std::abs(position_size_) > 1e-12) {
                unrealized_pnl_ = (mid - avg_entry_) * position_size_;
            }

            double equity = realized_pnl_ + unrealized_pnl_ - total_fees_ - total_funding_;
            strategy_metrics_.update_equity(equity);

            // Record equity curve (every 100 ticks to save memory)
            if (i % 100 == 0) {
                result_.equity_curve.push_back(equity);
                result_.drawdown_curve.push_back(
                    strategy_metrics_.snapshot().current_drawdown);
            }
        }

        // Finalize
        result_.total_pnl = realized_pnl_ + unrealized_pnl_;
        result_.total_fees = total_fees_;
        result_.total_funding = total_funding_;
        result_.total_slippage = total_slippage_;
        result_.net_pnl = result_.total_pnl - result_.total_fees
                         - result_.total_funding - result_.total_slippage;
        result_.metrics = strategy_metrics_.snapshot();
        result_.cancelled_orders = cancelled_count_;

        if (result_.end_ns > result_.start_ns) {
            result_.duration_hours = static_cast<double>(
                result_.end_ns - result_.start_ns) / 3.6e12;
        }

        return result_;
    }

    // Run backtest on full order book snapshots
    BacktestResult run_ob(const std::vector<BacktestOrderBookSnapshot>& snapshots,
                          SignalCallback strategy_fn) {
        std::vector<BacktestTick> ticks;
        ticks.reserve(snapshots.size());

        for (const auto& snap : snapshots) {
            BacktestTick t;
            t.timestamp_ns = snap.timestamp_ns;
            if (!snap.bids.empty()) {
                t.bid_price = snap.bids[0].price;
                t.bid_qty = snap.bids[0].qty;
            }
            if (!snap.asks.empty()) {
                t.ask_price = snap.asks[0].price;
                t.ask_qty = snap.asks[0].qty;
            }
            t.last_price = (t.bid_price + t.ask_price) * 0.5;
            ticks.push_back(t);
        }

        return run(ticks, std::move(strategy_fn));
    }

private:
    void reset() {
        result_ = {};
        strategy_metrics_.reset();
        feature_engine_.reset();
        pending_orders_.clear();
        position_size_ = 0.0;
        avg_entry_ = 0.0;
        realized_pnl_ = 0.0;
        unrealized_pnl_ = 0.0;
        total_fees_ = 0.0;
        total_funding_ = 0.0;
        total_slippage_ = 0.0;
        next_order_id_ = 1;
        cancelled_count_ = 0;
        last_funding_ns_ = 0;
    }

    void update_ob_from_tick(const BacktestTick& tick) {
        if (tick.bid_price > 0.0 && tick.ask_price > 0.0) {
            // Synthetic OB from BBO
            PriceLevel bid{tick.bid_price, tick.bid_qty > 0 ? tick.bid_qty : 1.0};
            PriceLevel ask{tick.ask_price, tick.ask_qty > 0 ? tick.ask_qty : 1.0};
            ob_.set_bbo(bid, ask, tick.timestamp_ns);
        }
    }

    void match_orders(const BacktestTick& tick) {
        auto it = pending_orders_.begin();
        while (it != pending_orders_.end()) {
            auto& ord = *it;

            // Simulate latency: order not visible until latency_ticks elapsed
            if (ord.latency_ticks > 0) {
                ord.latency_ticks--;
                ++it;
                continue;
            }

            bool filled = false;

            if (ord.type == OrderType::Market || ord.type == OrderType::Limit) {
                filled = try_fill(ord, tick);
            }

            if (filled || ord.status == OrderStatus::Filled ||
                ord.status == OrderStatus::Cancelled) {
                it = pending_orders_.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool try_fill(SimulatedOrder& ord, const BacktestTick& tick) {
        double fill_price = 0.0;
        double fill_qty = ord.qty - ord.filled_qty;
        bool is_taker = false;

        if (ord.type == OrderType::Market) {
            // Market order: fill at current price + slippage
            fill_price = (ord.side == Side::Buy) ? tick.ask_price : tick.bid_price;
            is_taker = true;
        } else {
            // Limit order: check if price crossed
            if (ord.side == Side::Buy) {
                if (tick.ask_price <= ord.price) {
                    fill_price = ord.price;
                    // Queue-based partial fill
                    if (cfg_.partial_fills && tick.last_qty > 0.0) {
                        double available = tick.last_qty * (1.0 - ord.queue_position);
                        fill_qty = std::min(fill_qty, available);
                        fill_qty = std::max(fill_qty, cfg_.min_fill_qty);
                    }
                } else {
                    return false;
                }
            } else {
                if (tick.bid_price >= ord.price) {
                    fill_price = ord.price;
                    if (cfg_.partial_fills && tick.last_qty > 0.0) {
                        double available = tick.last_qty * (1.0 - ord.queue_position);
                        fill_qty = std::min(fill_qty, available);
                        fill_qty = std::max(fill_qty, cfg_.min_fill_qty);
                    }
                } else {
                    return false;
                }
            }
            is_taker = (ord.side == Side::Buy && fill_price >= tick.ask_price) ||
                       (ord.side == Side::Sell && fill_price <= tick.bid_price);
        }

        if (fill_price <= 0.0 || fill_qty <= 0.0) return false;

        // Apply slippage
        double slippage = cfg_.base_slippage_bps * fill_price / 10000.0;
        if (is_taker) {
            slippage += cfg_.impact_factor * fill_qty * fill_price / 10000.0;
        }
        if (ord.side == Side::Buy) {
            fill_price += slippage;
        } else {
            fill_price -= slippage;
        }

        // Calculate fee
        double fee_rate = is_taker ? cfg_.taker_fee_rate : cfg_.maker_fee_rate;
        double fee = fill_qty * fill_price * fee_rate;

        // Record fill
        SimulatedFill fill;
        fill.timestamp_ns = current_time_;
        fill.side = ord.side;
        fill.price = fill_price;
        fill.qty = fill_qty;
        fill.fee = fee;
        fill.slippage_bps = slippage / fill_price * 10000.0;
        fill.is_partial = (fill_qty < (ord.qty - ord.filled_qty - 1e-12));

        result_.fills.push_back(fill);
        result_.total_fills++;

        spdlog::debug("[BT_FILL] id={} {} {} qty={:.6f} price={:.2f} fee={:.6f} slip={:.2f}bps taker={}",
                      ord.order_id, ord.side == Side::Buy ? "BUY" : "SELL",
                      ord.type == OrderType::Market ? "MKT" : "LMT",
                      fill_qty, fill_price, fee, fill.slippage_bps, is_taker);

        // Update position
        double prev_pnl = realized_pnl_;
        update_position(ord.side, fill_price, fill_qty);

        total_fees_ += fee;
        total_slippage_ += slippage * fill_qty;

        // Track trade PnL if position changed direction or closed
        double trade_pnl = realized_pnl_ - prev_pnl - fee;
        if (std::abs(trade_pnl) > 1e-12) {
            strategy_metrics_.record_trade(trade_pnl);
            spdlog::debug("[BT_PNL] trade_pnl={:.6f} realized={:.6f} pos={:.6f} entry={:.2f}",
                          trade_pnl, realized_pnl_, position_size_, avg_entry_);
        }

        // Update order state
        ord.filled_qty += fill_qty;
        if (ord.filled_qty >= ord.qty - 1e-12) {
            ord.status = OrderStatus::Filled;
            return true;
        }

        // Reduce queue position for next partial fill
        ord.queue_position *= 0.8;
        return false;
    }

    void update_position(Side side, double price, double qty) {
        double signed_qty = (side == Side::Buy) ? qty : -qty;

        if (std::abs(position_size_) < 1e-12) {
            // New position
            position_size_ = signed_qty;
            avg_entry_ = price;
            spdlog::debug("[BT_POS] OPEN {} size={:.6f} entry={:.2f}",
                          side == Side::Buy ? "LONG" : "SHORT", position_size_, avg_entry_);
        } else if ((position_size_ > 0 && side == Side::Buy) ||
                   (position_size_ < 0 && side == Side::Sell)) {
            // Adding to position
            double total = std::abs(position_size_) + qty;
            avg_entry_ = (avg_entry_ * std::abs(position_size_) + price * qty) / total;
            position_size_ += signed_qty;
            spdlog::debug("[BT_POS] ADD size={:.6f} avg_entry={:.2f}",
                          position_size_, avg_entry_);
        } else {
            // Closing/reversing
            double close_qty = std::min(qty, std::abs(position_size_));
            double pnl = (price - avg_entry_) * close_qty;
            if (position_size_ < 0) pnl = -pnl;
            realized_pnl_ += pnl;
            spdlog::debug("[BT_POS] CLOSE qty={:.6f} pnl={:.6f} total_realized={:.6f}",
                          close_qty, pnl, realized_pnl_);

            position_size_ += signed_qty;
            if (std::abs(position_size_) < 1e-12) {
                position_size_ = 0.0;
                avg_entry_ = 0.0;
            } else if ((position_size_ > 0 && side == Side::Buy) ||
                       (position_size_ < 0 && side == Side::Sell)) {
                // Reversed position
                avg_entry_ = price;
                spdlog::debug("[BT_POS] REVERSED size={:.6f} new_entry={:.2f}",
                              position_size_, avg_entry_);
            }
        }
    }

    double estimate_queue_position(const SimulatedOrder& ord) const {
        // Simple model: based on how aggressive the price is
        if (ord.side == Side::Buy) {
            double best_bid = ob_.best_bid();
            if (ord.price >= best_bid) return cfg_.queue_priority * 0.3;
            return cfg_.queue_priority;
        } else {
            double best_ask = ob_.best_ask();
            if (ord.price <= best_ask) return cfg_.queue_priority * 0.3;
            return cfg_.queue_priority;
        }
    }

    void simulate_funding(uint64_t now_ns) {
        if (std::abs(position_size_) < 1e-12) return;

        uint64_t interval_ns = static_cast<uint64_t>(cfg_.funding_interval_ns_shift)
                               * 1'000'000'000ULL;
        if (last_funding_ns_ == 0) {
            last_funding_ns_ = now_ns;
            return;
        }

        if (now_ns - last_funding_ns_ >= interval_ns) {
            double funding = std::abs(position_size_) * ob_.mid_price()
                             * cfg_.funding_rate_8h;
            total_funding_ += funding;
            last_funding_ns_ = now_ns;
        }
    }

    BacktestConfig cfg_;
    BacktestResult result_;
    StrategyMetrics strategy_metrics_;
    AdvancedFeatureEngine feature_engine_;
    OrderBook ob_;
    TradeFlowEngine tf_;

    std::deque<SimulatedOrder> pending_orders_;
    double position_size_ = 0.0;
    double avg_entry_ = 0.0;
    double realized_pnl_ = 0.0;
    double unrealized_pnl_ = 0.0;
    double total_fees_ = 0.0;
    double total_funding_ = 0.0;
    double total_slippage_ = 0.0;

    uint64_t next_order_id_ = 1;
    int cancelled_count_ = 0;
    uint64_t current_time_ = 0;
    uint64_t last_funding_ns_ = 0;
};

} // namespace bybit
