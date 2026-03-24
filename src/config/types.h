#pragma once

#include <string>
#include <array>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <type_traits>

// ─── Stage 1 V2 integration ────────────────────────────────────────────────
// memory_policy.h provides:
//   - bybit::BYBIT_CACHELINE (inline constexpr, 128 on Apple Silicon)
//   - CacheLinePadded<T>, IsolatedCounter, IsolatedAtomicDouble
//   - BYBIT_VERIFY_LAYOUT / BYBIT_VERIFY_TRIVIAL / BYBIT_VERIFY_CACHELINE macros
//
// The BYBIT_CACHELINE macro below is a backward-compat alias for the constant.
// TODO(stage3): Remove macro, use bybit::BYBIT_CACHELINE everywhere.

#include "../core/memory_policy.h"
#include "../core/strong_types.h"

// ─── Cache-line size for Apple Silicon (M2/M3/M4) ──────────────────────────
// Compat macro — delegates to the constexpr in memory_policy.h.
#ifndef BYBIT_CACHELINE
#define BYBIT_CACHELINE 128
#endif

// ─── Prefetch helpers ───────────────────────────────────────────────────────
// L1 temporal prefetch for data that will be reused soon
#define BYBIT_PREFETCH_R(ptr) __builtin_prefetch((ptr), 0, 3)
// L1 write prefetch for data that will be written soon
#define BYBIT_PREFETCH_W(ptr) __builtin_prefetch((ptr), 1, 3)
// L2 prefetch for data needed in ~50 cycles
#define BYBIT_PREFETCH_L2(ptr) __builtin_prefetch((ptr), 0, 2)

namespace bybit {

// ─── Compile-time constants ─────────────────────────────────────────────────

static constexpr size_t MAX_OB_LEVELS        = 500;
static constexpr size_t MAX_TRADES_BUFFER     = 16384;
static constexpr size_t FEATURE_TICK_MS       = 10;   // 10ms tick for high precision
static constexpr size_t MAX_OPEN_ORDERS       = 64;
static constexpr size_t RING_BUFFER_SIZE      = 32768;
// Legacy BTC defaults — used as fallback when instrument info is unavailable.
// Production code should use AppConfig::tick_size / lot_size instead.
static constexpr double TICK_SIZE_BTCUSDT     = 0.1;
static constexpr double LOT_SIZE_BTCUSDT      = 0.001;

// Feature sequence length for ML model
static constexpr size_t FEATURE_SEQ_LEN       = 100;
// Number of features in the expanded feature vector
static constexpr size_t FEATURE_COUNT          = 25;
// Number of prediction horizons (100ms, 500ms, 1s, 3s)
static constexpr size_t NUM_HORIZONS           = 4;
// Number of market regimes
static constexpr size_t NUM_REGIMES            = 5;
// GRU hidden size
static constexpr size_t GRU_HIDDEN_SIZE        = 64;

// Memory pool block sizes
static constexpr size_t POOL_BLOCK_SIZE        = 4096;
static constexpr size_t POOL_BLOCK_COUNT       = 256;

// ─── Price Level ────────────────────────────────────────────────────────────

struct alignas(16) PriceLevel {
    double price = 0.0;
    double qty   = 0.0;

    // Prefetch the next N levels starting from this pointer
    static void prefetch_ahead(const PriceLevel* p, size_t n = 4) noexcept {
        for (size_t i = 0; i < n; ++i)
            BYBIT_PREFETCH_R(p + i);
    }
};

// ─── Trade ──────────────────────────────────────────────────────────────────

struct alignas(64) Trade {
    uint64_t timestamp_ns = 0;
    double   price        = 0.0;
    double   qty          = 0.0;
    bool     is_buyer_maker = false;
    char     padding_[7]  = {};
    // Pad to 64 bytes — 2 trades per cache line on Apple Silicon
    uint64_t reserved_[2] = {};
};

// ─── Market Regime ──────────────────────────────────────────────────────────

enum class MarketRegime : uint8_t {
    LowVolatility   = 0,
    HighVolatility   = 1,
    Trending         = 2,
    MeanReverting    = 3,
    LiquidityVacuum  = 4
};

// ─── Features v2 ────────────────────────────────────────────────────────────
// 25 features for ML model input, cache-line aligned

struct alignas(BYBIT_CACHELINE) Features {
    // ── Order book features (7) ──
    double imbalance_1       = 0.0;   // top-of-book imbalance
    double imbalance_5       = 0.0;   // 5-level imbalance
    double imbalance_20      = 0.0;   // 20-level cumulative depth imbalance
    double ob_slope          = 0.0;   // qty-weighted avg distance from mid
    double depth_concentration = 0.0; // how concentrated liquidity is near TOB
    double cancel_spike      = 0.0;   // drop in TOB qty (spoofing detection)
    double liquidity_wall    = 0.0;   // max single-level qty / avg qty

