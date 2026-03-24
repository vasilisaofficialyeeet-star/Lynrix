// trading_core_api.h — Pure C API for Swift/Objective-C interop
// No C++ types exposed. Opaque handle + callbacks.
#ifndef TRADING_CORE_API_H
#define TRADING_CORE_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── Opaque Handle ──────────────────────────────────────────────────────────
typedef void* TCEngineHandle;

// ─── Data Structures (C-safe) ───────────────────────────────────────────────

typedef struct {
    double price;
    double qty;
} TCPriceLevel;

typedef struct {
    uint64_t timestamp_ns;
    double   price;
    double   qty;
    bool     is_buyer_maker;
} TCTrade;

typedef struct {
    double size;
    double entry_price;
    double unrealized_pnl;
    double realized_pnl;
    double funding_impact;
    bool   is_long;
} TCPosition;

typedef struct {
    // Order book features (7)
    double imbalance_1;
    double imbalance_5;
    double imbalance_20;
    double ob_slope;
    double depth_concentration;
    double cancel_spike;
    double liquidity_wall;
    // Trade flow features (5)
    double aggression_ratio;
    double avg_trade_size;
    double trade_velocity;
    double trade_acceleration;
    double volume_accel;
    // Price features (5)
    double microprice;
    double spread_bps;
    double spread_change_rate;
    double mid_momentum;
    double volatility;
    // Derived features (4)
    double microprice_dev;
    double short_term_pressure;
    double bid_depth_total;
    double ask_depth_total;
    // Temporal derivatives (4)
    double d_imbalance_dt;
    double d2_imbalance_dt2;
    double d_volatility_dt;
    double d_momentum_dt;
    uint64_t timestamp_ns;
} TCFeatures;

typedef struct {
    bool   is_buy;
    double price;
    double qty;
    double confidence;
    uint64_t timestamp_ns;
    int    regime;
    double fill_prob;
    double expected_pnl;
    double adaptive_threshold;
} TCSignal;

// ─── AI Extension Structures ────────────────────────────────────────────────

typedef struct {
    int    current_regime;       // 0=LowVol,1=HighVol,2=Trending,3=MeanRev,4=LiqVacuum
    int    previous_regime;
    double confidence;
    double volatility;
    double trend_score;
    double mr_score;
    double liq_score;
    uint64_t regime_start_ns;
} TCRegimeState;

typedef struct {
    // Per-horizon: prob_up, prob_down, prob_flat, predicted_move_bps
    double h100ms_up, h100ms_down, h100ms_flat, h100ms_move;
    double h500ms_up, h500ms_down, h500ms_flat, h500ms_move;
    double h1s_up,    h1s_down,    h1s_flat,    h1s_move;
    double h3s_up,    h3s_down,    h3s_flat,    h3s_move;
    // Primary prediction
    double probability_up;
    double probability_down;
    double model_confidence;
    uint64_t inference_latency_ns;
} TCModelPrediction;

typedef struct {
    double current_threshold;
    double base_threshold;
    double volatility_adj;
    double accuracy_adj;
    double liquidity_adj;
    double spread_adj;
    double recent_accuracy;
    int    total_signals;
    int    correct_signals;
} TCAdaptiveThresholdState;

typedef struct {
    bool   tripped;
    bool   in_cooldown;
    char   trip_reason[64];
    int    consecutive_losses;
    double peak_pnl;
    double drawdown_pct;
} TCCircuitBreakerState;

typedef struct {
    // Overall
    double accuracy;
    int    total_predictions;
    int    correct_predictions;
    // Per-class precision/recall (0=up, 1=down, 2=flat)
    double precision_up, precision_down, precision_flat;
    double recall_up, recall_down, recall_flat;
    double f1_up, f1_down, f1_flat;
    // Rolling
    double rolling_accuracy;
    int    rolling_window;
    // Per-horizon accuracy
    double horizon_accuracy_100ms;
    double horizon_accuracy_500ms;
    double horizon_accuracy_1s;
    double horizon_accuracy_3s;
    // Calibration
    double calibration_error;
    // ML backend info
    bool   using_onnx;
} TCAccuracyMetrics;

