// trading_core_api.cpp — C API implementation wrapping the C++ trading engine
#include "trading_core_api.h"
#include "../app/application.h"
#include "../config/config_loader.h"
#include "../config/types.h"
#include "../orderbook/orderbook.h"
#include "../portfolio/portfolio.h"
#include "../metrics/latency_histogram.h"
#include "../utils/clock.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/callback_sink.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <cstring>
#include <functional>

namespace {

// ─── Internal Engine Wrapper ────────────────────────────────────────────────
struct EngineWrapper {
    bybit::AppConfig config;
    std::unique_ptr<bybit::Application> app;
    std::thread engine_thread;
    std::atomic<TCEngineStatus> status{TC_STATUS_IDLE};

    // C2: Atomic runtime-mutable config — UI thread writes, engine reads
    std::atomic<bool>   rt_paper_trading{true};
    std::atomic<double> rt_signal_threshold{0.6};
    std::atomic<double> rt_order_qty{0.001};
    std::atomic<double> rt_max_position{0.1};

    // Last known features
    std::mutex features_mutex;
    TCFeatures last_features{};

    // Callbacks
    TCOrderBookCallback  ob_callback   = nullptr;   void* ob_ctx   = nullptr;
    TCTradeCallback      trade_callback = nullptr;   void* trade_ctx = nullptr;
    TCPositionCallback   pos_callback  = nullptr;    void* pos_ctx  = nullptr;
    TCSignalCallback     sig_callback  = nullptr;    void* sig_ctx  = nullptr;
    TCLogCallback        log_callback  = nullptr;    void* log_ctx  = nullptr;
    TCStatusCallback     status_callback = nullptr;  void* status_ctx = nullptr;
    TCMetricsCallback    metrics_callback = nullptr; void* metrics_ctx = nullptr;

    void set_status(TCEngineStatus s) {
        status.store(s, std::memory_order_release);
        if (status_callback) {
            status_callback(status_ctx, s);
        }
    }

    void emit_log(TCLogLevel level, const char* msg) {
        if (log_callback) {
            log_callback(log_ctx, level, msg);
        }
    }
};

} // anonymous namespace

// ─── Lifecycle ──────────────────────────────────────────────────────────────

extern "C" TCEngineHandle tc_engine_create(const TCConfig* config) {
    if (!config) return nullptr;

    auto* w = new EngineWrapper();

    // Map C config to C++ config
    auto& cfg = w->config;
    if (config->symbol) cfg.symbol = config->symbol;
    cfg.paper_trading = config->paper_trading;
    cfg.paper_fill_rate = config->paper_fill_rate;

    if (config->ws_public_url) cfg.ws_public_url = config->ws_public_url;
    if (config->ws_private_url) cfg.ws_private_url = config->ws_private_url;
    if (config->ws_ping_interval_sec > 0) cfg.ws_ping_interval_sec = config->ws_ping_interval_sec;

    if (config->rest_base_url) cfg.rest_base_url = config->rest_base_url;
    if (config->rest_timeout_ms > 0) cfg.rest_timeout_ms = config->rest_timeout_ms;

    if (config->order_qty > 0) cfg.order_qty = config->order_qty;
    if (config->signal_threshold > 0) cfg.signal_threshold = config->signal_threshold;
    if (config->signal_ttl_ms > 0) cfg.signal_ttl_ms = config->signal_ttl_ms;
    if (config->entry_offset_bps > 0) cfg.entry_offset_bps = config->entry_offset_bps;

    if (config->max_position_size > 0) cfg.risk.max_position_size = bybit::Qty(config->max_position_size);
    if (config->max_leverage > 0) cfg.risk.max_leverage = config->max_leverage;
    if (config->max_daily_loss > 0) cfg.risk.max_daily_loss = bybit::Notional(config->max_daily_loss);
    if (config->max_drawdown > 0) cfg.risk.max_drawdown = config->max_drawdown;
    if (config->max_orders_per_sec > 0) cfg.risk.max_orders_per_sec = config->max_orders_per_sec;

    cfg.model_bias = config->model_bias;
    if (config->model_weights && config->model_weights_count > 0) {
        int n = std::min(config->model_weights_count, static_cast<int>(cfg.model_weights.size()));
        for (int i = 0; i < n; ++i) {
            cfg.model_weights[i] = config->model_weights[i];
        }
    }

    // AI Edition settings
    if (config->ml_model_path) cfg.ml_model_path = config->ml_model_path;
    cfg.ml_model_enabled = config->ml_model_enabled;
    if (config->onnx_model_path) cfg.onnx_model_path = config->onnx_model_path;
    cfg.onnx_enabled = config->onnx_enabled;
    if (config->onnx_intra_threads > 0) cfg.onnx_intra_threads = config->onnx_intra_threads;
    cfg.adaptive_threshold_enabled = config->adaptive_threshold_enabled;
    if (config->adaptive_threshold_min > 0) cfg.adaptive_threshold_min = config->adaptive_threshold_min;
    if (config->adaptive_threshold_max > 0) cfg.adaptive_threshold_max = config->adaptive_threshold_max;
    cfg.regime_detection_enabled = config->regime_detection_enabled;
    cfg.requote_enabled = config->requote_enabled;
    if (config->requote_interval_ms > 0) cfg.requote_interval_ms = config->requote_interval_ms;
    cfg.fill_prob_enabled = config->fill_prob_enabled;
    if (config->fill_prob_market_threshold > 0) cfg.fill_prob_market_threshold = config->fill_prob_market_threshold;
    cfg.adaptive_sizing_enabled = config->adaptive_sizing_enabled;
    if (config->base_order_qty > 0) cfg.base_order_qty = config->base_order_qty;
    if (config->min_order_qty > 0) cfg.min_order_qty = config->min_order_qty;
    if (config->max_order_qty > 0) cfg.max_order_qty = config->max_order_qty;

    // Circuit breaker
    cfg.circuit_breaker.enabled = config->cb_enabled;
    if (config->cb_loss_threshold > 0) cfg.circuit_breaker.loss_threshold = config->cb_loss_threshold;
    if (config->cb_drawdown_threshold > 0) cfg.circuit_breaker.drawdown_threshold = config->cb_drawdown_threshold;
    if (config->cb_consecutive_losses > 0) cfg.circuit_breaker.consecutive_losses = config->cb_consecutive_losses;
    if (config->cb_cooldown_sec > 0) cfg.circuit_breaker.cooldown_sec = config->cb_cooldown_sec;

    // Recording
    cfg.record_ob_snapshots = config->record_ob_snapshots;
    cfg.record_features = config->record_features;

    if (config->log_dir) cfg.log_dir = config->log_dir;
    if (config->batch_flush_ms > 0) cfg.batch_flush_ms = config->batch_flush_ms;
    if (config->ob_levels > 0) cfg.ob_levels = config->ob_levels;
    if (config->io_threads > 0) cfg.io_threads = config->io_threads;
    if (config->feature_tick_ms > 0) cfg.feature_tick_ms = config->feature_tick_ms;

    return static_cast<TCEngineHandle>(w);
}

