#pragma once

// #12: Backtesting framework
// Event-driven backtester that replays historical market data through
// the same strategy pipeline used in live trading.

#include "../config/types.h"
#include "../orderbook/orderbook.h"
#include "../trade_flow/trade_flow_engine.h"
#include "../portfolio/portfolio.h"
#include "../feature_engine/advanced_feature_engine.h"
#include "../model_engine/gru_model.h"
#include "../strategy/adaptive_threshold.h"
#include "../strategy/adaptive_position_sizer.h"
#include "../strategy/fill_probability.h"
#include "../regime/regime_detector.h"
#include "../risk_engine/enhanced_risk_engine.h"
#include "../execution_engine/paper_fill_simulator.h"
#include "../analytics/strategy_metrics.h"
#include "../utils/clock.h"
#include "../core/strong_types.h"

#include <spdlog/spdlog.h>

#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <functional>
#include <algorithm>

namespace bybit {

// ─── Historical Data Types ──────────────────────────────────────────────────

struct HistoricalTick {
    uint64_t timestamp_ns;
    double   best_bid;
    double   best_ask;
    double   bid_qty;
    double   ask_qty;
    double   trade_price;
    double   trade_qty;
    bool     is_buyer_maker;
};

struct BacktestConfig {
    std::string data_file;              // Path to CSV data file
    std::string symbol = "BTCUSDT";
    double      initial_equity = 10000.0;
    double      maker_fee_bps  = 1.0;   // 0.01% maker fee
    double      taker_fee_bps  = 6.0;   // 0.06% taker fee
    bool        use_realistic_fills = true;
    PaperFillConfig fill_config;
    RiskLimits  risk_limits;
    CircuitBreakerConfig cb_config;
    double      signal_threshold = 0.6;
    double      tick_size = TICK_SIZE_BTCUSDT;
    double      lot_size  = LOT_SIZE_BTCUSDT;

    // Strategy params
    int         feature_tick_ms = 10;
    bool        rl_enabled = false;
};

// ─── Backtest Results ────────────────────────────────────────────────────────

struct BacktestResults {
    // Performance
    double total_pnl       = 0.0;
    double max_drawdown    = 0.0;
    double sharpe_ratio    = 0.0;
    double sortino_ratio   = 0.0;
    double win_rate        = 0.0;
    double profit_factor   = 0.0;
    double avg_trade_pnl   = 0.0;
    double max_trade_pnl   = 0.0;
    double min_trade_pnl   = 0.0;

    // Execution
    uint64_t total_signals  = 0;
    uint64_t total_orders   = 0;
    uint64_t total_fills    = 0;
    uint64_t partial_fills  = 0;
    double   avg_slippage_bps = 0.0;
    double   total_fees     = 0.0;

    // Timing
    uint64_t total_ticks    = 0;
    uint64_t start_ns       = 0;
    uint64_t end_ns         = 0;
    double   wall_time_sec  = 0.0;
    double   ticks_per_sec  = 0.0;

    // Equity curve
    std::vector<std::pair<uint64_t, double>> equity_curve;
};

// ─── Backtest Engine ─────────────────────────────────────────────────────────

class BacktestEngine {
public:
    explicit BacktestEngine(const BacktestConfig& cfg)
        : cfg_(cfg)
        , fill_sim_(cfg.fill_config, cfg.tick_size)
        , risk_(cfg.risk_limits, cfg.cb_config)
        , threshold_(cfg.signal_threshold, 0.3, 0.9)
        , sizer_(make_app_config(cfg))
        , lot_size_(cfg.lot_size)
    {}

    // Load historical data from CSV
    // Format: timestamp_ns,best_bid,best_ask,bid_qty,ask_qty,trade_price,trade_qty,is_buyer_maker
    bool load_data(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            spdlog::error("[BACKTEST] Failed to open data file: {}", path);
            return false;
        }

        std::string line;
        std::getline(file, line); // Skip header

        while (std::getline(file, line)) {
            std::istringstream ss(line);
            HistoricalTick tick;
            char delim;

            ss >> tick.timestamp_ns >> delim
               >> tick.best_bid >> delim
               >> tick.best_ask >> delim
               >> tick.bid_qty >> delim
               >> tick.ask_qty >> delim
               >> tick.trade_price >> delim
               >> tick.trade_qty >> delim
               >> tick.is_buyer_maker;

            if (!ss.fail()) {
                data_.push_back(tick);
            }
        }