typedef struct {
    double sharpe_ratio;
    double sortino_ratio;
    double max_drawdown_pct;
    double current_drawdown;
    double profit_factor;
    double win_rate;
    double avg_win;
    double avg_loss;
    double expectancy;
    double total_pnl;
    double best_trade;
    double worst_trade;
    int    total_trades;
    int    winning_trades;
    int    losing_trades;
    int    consecutive_wins;
    int    consecutive_losses;
    int    max_consecutive_wins;
    int    max_consecutive_losses;
    double daily_pnl;
    double hourly_pnl;
    double calmar_ratio;
    double recovery_factor;
} TCStrategyMetrics;

typedef struct {
    int    health_level;         // 0=Excellent..4=Halted
    double health_score;
    double activity_scale;
    double threshold_offset;
    double accuracy_score;
    double pnl_score;
    double drawdown_score;
    double sharpe_score;
    double consistency_score;
    double fill_rate_score;
    bool   accuracy_declining;
    bool   pnl_declining;
    bool   drawdown_warning;
    int    regime_changes_1h;
} TCStrategyHealth;

typedef struct {
    double cpu_usage_pct;
    double memory_used_mb;
    uint64_t memory_peak_bytes;
    int    cpu_cores;
    double ws_latency_p50_us;
    double ws_latency_p99_us;
    double feat_latency_p50_us;
    double feat_latency_p99_us;
    double model_latency_p50_us;
    double model_latency_p99_us;
    double e2e_latency_p50_us;
    double e2e_latency_p99_us;
    double exchange_latency_ms;
    double ticks_per_sec;
    double signals_per_sec;
    double orders_per_sec;
    double uptime_hours;
    bool   gpu_available;
    double gpu_usage_pct;
    char   gpu_name[64];
    char   inference_backend[32];
} TCSystemMonitor;

typedef struct {
    double signal_threshold_delta;
    double position_size_scale;
    double order_offset_bps;
    double requote_freq_scale;
    double avg_reward;
    double value_estimate;
    double policy_loss;
    double value_loss;
    int    total_steps;
    int    total_updates;
    bool   exploring;
} TCRLState;

typedef struct {
    double permutation_importance[25];
    double mutual_information[25];
    double shap_value[25];
    double correlation[25];
    int    ranking[25];
    int    active_features;
} TCFeatureImportance;

typedef struct {
    double best_bid;
    double best_ask;
    double mid_price;
    double spread;
    double microprice;
    size_t bid_count;
    size_t ask_count;
    uint64_t last_update_ns;
    bool   valid;
} TCOrderBookSummary;

typedef struct {
    uint64_t ob_updates;
    uint64_t trades_total;
    uint64_t signals_total;
    uint64_t orders_sent;
    uint64_t orders_filled;
    uint64_t orders_cancelled;
    uint64_t ws_reconnects;
    uint64_t e2e_latency_p50_ns;
    uint64_t e2e_latency_p99_ns;
    uint64_t feat_latency_p50_ns;
    uint64_t feat_latency_p99_ns;
    uint64_t model_latency_p50_ns;
    uint64_t model_latency_p99_ns;
} TCMetricsSnapshot;

typedef enum {
    TC_STATUS_IDLE       = 0,
    TC_STATUS_CONNECTING = 1,
    TC_STATUS_CONNECTED  = 2,
    TC_STATUS_TRADING    = 3,
    TC_STATUS_ERROR      = 4,
    TC_STATUS_STOPPING   = 5,
} TCEngineStatus;

typedef enum {
    TC_LOG_DEBUG = 0,
    TC_LOG_INFO  = 1,
    TC_LOG_WARN  = 2,
    TC_LOG_ERROR = 3,
} TCLogLevel;

// ─── Configuration ──────────────────────────────────────────────────────────