extern "C" void tc_engine_destroy(TCEngineHandle engine) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    tc_engine_stop(engine);
    delete w;
}

extern "C" void tc_engine_set_credentials(TCEngineHandle engine,
                                           const char* api_key, const char* api_secret) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (api_key) w->config.api_key = api_key;
    if (api_secret) w->config.api_secret = api_secret;
}

extern "C" bool tc_engine_start(TCEngineHandle engine) {
    if (!engine) return false;
    auto* w = static_cast<EngineWrapper*>(engine);

    auto current = w->status.load(std::memory_order_acquire);
    if (current == TC_STATUS_TRADING || current == TC_STATUS_CONNECTING) {
        return false; // Already running
    }

    try {
        w->app = std::make_unique<bybit::Application>(w->config);

        // Install spdlog callback sink to forward logs to UI
        if (w->log_callback) {
            auto callback_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
                [w](const spdlog::details::log_msg& msg) {
                    TCLogLevel level;
                    switch (msg.level) {
                        case spdlog::level::debug: level = TC_LOG_DEBUG; break;
                        case spdlog::level::warn:  level = TC_LOG_WARN;  break;
                        case spdlog::level::err:
                        case spdlog::level::critical: level = TC_LOG_ERROR; break;
                        default: level = TC_LOG_INFO; break;
                    }
                    std::string text(msg.payload.data(), msg.payload.size());
                    w->emit_log(level, text.c_str());
                }
            );
            auto logger = spdlog::default_logger();
            if (logger) {
                logger->sinks().push_back(callback_sink);
            }
        }

        w->set_status(TC_STATUS_CONNECTING);

        // Run engine on background thread
        w->engine_thread = std::thread([w]() {
            try {
                w->set_status(TC_STATUS_TRADING);
                w->app->run();
                w->set_status(TC_STATUS_IDLE);
            } catch (const std::exception& e) {
                w->emit_log(TC_LOG_ERROR, e.what());
                w->set_status(TC_STATUS_ERROR);
            }
        });

        return true;
    } catch (const std::exception& e) {
        w->emit_log(TC_LOG_ERROR, e.what());
        w->set_status(TC_STATUS_ERROR);
        return false;
    }
}

extern "C" void tc_engine_stop(TCEngineHandle engine) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);

    auto current = w->status.load(std::memory_order_acquire);
    if (current == TC_STATUS_IDLE || current == TC_STATUS_STOPPING) return;

    w->set_status(TC_STATUS_STOPPING);

    if (w->app) {
        w->app->request_stop();
    }

    if (w->engine_thread.joinable()) {
        w->engine_thread.join();
    }

    w->app.reset();
    w->set_status(TC_STATUS_IDLE);
}

// ─── Register Callbacks ─────────────────────────────────────────────────────

extern "C" void tc_engine_set_orderbook_callback(TCEngineHandle engine, TCOrderBookCallback cb, void* ctx) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->ob_callback = cb; w->ob_ctx = ctx;
}

extern "C" void tc_engine_set_trade_callback(TCEngineHandle engine, TCTradeCallback cb, void* ctx) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->trade_callback = cb; w->trade_ctx = ctx;
}

extern "C" void tc_engine_set_position_callback(TCEngineHandle engine, TCPositionCallback cb, void* ctx) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->pos_callback = cb; w->pos_ctx = ctx;
}

extern "C" void tc_engine_set_signal_callback(TCEngineHandle engine, TCSignalCallback cb, void* ctx) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->sig_callback = cb; w->sig_ctx = ctx;
}

extern "C" void tc_engine_set_log_callback(TCEngineHandle engine, TCLogCallback cb, void* ctx) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->log_callback = cb; w->log_ctx = ctx;
}

extern "C" void tc_engine_set_status_callback(TCEngineHandle engine, TCStatusCallback cb, void* ctx) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->status_callback = cb; w->status_ctx = ctx;
}

extern "C" void tc_engine_set_metrics_callback(TCEngineHandle engine, TCMetricsCallback cb, void* ctx) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->metrics_callback = cb; w->metrics_ctx = ctx;
}

// ─── Polling API ────────────────────────────────────────────────────────────

extern "C" TCEngineStatus tc_engine_get_status(TCEngineHandle engine) {
    if (!engine) return TC_STATUS_IDLE;
    return static_cast<EngineWrapper*>(engine)->status.load(std::memory_order_acquire);
}

extern "C" TCOrderBookSummary tc_engine_get_ob_summary(TCEngineHandle engine) {
    TCOrderBookSummary s{};
    if (!engine) return s;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return s;

    const auto& ob = w->app->orderbook();
    s.best_bid = ob.best_bid();
    s.best_ask = ob.best_ask();
    s.mid_price = ob.mid_price();
    s.spread = ob.spread();
    s.microprice = ob.microprice();
    s.bid_count = ob.bid_count();
    s.ask_count = ob.ask_count();
    s.last_update_ns = ob.last_update_ns();
    s.valid = ob.valid();
    return s;
}