    // ── Trade flow features (5) ──
    double aggression_ratio  = 0.0;   // buy_vol / total_vol in window
    double avg_trade_size    = 0.0;   // average trade size in window
    double trade_velocity    = 0.0;   // trades/sec
    double trade_acceleration = 0.0;  // d(velocity)/dt
    double volume_accel      = 0.0;   // short_rate / long_rate

    // ── Price features (5) ──
    double microprice        = 0.0;   // volume-weighted mid
    double spread_bps        = 0.0;   // spread in basis points
    double spread_change_rate = 0.0;  // d(spread)/dt
    double mid_momentum      = 0.0;   // mid price return over window
    double volatility        = 0.0;   // EWMA volatility of returns

    // ── Derived features (4) ──
    double microprice_dev    = 0.0;   // (microprice - mid) / mid
    double short_term_pressure = 0.0; // microprice momentum
    double bid_depth_total   = 0.0;   // total bid volume (normalized)
    double ask_depth_total   = 0.0;   // total ask volume (normalized)

    // ── Temporal derivatives (4) ──
    double d_imbalance_dt    = 0.0;   // first derivative of imbalance
    double d2_imbalance_dt2  = 0.0;   // second derivative of imbalance
    double d_volatility_dt   = 0.0;   // first derivative of volatility
    double d_momentum_dt     = 0.0;   // first derivative of momentum

    uint64_t timestamp_ns    = 0;

    static constexpr size_t COUNT = FEATURE_COUNT;

    const double* as_array() const noexcept { return &imbalance_1; }
    double* as_mutable_array() noexcept { return &imbalance_1; }
};

// ─── Model Output v2 ────────────────────────────────────────────────────────
// Multi-horizon predictions with 3-class output

struct alignas(BYBIT_CACHELINE) ModelOutput {
    // Per-horizon probabilities: [up, down, no_move]
    struct HorizonPrediction {
        double prob_up   = 0.333;
        double prob_down = 0.333;
        double prob_flat = 0.334;
        double predicted_move_bps = 0.0; // expected magnitude
    };

    std::array<HorizonPrediction, NUM_HORIZONS> horizons; // 100ms, 500ms, 1s, 3s

    // Convenience: primary prediction (500ms horizon)
    double probability_up   = 0.5;
    double probability_down = 0.5;

    // Model metadata
    double    model_confidence = 0.0;   // calibrated confidence [0,1]
    uint64_t  inference_latency_ns = 0;
    uint64_t  timestamp_ns = 0;
};

// ─── Regime State ───────────────────────────────────────────────────────────

struct alignas(BYBIT_CACHELINE) RegimeState {
    MarketRegime current      = MarketRegime::LowVolatility;
    MarketRegime previous     = MarketRegime::LowVolatility;
    double       confidence   = 0.0;
    double       volatility   = 0.0;
    double       trend_score  = 0.0;
    double       mr_score     = 0.0;  // mean-reversion score
    double       liq_score    = 0.0;  // liquidity score
    uint64_t     regime_start_ns = 0;
    uint64_t     timestamp_ns = 0;