typedef struct {
    const char* symbol;
    bool        paper_trading;
    double      paper_fill_rate;   // R3: probability gate [0.0, 1.0]

    const char* ws_public_url;
    const char* ws_private_url;
    int         ws_ping_interval_sec;

    const char* rest_base_url;
    int         rest_timeout_ms;

    double      order_qty;
    double      signal_threshold;
    int         signal_ttl_ms;
    double      entry_offset_bps;

    double      max_position_size;
    double      max_leverage;
    double      max_daily_loss;
    double      max_drawdown;
    int         max_orders_per_sec;

    double      model_bias;
    const double* model_weights;
    int         model_weights_count;

    // AI Edition settings
    const char* ml_model_path;
    bool        ml_model_enabled;
    const char* onnx_model_path;
    bool        onnx_enabled;
    int         onnx_intra_threads;
    bool        adaptive_threshold_enabled;
    double      adaptive_threshold_min;
    double      adaptive_threshold_max;
    bool        regime_detection_enabled;
    bool        requote_enabled;
    int         requote_interval_ms;
    bool        fill_prob_enabled;
    double      fill_prob_market_threshold;
    bool        adaptive_sizing_enabled;
    double      base_order_qty;
    double      min_order_qty;
    double      max_order_qty;
    // Circuit breaker
    double      cb_loss_threshold;
    double      cb_drawdown_threshold;
    int         cb_consecutive_losses;
    int         cb_cooldown_sec;
    bool        cb_enabled;
    // Recording
    bool        record_ob_snapshots;
    bool        record_features;

    const char* log_dir;
    int         batch_flush_ms;
    int         ob_levels;
    int         io_threads;
    int         feature_tick_ms;
} TCConfig;

// ─── Callbacks ──────────────────────────────────────────────────────────────

typedef void (*TCOrderBookCallback)(void* ctx, const TCPriceLevel* bids, size_t bid_count,
                                     const TCPriceLevel* asks, size_t ask_count,
                                     const TCOrderBookSummary* summary);

typedef void (*TCTradeCallback)(void* ctx, const TCTrade* trade);
typedef void (*TCPositionCallback)(void* ctx, const TCPosition* position);
typedef void (*TCSignalCallback)(void* ctx, const TCSignal* signal);
typedef void (*TCLogCallback)(void* ctx, TCLogLevel level, const char* message);
typedef void (*TCStatusCallback)(void* ctx, TCEngineStatus status);
typedef void (*TCMetricsCallback)(void* ctx, const TCMetricsSnapshot* metrics);

// ─── Engine Lifecycle ───────────────────────────────────────────────────────

TCEngineHandle tc_engine_create(const TCConfig* config);
void           tc_engine_destroy(TCEngineHandle engine);

// Set API credentials (call before start)
void tc_engine_set_credentials(TCEngineHandle engine,
                                const char* api_key, const char* api_secret);

// Start / stop
bool tc_engine_start(TCEngineHandle engine);
void tc_engine_stop(TCEngineHandle engine);

// ─── Register Callbacks ─────────────────────────────────────────────────────

void tc_engine_set_orderbook_callback(TCEngineHandle engine, TCOrderBookCallback cb, void* ctx);
void tc_engine_set_trade_callback(TCEngineHandle engine, TCTradeCallback cb, void* ctx);
void tc_engine_set_position_callback(TCEngineHandle engine, TCPositionCallback cb, void* ctx);
void tc_engine_set_signal_callback(TCEngineHandle engine, TCSignalCallback cb, void* ctx);
void tc_engine_set_log_callback(TCEngineHandle engine, TCLogCallback cb, void* ctx);
void tc_engine_set_status_callback(TCEngineHandle engine, TCStatusCallback cb, void* ctx);
void tc_engine_set_metrics_callback(TCEngineHandle engine, TCMetricsCallback cb, void* ctx);

// ─── Polling (alternative to callbacks, thread-safe) ────────────────────────

TCEngineStatus     tc_engine_get_status(TCEngineHandle engine);
TCOrderBookSummary tc_engine_get_ob_summary(TCEngineHandle engine);
TCPosition         tc_engine_get_position(TCEngineHandle engine);
TCMetricsSnapshot  tc_engine_get_metrics(TCEngineHandle engine);
TCFeatures         tc_engine_get_features(TCEngineHandle engine);

// Get OB levels (caller allocates arrays, returns actual count written)
size_t tc_engine_get_bids(TCEngineHandle engine, TCPriceLevel* out, size_t max_levels);
size_t tc_engine_get_asks(TCEngineHandle engine, TCPriceLevel* out, size_t max_levels);

// ─── Runtime Config Changes ─────────────────────────────────────────────────

void tc_engine_set_paper_mode(TCEngineHandle engine, bool paper);
void tc_engine_set_signal_threshold(TCEngineHandle engine, double threshold);
void tc_engine_set_order_qty(TCEngineHandle engine, double qty);
void tc_engine_set_max_position(TCEngineHandle engine, double max_pos);

// ─── AI Edition Polling ─────────────────────────────────────────────────────