extern "C" TCPosition tc_engine_get_position(TCEngineHandle engine) {
    TCPosition p{};
    if (!engine) return p;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return p;

    auto pos = w->app->portfolio().snapshot();
    p.size = pos.size.raw();
    p.entry_price = pos.entry_price.raw();
    p.unrealized_pnl = pos.unrealized_pnl.raw();
    p.realized_pnl = pos.realized_pnl.raw();
    p.funding_impact = pos.funding_impact.raw();
    p.is_long = (pos.side == bybit::Side::Buy);
    return p;
}

extern "C" TCMetricsSnapshot tc_engine_get_metrics(TCEngineHandle engine) {
    TCMetricsSnapshot m{};
    if (!engine) return m;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return m;

    const auto& met = w->app->metrics();
    m.ob_updates = met.ob_updates_total.load(std::memory_order_relaxed);
    m.trades_total = met.trades_total.load(std::memory_order_relaxed);
    m.signals_total = met.signals_total.load(std::memory_order_relaxed);
    m.orders_sent = met.orders_sent_total.load(std::memory_order_relaxed);
    m.orders_filled = met.orders_filled_total.load(std::memory_order_relaxed);
    m.orders_cancelled = met.orders_cancelled_total.load(std::memory_order_relaxed);
    m.ws_reconnects = met.ws_reconnects_total.load(std::memory_order_relaxed);
    m.e2e_latency_p50_ns = met.end_to_end_latency.percentile(0.50);
    m.e2e_latency_p99_ns = met.end_to_end_latency.percentile(0.99);
    m.feat_latency_p50_ns = met.feature_calc_latency.percentile(0.50);
    m.feat_latency_p99_ns = met.feature_calc_latency.percentile(0.99);
    m.model_latency_p50_ns = met.model_inference_latency.percentile(0.50);
    m.model_latency_p99_ns = met.model_inference_latency.percentile(0.99);
    return m;
}

extern "C" TCFeatures tc_engine_get_features(TCEngineHandle engine) {
    TCFeatures f{};
    if (!engine) return f;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return f;

    const auto& feat = w->app->last_features();
    f.imbalance_1 = feat.imbalance_1;
    f.imbalance_5 = feat.imbalance_5;
    f.imbalance_20 = feat.imbalance_20;
    f.ob_slope = feat.ob_slope;
    f.depth_concentration = feat.depth_concentration;
    f.cancel_spike = feat.cancel_spike;
    f.liquidity_wall = feat.liquidity_wall;
    f.aggression_ratio = feat.aggression_ratio;
    f.avg_trade_size = feat.avg_trade_size;
    f.trade_velocity = feat.trade_velocity;
    f.trade_acceleration = feat.trade_acceleration;
    f.volume_accel = feat.volume_accel;
    f.microprice = feat.microprice;
    f.spread_bps = feat.spread_bps;
    f.spread_change_rate = feat.spread_change_rate;
    f.mid_momentum = feat.mid_momentum;
    f.volatility = feat.volatility;
    f.microprice_dev = feat.microprice_dev;
    f.short_term_pressure = feat.short_term_pressure;
    f.bid_depth_total = feat.bid_depth_total;
    f.ask_depth_total = feat.ask_depth_total;
    f.d_imbalance_dt = feat.d_imbalance_dt;
    f.d2_imbalance_dt2 = feat.d2_imbalance_dt2;
    f.d_volatility_dt = feat.d_volatility_dt;
    f.d_momentum_dt = feat.d_momentum_dt;
    f.timestamp_ns = feat.timestamp_ns;
    return f;
}

extern "C" size_t tc_engine_get_bids(TCEngineHandle engine, TCPriceLevel* out, size_t max_levels) {
    if (!engine || !out) return 0;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return 0;

    const auto& ob = w->app->orderbook();
    size_t count = std::min(ob.bid_count(), max_levels);
    const auto* bids = ob.bids();
    for (size_t i = 0; i < count; ++i) {
        out[i].price = bids[i].price;
        out[i].qty = bids[i].qty;
    }
    return count;
}

extern "C" size_t tc_engine_get_asks(TCEngineHandle engine, TCPriceLevel* out, size_t max_levels) {
    if (!engine || !out) return 0;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return 0;

    const auto& ob = w->app->orderbook();
    size_t count = std::min(ob.ask_count(), max_levels);
    const auto* asks = ob.asks();
    for (size_t i = 0; i < count; ++i) {
        out[i].price = asks[i].price;
        out[i].qty = asks[i].qty;
    }
    return count;
}

// ─── Runtime Config Changes ─────────────────────────────────────────────────

extern "C" void tc_engine_set_paper_mode(TCEngineHandle engine, bool paper) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->rt_paper_trading.store(paper, std::memory_order_release);
    // Also update config for next restart
    w->config.paper_trading = paper;
    // Propagate to live engine if running
}

extern "C" void tc_engine_set_signal_threshold(TCEngineHandle engine, double threshold) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->rt_signal_threshold.store(threshold, std::memory_order_release);
    w->config.signal_threshold = threshold;
}

extern "C" void tc_engine_set_order_qty(TCEngineHandle engine, double qty) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->rt_order_qty.store(qty, std::memory_order_release);
    w->config.order_qty = qty;
}

extern "C" void tc_engine_set_max_position(TCEngineHandle engine, double max_pos) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    w->rt_max_position.store(max_pos, std::memory_order_release);
    w->config.risk.max_position_size = bybit::Qty(max_pos);
}

// ─── AI Edition Polling ─────────────────────────────────────────────────────

extern "C" TCRegimeState tc_engine_get_regime(TCEngineHandle engine) {
    TCRegimeState r{};
    if (!engine) return r;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return r;

    const auto& rs = w->app->regime_state();
    r.current_regime = static_cast<int>(rs.current);
    r.previous_regime = static_cast<int>(rs.previous);
    r.confidence = rs.confidence;
    r.volatility = rs.volatility;
    r.trend_score = rs.trend_score;
    r.mr_score = rs.mr_score;
    r.liq_score = rs.liq_score;
    r.regime_start_ns = rs.regime_start_ns;
    return r;
}