    // Per-regime strategy parameters
    struct RegimeParams {
        double signal_threshold = 0.6;
        double position_scale   = 1.0;
        double entry_offset_bps = 1.0;
        double stale_cancel_ms  = 300.0;
    };
    std::array<RegimeParams, NUM_REGIMES> params;
};

// ─── Fill Probability ───────────────────────────────────────────────────────

struct alignas(64) FillProbability {
    double prob_fill_100ms = 0.0;
    double prob_fill_500ms = 0.0;
    double prob_fill_1s    = 0.0;
    double queue_position  = 0.0;  // estimated position in queue [0..1]
    double liq_removal_rate = 0.0; // rate of liquidity removal
};

// ─── Order Side / Status / Type ─────────────────────────────────────────────

enum class Side : uint8_t { Buy, Sell };
enum class OrderStatus : uint8_t { New, PartiallyFilled, Filled, Cancelled, Rejected };
enum class OrderType : uint8_t { Limit, Market };
enum class TimeInForce : uint8_t { GTC, IOC, FOK, PostOnly };

// ─── Order ──────────────────────────────────────────────────────────────────

struct alignas(BYBIT_CACHELINE) Order {
    OrderId      order_id;                  // fixed 48-byte ID (was char[48])
    InstrumentId symbol;                    // fixed 16-byte symbol (was char[16])
    Side     side          = Side::Buy;
    OrderType type         = OrderType::Limit;
    TimeInForce tif        = TimeInForce::PostOnly;
    OrderStatus status     = OrderStatus::New;
    Price    price;                         // order price (semantic)
    Qty      qty;                           // order quantity (semantic)
    Qty      filled_qty;                    // filled quantity (semantic)
    uint64_t create_time_ns = 0;
    bool     reduce_only   = false;
    uint32_t requote_count = 0;
    double   fill_prob     = 0.0;           // normalized [0,1] — dimensionless
};

// ─── Position ───────────────────────────────────────────────────────────────

struct Position {
    Qty      size;                // signed: +long, -short
    Price    entry_price;         // average entry price
    Notional unrealized_pnl;      // mark-to-market PnL
    Notional realized_pnl;        // closed PnL
    Notional funding_impact;      // cumulative funding
    Side     side           = Side::Buy;
    Price    mark_price;          // last mark price
};

// ─── Risk Limits ────────────────────────────────────────────────────────────

struct RiskLimits {
    Qty      max_position_size  = Qty(0.1);
    double   max_leverage       = 10.0;      // dimensionless ratio
    Notional max_daily_loss     = Notional(500.0);
    double   max_drawdown       = 0.1;       // dimensionless fraction
    int      max_orders_per_sec = 20;   // Bybit allows 10-20 req/s on order endpoints
};

// ─── Circuit Breaker Config ─────────────────────────────────────────────────

struct CircuitBreakerConfig {
    double loss_threshold     = 200.0;  // stop if PnL drops below this
    double drawdown_threshold = 0.05;   // stop if drawdown exceeds 5%
    int    consecutive_losses = 10;     // stop after N consecutive losing trades
    int    cooldown_sec       = 300;    // cooldown period before restart
    double max_loss_rate      = 50.0;   // max $/minute loss rate
    bool   enabled            = true;
};

// ─── Signal (enhanced) ──────────────────────────────────────────────────────

struct alignas(BYBIT_CACHELINE) Signal {
    Side          side          = Side::Buy;
    Price         price;                    // entry price (semantic)
    Qty           qty;                      // order quantity (semantic)
    double        confidence    = 0.0;      // normalized [0,1] — dimensionless
    uint64_t      timestamp_ns  = 0;
    MarketRegime  regime        = MarketRegime::LowVolatility;
    double        fill_prob     = 0.0;      // normalized [0,1] — dimensionless
    Notional      expected_pnl;              // expected PnL of the trade
    BasisPoints   expected_move;             // predicted price move in bps
    double        adaptive_threshold = 0.0;  // dimensionless threshold
};

// ─── Adaptive Threshold State ───────────────────────────────────────────────

struct AdaptiveThresholdState {
    double current_threshold  = 0.6;
    double base_threshold     = 0.6;
    double volatility_adj     = 0.0;
    double accuracy_adj       = 0.0;
    double liquidity_adj      = 0.0;
    double spread_adj         = 0.0;
    double recent_accuracy    = 0.5;  // accuracy over last N signals
    int    window_size        = 100;
    int    correct_count      = 0;
    int    total_count        = 0;
};

// ─── Config ─────────────────────────────────────────────────────────────────

struct AppConfig {
    // Exchange
    std::string api_key;
    std::string api_secret;
    std::string symbol           = "BTCUSDT";
    bool        paper_trading    = true;
    double      paper_fill_rate  = 0.85; // R3: probability gate for paper fills (0.0-1.0)

    // WebSocket
    std::string ws_public_url    = "wss://stream.bybit.com/v5/public/linear";
    std::string ws_private_url   = "wss://stream.bybit.com/v5/private";
    std::string ws_trade_url     = "wss://stream.bybit.com/v5/trade";  // #15: WS Trade API
    int         ws_ping_interval_sec = 20;
    int         ws_stale_timeout_sec = 30;
    int         ws_reconnect_base_ms = 1000;
    int         ws_reconnect_max_ms  = 30000;