TCRegimeState              tc_engine_get_regime(TCEngineHandle engine);
TCModelPrediction          tc_engine_get_prediction(TCEngineHandle engine);
TCAdaptiveThresholdState   tc_engine_get_threshold_state(TCEngineHandle engine);
TCCircuitBreakerState      tc_engine_get_circuit_breaker(TCEngineHandle engine);
TCAccuracyMetrics          tc_engine_get_accuracy(TCEngineHandle engine);
TCStrategyMetrics          tc_engine_get_strategy_metrics(TCEngineHandle engine);
TCStrategyHealth           tc_engine_get_strategy_health(TCEngineHandle engine);
TCSystemMonitor            tc_engine_get_system_monitor(TCEngineHandle engine);
TCRLState                  tc_engine_get_rl_state(TCEngineHandle engine);
TCFeatureImportance        tc_engine_get_feature_importance(TCEngineHandle engine);

// Circuit breaker manual reset
void tc_engine_reset_circuit_breaker(TCEngineHandle engine);

// Emergency stop: cancels all orders and stops engine immediately
void tc_engine_emergency_stop(TCEngineHandle engine);

// Reload ML model at runtime (returns true on success)
bool tc_engine_reload_model(TCEngineHandle engine, const char* model_path);

// ─── Snapshot-based Polling (preferred — fully thread-safe) ──────────────────
// The engine publishes a consistent snapshot at end of each tick via SeqLock.
// These functions read from that snapshot — zero mutex, zero allocation.

typedef struct {
    // OrderBook
    TCPriceLevel bids[20];
    TCPriceLevel asks[20];
    int bid_count;
    int ask_count;
    double best_bid, best_ask, mid_price, spread, microprice;
    uint64_t ob_last_update_ns;
    bool ob_valid;

    // Position
    double pos_size, pos_entry, pos_unrealized, pos_realized, pos_funding;
    bool pos_is_long;

    // Metrics
    uint64_t ob_updates, trades_total, signals_total;
    uint64_t orders_sent, orders_filled, orders_cancelled, ws_reconnects;
    uint64_t e2e_p50_ns, e2e_p99_ns, feat_p50_ns, feat_p99_ns;
    uint64_t model_p50_ns, model_p99_ns;

    // Features
    TCFeatures features;

    // Regime
    TCRegimeState regime;

    // Prediction
    TCModelPrediction prediction;

    // Threshold
    TCAdaptiveThresholdState threshold;

    // Circuit Breaker
    bool cb_tripped, cb_cooldown;
    int cb_consec_losses;
    double cb_peak_pnl, cb_drawdown;

    // Accuracy
    TCAccuracyMetrics accuracy;
    bool using_onnx;

    // Strategy Metrics
    TCStrategyMetrics strategy_metrics;

    // Strategy Health
    TCStrategyHealth strategy_health;

    // System Monitor
    TCSystemMonitor system_monitor;

    // RL State
    TCRLState rl_state;

    // Feature Importance
    TCFeatureImportance feature_importance;

    // Control Plane (Stage 6)
    uint8_t  risk_state;              // RiskState enum
    uint8_t  exec_state;              // ExecState enum
    uint8_t  system_mode;             // SystemMode enum
    double   ctrl_position_scale;     // combined risk + health scale
    double   ctrl_throttle_factor;    // exec throttle
    bool     ctrl_allows_new_orders;
    bool     ctrl_allows_increase;
    uint32_t ctrl_total_transitions;
    uint64_t ctrl_audit_depth;

    // Engine state
    char inference_backend[32];
    bool engine_running;
    uint64_t ws_rtt_us;               // E5: WebSocket round-trip time in microseconds
    uint64_t snapshot_ns;
    uint64_t snapshot_version;        // M2: version counter for diff-based polling
} TCFullSnapshot;

// Read the full snapshot in one call (most efficient — single SeqLock read)
void tc_engine_get_snapshot(TCEngineHandle engine, TCFullSnapshot* out);

// Snapshot version counter (increments on each publish)
uint64_t tc_engine_snapshot_version(TCEngineHandle engine);

// ─── v2.4: Chaos Engine ────────────────────────────────────────────────────