        spdlog::info("[BACKTEST] Loaded {} ticks from {}", data_.size(), path);
        return !data_.empty();
    }

    // Add data programmatically
    void add_tick(const HistoricalTick& tick) {
        data_.push_back(tick);
    }

    // Run the backtest
    BacktestResults run() {
        BacktestResults results;
        if (data_.empty()) {
            spdlog::warn("[BACKTEST] No data loaded");
            return results;
        }

        uint64_t wall_start = Clock::now_ns();
        results.start_ns = data_.front().timestamp_ns;
        results.end_ns = data_.back().timestamp_ns;

        // Reset state
        ob_ = OrderBook();
        portfolio_ = Portfolio();
        feature_engine_ = AdvancedFeatureEngine();
        regime_detector_ = RegimeDetector();

        double peak_equity = cfg_.initial_equity;
        double max_dd = 0.0;
        uint64_t last_feature_ns = 0;

        for (size_t i = 0; i < data_.size(); ++i) {
            const auto& tick = data_[i];

            // Update orderbook with synthetic snapshot
            PriceLevel bids[1] = {{tick.best_bid, tick.bid_qty}};
            PriceLevel asks[1] = {{tick.best_ask, tick.ask_qty}};
            ob_.apply_snapshot(bids, 1, asks, 1, i);

            // Feed trade
            if (tick.trade_price > 0.0) {
                Trade t;
                t.timestamp_ns = tick.timestamp_ns;
                t.price = tick.trade_price;
                t.qty = tick.trade_qty;
                t.is_buyer_maker = tick.is_buyer_maker;
                tf_.on_trade(t);
            }

            // Mark to market
            double mid = (tick.best_bid + tick.best_ask) / 2.0;
            portfolio_.mark_to_market(Price(mid));

            // Run strategy at configured interval
            if (tick.timestamp_ns - last_feature_ns >=
                    static_cast<uint64_t>(cfg_.feature_tick_ms) * 1'000'000ULL) {
                last_feature_ns = tick.timestamp_ns;
                run_strategy_tick(tick, results);
                ++results.total_ticks;
            }

            // Track equity curve (sample every 1000 ticks)
            if (i % 1000 == 0) {
                double equity = cfg_.initial_equity + portfolio_.net_pnl().raw();
                results.equity_curve.emplace_back(tick.timestamp_ns, equity);

                if (equity > peak_equity) peak_equity = equity;
                double dd = (peak_equity - equity) / peak_equity;
                if (dd > max_dd) max_dd = dd;
            }
        }

        // Compute final results
        uint64_t wall_end = Clock::now_ns();
        results.wall_time_sec = static_cast<double>(wall_end - wall_start) / 1e9;
        results.ticks_per_sec = results.total_ticks > 0
            ? static_cast<double>(results.total_ticks) / results.wall_time_sec : 0.0;

        Position pos = portfolio_.snapshot();
        results.total_pnl = portfolio_.net_pnl().raw();
        results.max_drawdown = max_dd;
        results.total_fees = total_fees_;

        // Strategy metrics
        auto& sm = strategy_metrics_.snapshot();
        results.sharpe_ratio = sm.sharpe_ratio;
        results.sortino_ratio = sm.sortino_ratio;
        results.win_rate = sm.win_rate;
        results.profit_factor = sm.profit_factor;

        spdlog::info("[BACKTEST] Complete: PnL={:.2f} MaxDD={:.2f}% Sharpe={:.2f} "
                     "Fills={} Ticks={} Speed={:.0f} ticks/s",
                     results.total_pnl, results.max_drawdown * 100.0,
                     results.sharpe_ratio, results.total_fills,
                     results.total_ticks, results.ticks_per_sec);

        return results;
    }

private:
    void run_strategy_tick(const HistoricalTick& tick, BacktestResults& results) {
        if (!ob_.valid()) return;

        double mid = ob_.mid_price();

        // Compute features
        Features features = feature_engine_.compute(ob_, tf_);

        // Regime detection
        regime_detector_.update(features, mid);
        auto& regime = regime_detector_.state();

        // Model inference (use GRU if available, else simple momentum signal)
        ModelOutput prediction;
        if (gru_model_.is_loaded()) {
            prediction = gru_model_.predict(features);
        } else {
            // Simple momentum signal for backtesting without model
            double momentum = features.mid_return_10;
            prediction.probability_up = 0.5 + momentum * 10.0;
            prediction.probability_down = 1.0 - prediction.probability_up;
            prediction.model_confidence = std::abs(momentum) * 20.0;
        }

        // Generate signal
        double threshold = threshold_.current();
        Signal signal;
        signal.regime = regime.current;

        if (prediction.probability_up > threshold &&
            prediction.model_confidence > 0.3) {
            signal.side = Side::Buy;
            signal.confidence = prediction.probability_up;
        } else if (prediction.probability_down > threshold &&
                   prediction.model_confidence > 0.3) {
            signal.side = Side::Sell;
            signal.confidence = prediction.probability_down;
        } else {
            return; // No signal
        }

        ++results.total_signals;

        // Position sizing
        Qty qty = sizer_.compute(signal, regime, portfolio_.snapshot(), risk_);
        if (qty.raw() < lot_size_) return;
        signal.qty = qty;

        // Price
        double spread = ob_.spread();
        double offset = spread * 0.5;
        signal.price = Price(signal.side == Side::Buy
            ? ob_.best_bid() + offset
            : ob_.best_ask() - offset);

        // Risk check
        if (risk_.circuit_breaker().is_tripped()) return;

        ++results.total_orders;

        // Simulate fill
        PaperFillResult fill;
        if (cfg_.use_realistic_fills) {
            fill = fill_sim_.simulate_limit_fill(
                signal.side, signal.price.raw(), signal.qty.raw(),
                ob_, 1'000'000); // 1ms simulated age
        } else {
            fill.filled = true;
            fill.fill_price = signal.price.raw();
            fill.fill_qty = signal.qty.raw();
        }

        if (fill.filled) {
            ++results.total_fills;
            if (fill.partial) ++results.partial_fills;
            results.avg_slippage_bps =
                results.avg_slippage_bps * 0.95 + fill.slippage_bps * 0.05;

            // Apply fill to portfolio
            double fee = fill.fill_price * fill.fill_qty * cfg_.maker_fee_bps * 1e-4;
            total_fees_ += fee;

            Position pos = portfolio_.snapshot();
            if (signal.side == Side::Buy) {
                double new_size = pos.size.raw() + fill.fill_qty;
                double new_entry = (pos.size.raw() * pos.entry_price.raw() +
                                    fill.fill_qty * fill.fill_price) / new_size;
                portfolio_.update_position(Qty(new_size), Price(new_entry), Side::Buy);
            } else {
                double new_size = pos.size.raw() - fill.fill_qty;
                if (new_size < 0) {
                    portfolio_.update_position(Qty(-new_size), Price(fill.fill_price), Side::Sell);
                } else {
                    double pnl = fill.fill_qty * (fill.fill_price - pos.entry_price.raw());
                    portfolio_.add_realized_pnl(Notional(pnl - fee));
                    portfolio_.update_position(Qty(new_size), pos.entry_price, pos.side);
                }
            }

            // Update strategy metrics
            double net = portfolio_.net_pnl().raw();
            strategy_metrics_.on_tick(net, signal.side == Side::Buy ? 1.0 : -1.0, 0.0);
        }
    }

    static AppConfig make_app_config(const BacktestConfig& cfg) {
        AppConfig ac;
        ac.symbol = cfg.symbol;
        ac.risk = cfg.risk_limits;
        ac.circuit_breaker = cfg.cb_config;
        ac.signal_threshold = cfg.signal_threshold;
        ac.tick_size = cfg.tick_size;
        ac.lot_size = cfg.lot_size;
        return ac;
    }

    BacktestConfig cfg_;
    PaperFillSimulator fill_sim_;

    // Strategy components
    OrderBook ob_;
    TradeFlowEngine tf_;
    Portfolio portfolio_;
    AdvancedFeatureEngine feature_engine_;
    GRUModelEngine gru_model_;
    RegimeDetector regime_detector_;
    EnhancedRiskEngine risk_;
    AdaptiveThreshold threshold_;
    AdaptivePositionSizer sizer_;
    StrategyMetrics strategy_metrics_;
    FillProbabilityModel fill_model_;

    double lot_size_ = LOT_SIZE_BTCUSDT;

    // Data
    std::vector<HistoricalTick> data_;
    double total_fees_ = 0.0;
};

} // namespace bybit