extern "C" TCModelPrediction tc_engine_get_prediction(TCEngineHandle engine) {
    TCModelPrediction p{};
    if (!engine) return p;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return p;

    const auto& pred = w->app->last_prediction();
    p.h100ms_up = pred.horizons[0].prob_up;
    p.h100ms_down = pred.horizons[0].prob_down;
    p.h100ms_flat = pred.horizons[0].prob_flat;
    p.h100ms_move = pred.horizons[0].predicted_move_bps;
    p.h500ms_up = pred.horizons[1].prob_up;
    p.h500ms_down = pred.horizons[1].prob_down;
    p.h500ms_flat = pred.horizons[1].prob_flat;
    p.h500ms_move = pred.horizons[1].predicted_move_bps;
    p.h1s_up = pred.horizons[2].prob_up;
    p.h1s_down = pred.horizons[2].prob_down;
    p.h1s_flat = pred.horizons[2].prob_flat;
    p.h1s_move = pred.horizons[2].predicted_move_bps;
    p.h3s_up = pred.horizons[3].prob_up;
    p.h3s_down = pred.horizons[3].prob_down;
    p.h3s_flat = pred.horizons[3].prob_flat;
    p.h3s_move = pred.horizons[3].predicted_move_bps;
    p.probability_up = pred.probability_up;
    p.probability_down = pred.probability_down;
    p.model_confidence = pred.model_confidence;
    p.inference_latency_ns = pred.inference_latency_ns;
    return p;
}

extern "C" TCAdaptiveThresholdState tc_engine_get_threshold_state(TCEngineHandle engine) {
    TCAdaptiveThresholdState t{};
    if (!engine) return t;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return t;

    const auto& ts = w->app->threshold_state();
    t.current_threshold = ts.current_threshold;
    t.base_threshold = ts.base_threshold;
    t.volatility_adj = ts.volatility_adj;
    t.accuracy_adj = ts.accuracy_adj;
    t.liquidity_adj = ts.liquidity_adj;
    t.spread_adj = ts.spread_adj;
    t.recent_accuracy = ts.recent_accuracy;
    t.total_signals = ts.total_count;
    t.correct_signals = ts.correct_count;
    return t;
}

extern "C" TCCircuitBreakerState tc_engine_get_circuit_breaker(TCEngineHandle engine) {
    TCCircuitBreakerState cb{};
    if (!engine) return cb;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return cb;

    cb.tripped = w->app->circuit_breaker_tripped();
    cb.in_cooldown = cb.tripped; // simplified
    cb.drawdown_pct = w->app->current_drawdown() * 100.0;
    // C5: Safe copy trip_reason into fixed buffer
    std::strncpy(cb.trip_reason, cb.tripped ? "drawdown" : "", sizeof(cb.trip_reason) - 1);
    cb.trip_reason[sizeof(cb.trip_reason) - 1] = '\0';
    return cb;
}

extern "C" TCAccuracyMetrics tc_engine_get_accuracy(TCEngineHandle engine) {
    TCAccuracyMetrics a{};
    if (!engine) return a;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return a;

    const auto& acc = w->app->accuracy_metrics();
    a.accuracy = acc.accuracy;
    a.total_predictions = acc.total_predictions;
    a.correct_predictions = acc.correct_predictions;
    a.precision_up = acc.per_class[0].precision;
    a.precision_down = acc.per_class[1].precision;
    a.precision_flat = acc.per_class[2].precision;
    a.recall_up = acc.per_class[0].recall;
    a.recall_down = acc.per_class[1].recall;
    a.recall_flat = acc.per_class[2].recall;
    a.f1_up = acc.per_class[0].f1_score;
    a.f1_down = acc.per_class[1].f1_score;
    a.f1_flat = acc.per_class[2].f1_score;
    a.rolling_accuracy = acc.rolling_accuracy;
    a.rolling_window = acc.rolling_window;
    a.horizon_accuracy_100ms = acc.horizon_accuracy[0];
    a.horizon_accuracy_500ms = acc.horizon_accuracy[1];
    a.horizon_accuracy_1s = acc.horizon_accuracy[2];
    a.horizon_accuracy_3s = acc.horizon_accuracy[3];
    a.calibration_error = acc.calibration_error;
    a.using_onnx = w->app->using_onnx();
    return a;
}

extern "C" TCStrategyMetrics tc_engine_get_strategy_metrics(TCEngineHandle engine) {
    TCStrategyMetrics m{};
    if (!engine) return m;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return m;

    const auto& sm = w->app->strategy_metrics();
    m.sharpe_ratio = sm.sharpe_ratio;
    m.sortino_ratio = sm.sortino_ratio;
    m.max_drawdown_pct = sm.max_drawdown_pct;
    m.current_drawdown = sm.current_drawdown;
    m.profit_factor = sm.profit_factor;
    m.win_rate = sm.win_rate;
    m.avg_win = sm.avg_win;
    m.avg_loss = sm.avg_loss;
    m.expectancy = sm.expectancy;
    m.total_pnl = sm.total_pnl;
    m.best_trade = sm.best_trade;
    m.worst_trade = sm.worst_trade;
    m.total_trades = sm.total_trades;
    m.winning_trades = sm.winning_trades;
    m.losing_trades = sm.losing_trades;
    m.consecutive_wins = sm.consecutive_wins;
    m.consecutive_losses = sm.consecutive_losses;
    m.max_consecutive_wins = sm.max_consecutive_wins;
    m.max_consecutive_losses = sm.max_consecutive_losses;
    m.daily_pnl = sm.daily_pnl;
    m.hourly_pnl = sm.hourly_pnl;
    m.calmar_ratio = sm.calmar_ratio;
    m.recovery_factor = sm.recovery_factor;
    return m;
}