typedef struct {
    bool     enabled;
    uint64_t latency_spikes;
    uint64_t packets_dropped;
    uint64_t fake_deltas_injected;
    uint64_t oom_simulations;
    uint64_t corrupted_jsons;
    uint64_t clock_skews;
    uint64_t total_injected_latency_ns;
    uint64_t max_injected_latency_ns;
    uint64_t total_injections;
} TCChaosState;

TCChaosState tc_engine_get_chaos_state(TCEngineHandle engine);
void tc_engine_set_chaos_enabled(TCEngineHandle engine, bool enabled);
void tc_engine_set_chaos_nightly(TCEngineHandle engine);
void tc_engine_set_chaos_flash_crash(TCEngineHandle engine);
void tc_engine_reset_chaos_stats(TCEngineHandle engine);

// ─── v2.4: Deterministic Replay ────────────────────────────────────────────

typedef struct {
    bool     loaded;
    bool     playing;
    uint64_t event_count;
    uint64_t events_replayed;
    uint64_t events_filtered;
    double   replay_speed;
    bool     checksum_valid;
    bool     sequence_monotonic;
    uint64_t replay_duration_ns;
    char   loaded_file[256];
} TCReplayState;

TCReplayState tc_engine_get_replay_state(TCEngineHandle engine);
bool tc_engine_load_replay(TCEngineHandle engine, const char* path);
void tc_engine_start_replay(TCEngineHandle engine);
void tc_engine_stop_replay(TCEngineHandle engine);
void tc_engine_set_replay_speed(TCEngineHandle engine, double speed);
void tc_engine_step_replay(TCEngineHandle engine);

// ─── v2.4: VaR Engine ─────────────────────────────────────────────────────

typedef struct {
    double var_95;
    double var_99;
    double cvar_95;
    double cvar_99;
    double parametric_var;
    double historical_var;
    double monte_carlo_var;
    int    monte_carlo_samples;
    double portfolio_value;
    double stress_scenario_losses[8];
} TCVaRSnapshot;

TCVaRSnapshot tc_engine_get_var(TCEngineHandle engine);

// ─── v2.4: Order State Machine ─────────────────────────────────────────────

typedef struct {
    char   order_id[48];
    int    state;
    bool   is_buy;
    double price;
    double qty;
    double filled_qty;
    double avg_fill_price;
    double fill_probability;
    uint64_t created_ns;
    uint64_t last_update_ns;
    int    cancel_attempts;
} TCManagedOrderC;

typedef struct {
    int    active_orders;
    int    total_transitions;
    double avg_fill_time_us;
    double avg_slippage;
    bool   iceberg_active;
    int    iceberg_slices_done;
    int    iceberg_slices_total;
    double iceberg_filled_qty;
    double iceberg_total_qty;
    bool   twap_active;
    int    twap_slices_done;
    int    twap_slices_total;
    double market_impact_bps;
    TCManagedOrderC orders[64];
    int    order_count;
} TCOSMSummary;

TCOSMSummary tc_engine_get_osm_summary(TCEngineHandle engine);

// ─── v2.4: RL v2 Extended ──────────────────────────────────────────────────

typedef struct {
    double entropy_alpha;
    double kl_divergence;
    double clip_fraction;
    double approx_kl;
    double explained_variance;
    int    rollback_count;
    int    epochs_completed;
    int    buffer_size;
    int    buffer_capacity;
    bool   training_active;
    double learning_rate;
    double state_vector[32];
    double action_vector[4];
    double reward_history[256];
    int    reward_history_count;
} TCRLv2State;

TCRLv2State tc_engine_get_rl_v2_state(TCEngineHandle engine);

// ─── v2.4: Per-Stage Histograms ────────────────────────────────────────────

typedef struct {
    char   stage_name[32];
    uint64_t count;
    double mean_us;
    double p50_us;
    double p90_us;
    double p95_us;
    double p99_us;
    double p999_us;
    double max_us;
    double stddev_us;
} TCStageHistogramEntry;

typedef struct {
    TCStageHistogramEntry stages[16];
    int count;
} TCStageHistogramArray;

TCStageHistogramArray tc_engine_get_stage_histograms(TCEngineHandle engine);
bool tc_engine_export_flamegraph(TCEngineHandle engine, const char* path);
bool tc_engine_export_histograms_csv(TCEngineHandle engine, const char* path);

#ifdef __cplusplus
}
#endif

#endif // TRADING_CORE_API_H