    // REST
    std::string rest_base_url    = "https://api.bybit.com";
    int         rest_timeout_ms  = 5000;
    int         rest_max_retries = 3;

    // Trading
    double      order_qty        = 0.001;
    double      signal_threshold = 0.6;
    int         signal_ttl_ms    = 300;
    double      entry_offset_bps = 1.0;

    // Risk
    RiskLimits  risk;
    CircuitBreakerConfig circuit_breaker;

    // Model — legacy simple model
    std::array<double, FEATURE_COUNT> model_weights = {};
    double model_bias = 0.0;

    // ML model path (GRU weights file)
    std::string ml_model_path    = "";
    bool        ml_model_enabled = true;

    // ONNX model path (preferred over native GRU when available)
    std::string onnx_model_path  = "";
    bool        onnx_enabled     = true;  // use ONNX if available, else fallback to native GRU
    int         onnx_intra_threads = 1;   // ONNX Runtime intra-op parallelism

    // Adaptive thresholds
    bool   adaptive_threshold_enabled = true;
    double adaptive_threshold_min     = 0.45;
    double adaptive_threshold_max     = 0.85;

    // Regime detection
    bool   regime_detection_enabled   = true;

    // Execution intelligence
    bool   requote_enabled     = true;
    int    requote_interval_ms = 100;
    bool   fill_prob_enabled   = true;
    double fill_prob_market_threshold = 0.1; // use market order if fill_prob < this

    // Adaptive position sizing
    bool   adaptive_sizing_enabled = true;
    double base_order_qty          = 0.001;
    double min_order_qty           = 0.001;
    double max_order_qty           = 0.01;

    // Reinforcement learning
    bool   rl_enabled              = false;

    // Persistence
    std::string log_dir          = "./logs";
    int         batch_flush_ms   = 500;
    bool        record_ob_snapshots = true;
    bool        record_features     = true;

    // Performance
    int         ob_levels        = 500;
    int         io_threads       = 2;
    int         feature_tick_ms  = 10;

    // Instrument metadata (fetched from exchange or config; BTC defaults)
    double      tick_size        = TICK_SIZE_BTCUSDT;
    double      lot_size         = LOT_SIZE_BTCUSDT;
};

// ─── Branchless Regime Lookup Tables ─────────────────────────────────────────
// Semi-static: eliminates branches in hot path for regime-dependent parameters.
// Use: regime_threshold_lut[static_cast<uint8_t>(regime)] instead of switch.

static constexpr std::array<double, NUM_REGIMES> REGIME_THRESHOLD_LUT = {
    0.55,  // LowVolatility  — tighter threshold
    0.70,  // HighVolatility — wider threshold
    0.50,  // Trending       — follow momentum
    0.65,  // MeanReverting  — moderate
    0.80,  // LiquidityVacuum — very conservative
};

static constexpr std::array<double, NUM_REGIMES> REGIME_POSITION_SCALE_LUT = {
    1.0,   // LowVolatility
    0.5,   // HighVolatility — reduce size
    1.2,   // Trending       — lean in
    0.8,   // MeanReverting
    0.3,   // LiquidityVacuum — minimal size
};

static constexpr std::array<double, NUM_REGIMES> REGIME_OFFSET_BPS_LUT = {
    1.0,   // LowVolatility
    3.0,   // HighVolatility — wider offset
    0.5,   // Trending       — tight to capture move
    1.5,   // MeanReverting
    5.0,   // LiquidityVacuum — wide to avoid slippage
};

static constexpr std::array<double, NUM_REGIMES> REGIME_CANCEL_MS_LUT = {
    300.0, // LowVolatility
    100.0, // HighVolatility — cancel fast
    500.0, // Trending       — patient
    200.0, // MeanReverting
    50.0,  // LiquidityVacuum — very fast cancel
};

// Branchless regime parameter access
inline double regime_threshold(MarketRegime r) noexcept {
    return REGIME_THRESHOLD_LUT[static_cast<uint8_t>(r)];
}
inline double regime_position_scale(MarketRegime r) noexcept {
    return REGIME_POSITION_SCALE_LUT[static_cast<uint8_t>(r)];
}
inline double regime_offset_bps(MarketRegime r) noexcept {
    return REGIME_OFFSET_BPS_LUT[static_cast<uint8_t>(r)];
}
inline double regime_cancel_ms(MarketRegime r) noexcept {
    return REGIME_CANCEL_MS_LUT[static_cast<uint8_t>(r)];
}

} // namespace bybit