extern "C" TCStrategyHealth tc_engine_get_strategy_health(TCEngineHandle engine) {
    TCStrategyHealth h{};
    if (!engine) return h;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return h;

    const auto& sh = w->app->strategy_health();
    h.health_level = static_cast<int>(sh.level);
    h.health_score = sh.health_score;
    h.activity_scale = sh.activity_scale;
    h.threshold_offset = sh.threshold_offset;
    h.accuracy_score = sh.accuracy_score;
    h.pnl_score = sh.pnl_score;
    h.drawdown_score = sh.drawdown_score;
    h.sharpe_score = sh.sharpe_score;
    h.consistency_score = sh.consistency_score;
    h.fill_rate_score = sh.fill_rate_score;
    h.accuracy_declining = sh.accuracy_declining;
    h.pnl_declining = sh.pnl_declining;
    h.drawdown_warning = sh.drawdown_warning;
    h.regime_changes_1h = sh.regime_changes_1h;
    return h;
}

extern "C" TCSystemMonitor tc_engine_get_system_monitor(TCEngineHandle engine) {
    TCSystemMonitor s{};
    if (!engine) return s;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return s;

    const auto& sm = w->app->system_monitor();
    s.cpu_usage_pct = sm.cpu_usage_pct;
    s.memory_used_mb = sm.memory_used_mb;
    s.memory_peak_bytes = sm.memory_peak_bytes;
    s.cpu_cores = sm.cpu_cores;
    s.ws_latency_p50_us = sm.ws_latency_p50_us;
    s.ws_latency_p99_us = sm.ws_latency_p99_us;
    s.feat_latency_p50_us = sm.feat_latency_p50_us;
    s.feat_latency_p99_us = sm.feat_latency_p99_us;
    s.model_latency_p50_us = sm.model_latency_p50_us;
    s.model_latency_p99_us = sm.model_latency_p99_us;
    s.e2e_latency_p50_us = sm.e2e_latency_p50_us;
    s.e2e_latency_p99_us = sm.e2e_latency_p99_us;
    s.exchange_latency_ms = sm.exchange_latency_ms;
    s.ticks_per_sec = sm.ticks_per_sec;
    s.signals_per_sec = sm.signals_per_sec;
    s.orders_per_sec = sm.orders_per_sec;
    s.uptime_hours = sm.uptime_hours;
    s.gpu_available = sm.gpu_available;
    s.gpu_usage_pct = sm.gpu_usage_pct;
    // C5: Safe copy into fixed-size buffers
    std::strncpy(s.gpu_name, sm.gpu_name, sizeof(s.gpu_name) - 1);
    s.gpu_name[sizeof(s.gpu_name) - 1] = '\0';
    const char* backend = w->app->inference_backend_name();
    if (backend) {
        std::strncpy(s.inference_backend, backend, sizeof(s.inference_backend) - 1);
        s.inference_backend[sizeof(s.inference_backend) - 1] = '\0';
    }
    return s;
}

extern "C" TCRLState tc_engine_get_rl_state(TCEngineHandle engine) {
    TCRLState r{};
    if (!engine) return r;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return r;

    const auto& rl = w->app->rl_state();
    r.signal_threshold_delta = rl.current_action.signal_threshold_delta;
    r.position_size_scale = rl.current_action.position_size_scale;
    r.order_offset_bps = rl.current_action.order_offset_bps;
    r.requote_freq_scale = rl.current_action.requote_freq_scale;
    r.avg_reward = rl.avg_reward;
    r.value_estimate = rl.value_estimate;
    r.policy_loss = rl.policy_loss;
    r.value_loss = rl.value_loss;
    r.total_steps = rl.total_steps;
    r.total_updates = rl.total_updates;
    r.exploring = rl.exploring;
    return r;
}

extern "C" TCFeatureImportance tc_engine_get_feature_importance(TCEngineHandle engine) {
    TCFeatureImportance fi{};
    if (!engine) return fi;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return fi;

    const auto& snap = w->app->feature_importance();
    for (size_t i = 0; i < 25 && i < bybit::FEATURE_COUNT; ++i) {
        fi.permutation_importance[i] = snap.scores[i].permutation_importance;
        fi.mutual_information[i] = snap.scores[i].mutual_information;
        fi.shap_value[i] = snap.scores[i].shap_value;
        fi.correlation[i] = snap.scores[i].correlation;
        fi.ranking[i] = snap.ranking[i];
    }
    fi.active_features = snap.active_features;
    return fi;
}

extern "C" void tc_engine_reset_circuit_breaker(TCEngineHandle engine) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (w->app) w->app->reset_circuit_breaker();
}

extern "C" void tc_engine_emergency_stop(TCEngineHandle engine) {
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (w->app) {
        w->app->emergency_stop();
    }
    // Also do a normal stop to join threads
    tc_engine_stop(engine);
}

extern "C" bool tc_engine_reload_model(TCEngineHandle engine, const char* model_path) {
    if (!engine || !model_path) return false;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return false;
    return w->app->reload_model(model_path);
}

// ─── Snapshot-based Polling ──────────────────────────────────────────────────

static void convert_snapshot(const bybit::UISnapshot& src, TCFullSnapshot* dst) {
    // OrderBook
    for (int i = 0; i < src.bid_count && i < 20; ++i) {
        dst->bids[i].price = src.bids[i].price;
        dst->bids[i].qty   = src.bids[i].qty;
    }
    for (int i = 0; i < src.ask_count && i < 20; ++i) {
        dst->asks[i].price = src.asks[i].price;
        dst->asks[i].qty   = src.asks[i].qty;
    }
    dst->bid_count = src.bid_count;
    dst->ask_count = src.ask_count;
    dst->best_bid = src.best_bid;
    dst->best_ask = src.best_ask;
    dst->mid_price = src.mid_price;
    dst->spread = src.spread;
    dst->microprice = src.microprice;
    dst->ob_last_update_ns = src.ob_last_update;
    dst->ob_valid = src.ob_valid;

    // Position
    dst->pos_size = src.pos_size;
    dst->pos_entry = src.pos_entry;
    dst->pos_unrealized = src.pos_unrealized;
    dst->pos_realized = src.pos_realized;
    dst->pos_funding = src.pos_funding;
    dst->pos_is_long = src.pos_is_long;

    // Metrics
    dst->ob_updates = src.ob_updates;
    dst->trades_total = src.trades_total;
    dst->signals_total = src.signals_total;
    dst->orders_sent = src.orders_sent;
    dst->orders_filled = src.orders_filled;
    dst->orders_cancelled = src.orders_cancelled;
    dst->ws_reconnects = src.ws_reconnects;
    dst->e2e_p50_ns = src.e2e_p50_ns;
    dst->e2e_p99_ns = src.e2e_p99_ns;
    dst->feat_p50_ns = src.feat_p50_ns;
    dst->feat_p99_ns = src.feat_p99_ns;
    dst->model_p50_ns = src.model_p50_ns;
    dst->model_p99_ns = src.model_p99_ns;

    // Features
    const auto& f = src.features;
    auto& tf = dst->features;
    tf.imbalance_1 = f.imbalance_1;
    tf.imbalance_5 = f.imbalance_5;
    tf.imbalance_20 = f.imbalance_20;
    tf.ob_slope = f.ob_slope;
    tf.depth_concentration = f.depth_concentration;
    tf.cancel_spike = f.cancel_spike;
    tf.liquidity_wall = f.liquidity_wall;
    tf.aggression_ratio = f.aggression_ratio;
    tf.avg_trade_size = f.avg_trade_size;
    tf.trade_velocity = f.trade_velocity;
    tf.trade_acceleration = f.trade_acceleration;
    tf.volume_accel = f.volume_accel;
    tf.microprice = f.microprice;
    tf.spread_bps = f.spread_bps;
    tf.spread_change_rate = f.spread_change_rate;
    tf.mid_momentum = f.mid_momentum;
    tf.volatility = f.volatility;
    tf.microprice_dev = f.microprice_dev;
    tf.short_term_pressure = f.short_term_pressure;
    tf.bid_depth_total = f.bid_depth_total;
    tf.ask_depth_total = f.ask_depth_total;
    tf.d_imbalance_dt = f.d_imbalance_dt;
    tf.d2_imbalance_dt2 = f.d2_imbalance_dt2;
    tf.d_volatility_dt = f.d_volatility_dt;
    tf.d_momentum_dt = f.d_momentum_dt;
    tf.timestamp_ns = f.timestamp_ns;

    // Regime
    const auto& rs = src.regime;
    dst->regime.current_regime = static_cast<int>(rs.current);
    dst->regime.previous_regime = static_cast<int>(rs.previous);
    dst->regime.confidence = rs.confidence;
    dst->regime.volatility = rs.volatility;
    dst->regime.trend_score = rs.trend_score;
    dst->regime.mr_score = rs.mr_score;
    dst->regime.liq_score = rs.liq_score;
    dst->regime.regime_start_ns = rs.regime_start_ns;

    // Prediction
    const auto& pred = src.prediction;
    dst->prediction.h100ms_up = pred.horizons[0].prob_up;
    dst->prediction.h100ms_down = pred.horizons[0].prob_down;
    dst->prediction.h100ms_flat = pred.horizons[0].prob_flat;
    dst->prediction.h100ms_move = pred.horizons[0].predicted_move_bps;
    dst->prediction.h500ms_up = pred.horizons[1].prob_up;
    dst->prediction.h500ms_down = pred.horizons[1].prob_down;
    dst->prediction.h500ms_flat = pred.horizons[1].prob_flat;
    dst->prediction.h500ms_move = pred.horizons[1].predicted_move_bps;
    dst->prediction.h1s_up = pred.horizons[2].prob_up;
    dst->prediction.h1s_down = pred.horizons[2].prob_down;
    dst->prediction.h1s_flat = pred.horizons[2].prob_flat;
    dst->prediction.h1s_move = pred.horizons[2].predicted_move_bps;
    dst->prediction.h3s_up = pred.horizons[3].prob_up;
    dst->prediction.h3s_down = pred.horizons[3].prob_down;
    dst->prediction.h3s_flat = pred.horizons[3].prob_flat;
    dst->prediction.h3s_move = pred.horizons[3].predicted_move_bps;
    dst->prediction.probability_up = pred.probability_up;
    dst->prediction.probability_down = pred.probability_down;
    dst->prediction.model_confidence = pred.model_confidence;
    dst->prediction.inference_latency_ns = pred.inference_latency_ns;

    // Threshold
    const auto& ts = src.threshold;
    dst->threshold.current_threshold = ts.current_threshold;
    dst->threshold.base_threshold = ts.base_threshold;
    dst->threshold.volatility_adj = ts.volatility_adj;
    dst->threshold.accuracy_adj = ts.accuracy_adj;
    dst->threshold.liquidity_adj = ts.liquidity_adj;
    dst->threshold.spread_adj = ts.spread_adj;
    dst->threshold.recent_accuracy = ts.recent_accuracy;
    dst->threshold.total_signals = ts.total_count;
    dst->threshold.correct_signals = ts.correct_count;

    // Circuit Breaker
    dst->cb_tripped = src.cb_tripped;
    dst->cb_cooldown = src.cb_cooldown;
    dst->cb_consec_losses = src.cb_consec_losses;
    dst->cb_peak_pnl = src.cb_peak_pnl;
    dst->cb_drawdown = src.cb_drawdown;

    // Accuracy
    const auto& acc = src.accuracy;
    dst->accuracy.accuracy = acc.accuracy;
    dst->accuracy.total_predictions = acc.total_predictions;
    dst->accuracy.correct_predictions = acc.correct_predictions;
    dst->accuracy.precision_up = acc.per_class[0].precision;
    dst->accuracy.precision_down = acc.per_class[1].precision;
    dst->accuracy.precision_flat = acc.per_class[2].precision;
    dst->accuracy.recall_up = acc.per_class[0].recall;
    dst->accuracy.recall_down = acc.per_class[1].recall;
    dst->accuracy.recall_flat = acc.per_class[2].recall;
    dst->accuracy.f1_up = acc.per_class[0].f1_score;
    dst->accuracy.f1_down = acc.per_class[1].f1_score;
    dst->accuracy.f1_flat = acc.per_class[2].f1_score;
    dst->accuracy.rolling_accuracy = acc.rolling_accuracy;
    dst->accuracy.rolling_window = acc.rolling_window;
    dst->accuracy.horizon_accuracy_100ms = acc.horizon_accuracy[0];
    dst->accuracy.horizon_accuracy_500ms = acc.horizon_accuracy[1];
    dst->accuracy.horizon_accuracy_1s = acc.horizon_accuracy[2];
    dst->accuracy.horizon_accuracy_3s = acc.horizon_accuracy[3];
    dst->accuracy.calibration_error = acc.calibration_error;
    dst->accuracy.using_onnx = src.using_onnx;
    dst->using_onnx = src.using_onnx;

    // Strategy Metrics
    const auto& sm = src.strategy_metrics;
    dst->strategy_metrics.sharpe_ratio = sm.sharpe_ratio;
    dst->strategy_metrics.sortino_ratio = sm.sortino_ratio;
    dst->strategy_metrics.max_drawdown_pct = sm.max_drawdown_pct;
    dst->strategy_metrics.current_drawdown = sm.current_drawdown;
    dst->strategy_metrics.profit_factor = sm.profit_factor;
    dst->strategy_metrics.win_rate = sm.win_rate;
    dst->strategy_metrics.avg_win = sm.avg_win;
    dst->strategy_metrics.avg_loss = sm.avg_loss;
    dst->strategy_metrics.expectancy = sm.expectancy;
    dst->strategy_metrics.total_pnl = sm.total_pnl;
    dst->strategy_metrics.best_trade = sm.best_trade;
    dst->strategy_metrics.worst_trade = sm.worst_trade;
    dst->strategy_metrics.total_trades = sm.total_trades;
    dst->strategy_metrics.winning_trades = sm.winning_trades;
    dst->strategy_metrics.losing_trades = sm.losing_trades;
    dst->strategy_metrics.consecutive_wins = sm.consecutive_wins;
    dst->strategy_metrics.consecutive_losses = sm.consecutive_losses;
    dst->strategy_metrics.max_consecutive_wins = sm.max_consecutive_wins;
    dst->strategy_metrics.max_consecutive_losses = sm.max_consecutive_losses;
    dst->strategy_metrics.daily_pnl = sm.daily_pnl;
    dst->strategy_metrics.hourly_pnl = sm.hourly_pnl;
    dst->strategy_metrics.calmar_ratio = sm.calmar_ratio;
    dst->strategy_metrics.recovery_factor = sm.recovery_factor;

    // Strategy Health
    const auto& sh = src.strategy_health;
    dst->strategy_health.health_level = static_cast<int>(sh.level);
    dst->strategy_health.health_score = sh.health_score;
    dst->strategy_health.activity_scale = sh.activity_scale;
    dst->strategy_health.threshold_offset = sh.threshold_offset;
    dst->strategy_health.accuracy_score = sh.accuracy_score;
    dst->strategy_health.pnl_score = sh.pnl_score;
    dst->strategy_health.drawdown_score = sh.drawdown_score;
    dst->strategy_health.sharpe_score = sh.sharpe_score;
    dst->strategy_health.consistency_score = sh.consistency_score;
    dst->strategy_health.fill_rate_score = sh.fill_rate_score;
    dst->strategy_health.accuracy_declining = sh.accuracy_declining;
    dst->strategy_health.pnl_declining = sh.pnl_declining;
    dst->strategy_health.drawdown_warning = sh.drawdown_warning;
    dst->strategy_health.regime_changes_1h = sh.regime_changes_1h;

    // System Monitor
    const auto& smon = src.system_monitor;
    dst->system_monitor.cpu_usage_pct = smon.cpu_usage_pct;
    dst->system_monitor.memory_used_mb = smon.memory_used_mb;
    dst->system_monitor.memory_peak_bytes = smon.memory_peak_bytes;
    dst->system_monitor.cpu_cores = smon.cpu_cores;
    dst->system_monitor.ws_latency_p50_us = smon.ws_latency_p50_us;
    dst->system_monitor.ws_latency_p99_us = smon.ws_latency_p99_us;
    dst->system_monitor.feat_latency_p50_us = smon.feat_latency_p50_us;
    dst->system_monitor.feat_latency_p99_us = smon.feat_latency_p99_us;
    dst->system_monitor.model_latency_p50_us = smon.model_latency_p50_us;
    dst->system_monitor.model_latency_p99_us = smon.model_latency_p99_us;
    dst->system_monitor.e2e_latency_p50_us = smon.e2e_latency_p50_us;
    dst->system_monitor.e2e_latency_p99_us = smon.e2e_latency_p99_us;
    dst->system_monitor.exchange_latency_ms = smon.exchange_latency_ms;
    dst->system_monitor.ticks_per_sec = smon.ticks_per_sec;
    dst->system_monitor.signals_per_sec = smon.signals_per_sec;
    dst->system_monitor.orders_per_sec = smon.orders_per_sec;
    dst->system_monitor.uptime_hours = smon.uptime_hours;
    dst->system_monitor.gpu_available = smon.gpu_available;
    dst->system_monitor.gpu_usage_pct = smon.gpu_usage_pct;
    // C5: Safe copy into fixed buffers — no dangling pointers
    std::strncpy(dst->system_monitor.gpu_name, smon.gpu_name, sizeof(dst->system_monitor.gpu_name) - 1);
    dst->system_monitor.gpu_name[sizeof(dst->system_monitor.gpu_name) - 1] = '\0';
    std::strncpy(dst->system_monitor.inference_backend, src.inference_backend, sizeof(dst->system_monitor.inference_backend) - 1);
    dst->system_monitor.inference_backend[sizeof(dst->system_monitor.inference_backend) - 1] = '\0';

    // RL State
    const auto& rl = src.rl_state;
    dst->rl_state.signal_threshold_delta = rl.current_action.signal_threshold_delta;
    dst->rl_state.position_size_scale = rl.current_action.position_size_scale;
    dst->rl_state.order_offset_bps = rl.current_action.order_offset_bps;
    dst->rl_state.requote_freq_scale = rl.current_action.requote_freq_scale;
    dst->rl_state.avg_reward = rl.avg_reward;
    dst->rl_state.value_estimate = rl.value_estimate;
    dst->rl_state.policy_loss = rl.policy_loss;
    dst->rl_state.value_loss = rl.value_loss;
    dst->rl_state.total_steps = rl.total_steps;
    dst->rl_state.total_updates = rl.total_updates;
    dst->rl_state.exploring = rl.exploring;

    // Feature Importance
    const auto& fi = src.feature_importance;
    for (size_t i = 0; i < 25 && i < bybit::FEATURE_COUNT; ++i) {
        dst->feature_importance.permutation_importance[i] = fi.scores[i].permutation_importance;
        dst->feature_importance.mutual_information[i] = fi.scores[i].mutual_information;
        dst->feature_importance.shap_value[i] = fi.scores[i].shap_value;
        dst->feature_importance.correlation[i] = fi.scores[i].correlation;
        dst->feature_importance.ranking[i] = fi.ranking[i];
    }
    dst->feature_importance.active_features = fi.active_features;

    // E7: Control Plane state
    dst->risk_state = src.risk_state;
    dst->exec_state = src.exec_state;
    dst->system_mode = src.system_mode;
    dst->ctrl_position_scale = src.ctrl_position_scale;
    dst->ctrl_throttle_factor = src.ctrl_throttle_factor;
    dst->ctrl_allows_new_orders = src.ctrl_allows_new_orders;
    dst->ctrl_allows_increase = src.ctrl_allows_increase;
    dst->ctrl_total_transitions = src.ctrl_total_transitions;
    dst->ctrl_audit_depth = src.ctrl_audit_depth;

    // Engine state
    std::strncpy(dst->inference_backend, src.inference_backend, sizeof(dst->inference_backend) - 1);
    dst->inference_backend[sizeof(dst->inference_backend) - 1] = '\0';
    dst->engine_running = src.engine_running;
    dst->ws_rtt_us = src.ws_rtt_us;  // E5: WebSocket RTT
    dst->snapshot_ns = src.snapshot_ns;
}

extern "C" void tc_engine_get_snapshot(TCEngineHandle engine, TCFullSnapshot* out) {
    if (!out) return;
    std::memset(out, 0, sizeof(TCFullSnapshot));
    if (!engine) return;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return;

    auto snap = w->app->ui_snapshot();
    convert_snapshot(snap, out);
    out->snapshot_version = w->app->snapshot_version();
}

extern "C" uint64_t tc_engine_snapshot_version(TCEngineHandle engine) {
    if (!engine) return 0;
    auto* w = static_cast<EngineWrapper*>(engine);
    if (!w->app) return 0;
    return w->app->snapshot_version();
}

// ─── v2.4: Chaos Engine Stubs ──────────────────────────────────────────────

extern "C" TCChaosState tc_engine_get_chaos_state(TCEngineHandle engine) {
    TCChaosState s{};
    if (!engine) return s;
    // TODO: wire to ChaosEngine when integrated into Application
    return s;
}

extern "C" void tc_engine_set_chaos_enabled(TCEngineHandle engine, bool enabled) {
    if (!engine) return;
    (void)enabled;
}

extern "C" void tc_engine_set_chaos_nightly(TCEngineHandle engine) {
    if (!engine) return;
}

extern "C" void tc_engine_set_chaos_flash_crash(TCEngineHandle engine) {
    if (!engine) return;
}

extern "C" void tc_engine_reset_chaos_stats(TCEngineHandle engine) {
    if (!engine) return;
}

// ─── v2.4: Deterministic Replay Stubs ──────────────────────────────────────

extern "C" TCReplayState tc_engine_get_replay_state(TCEngineHandle engine) {
    TCReplayState s{};
    // C5: loaded_file is now a char[256] buffer, zero-initialized by {}
    if (!engine) return s;
    return s;
}

extern "C" bool tc_engine_load_replay(TCEngineHandle engine, const char* path) {
    if (!engine || !path) return false;
    return false;
}

extern "C" void tc_engine_start_replay(TCEngineHandle engine) {
    if (!engine) return;
}

extern "C" void tc_engine_stop_replay(TCEngineHandle engine) {
    if (!engine) return;
}

extern "C" void tc_engine_set_replay_speed(TCEngineHandle engine, double speed) {
    if (!engine) return;
    (void)speed;
}

extern "C" void tc_engine_step_replay(TCEngineHandle engine) {
    if (!engine) return;
}

// ─── v2.4: VaR Engine Stubs ───────────────────────────────────────────────

extern "C" TCVaRSnapshot tc_engine_get_var(TCEngineHandle engine) {
    TCVaRSnapshot s{};
    if (!engine) return s;
    return s;
}

// ─── v2.4: Order State Machine Stubs ──────────────────────────────────────

extern "C" TCOSMSummary tc_engine_get_osm_summary(TCEngineHandle engine) {
    TCOSMSummary s{};
    if (!engine) return s;
    return s;
}

// ─── v2.4: RL v2 Extended Stubs ───────────────────────────────────────────

extern "C" TCRLv2State tc_engine_get_rl_v2_state(TCEngineHandle engine) {
    TCRLv2State s{};
    if (!engine) return s;
    return s;
}

// ─── v2.4: Per-Stage Histograms Stubs ─────────────────────────────────────

extern "C" TCStageHistogramArray tc_engine_get_stage_histograms(TCEngineHandle engine) {
    TCStageHistogramArray arr{};
    if (!engine) return arr;
    return arr;
}

extern "C" bool tc_engine_export_flamegraph(TCEngineHandle engine, const char* path) {
    if (!engine || !path) return false;
    return false;
}

extern "C" bool tc_engine_export_histograms_csv(TCEngineHandle engine, const char* path) {
    if (!engine || !path) return false;
    return false;
}
