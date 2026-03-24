// LynrixCoreBridge.mm — Objective-C++ implementation bridging C API to Objective-C (Lynrix v2.4)
#import "LynrixCoreBridge.h"
#include "trading_core_api.h"
#include <os/log.h>

static os_log_t bridgeLog() {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("com.lynrix.trader", "Bridge");
    });
    return log;
}

// ─── Data Model Implementations ─────────────────────────────────────────────

@implementation TCPriceLevelObjC
@end

@implementation TCOrderBookData
- (instancetype)init {
    self = [super init];
    if (self) {
        _bids = @[];
        _asks = @[];
    }
    return self;
}
@end

@implementation TCPositionData
@end

@implementation TCMetricsData
@end

@implementation TCSignalData
@end

@implementation TCRegimeData
@end

@implementation TCPredictionData
@end

@implementation TCThresholdData
@end

@implementation TCCircuitBreakerData
@end

@implementation TCAccuracyData
@end

@implementation TCStrategyMetricsData
@end

@implementation TCStrategyHealthData
@end

@implementation TCSystemMonitorData
- (instancetype)init {
    self = [super init];
    if (self) {
        _gpuName = @"N/A";
        _inferenceBackend = @"CPU";
    }
    return self;
}
@end

@implementation TCRLStateData
@end

@implementation TCFeatureImportanceData
- (instancetype)init {
    self = [super init];
    if (self) {
        _permutationImportance = @[];
        _mutualInformation = @[];
        _shapValue = @[];
        _correlation = @[];
        _ranking = @[];
    }
    return self;
}
@end

// ─── v2.4 Data Model Implementations ────────────────────────────────────────

@implementation TCChaosStateData
@end

@implementation TCReplayStateData
- (instancetype)init {
    self = [super init];
    if (self) {
        _loadedFile = @"";
    }
    return self;
}
@end

@implementation TCVaRData
- (instancetype)init {
    self = [super init];
    if (self) {
        _stressScenarioLosses = @[];
        _stressScenarioNames = @[
            @"Flash Crash -10%", @"Vol Spike 3x", @"Liquidity Drought",
            @"Correlated Sell-off", @"Rate Shock +200bp", @"FX Dislocation",
            @"Crypto Contagion", @"Black Swan -25%"
        ];
    }
    return self;
}
@end

@implementation TCManagedOrderData
- (instancetype)init {
    self = [super init];
    if (self) {
        _orderId = @"";
    }
    return self;
}
@end

@implementation TCOSMSummaryData
- (instancetype)init {
    self = [super init];
    if (self) {
        _orders = @[];
    }
    return self;
}
@end

@implementation TCRLv2StateData
- (instancetype)init {
    self = [super init];
    if (self) {
        _stateVector = @[];
        _actionVector = @[];
        _rewardHistory = @[];
    }
    return self;
}
@end

@implementation TCStageHistogramData
- (instancetype)init {
    self = [super init];
    if (self) {
        _stageName = @"";
    }
    return self;
}
@end

@implementation TCConfigObjC
- (instancetype)init {
    self = [super init];
    if (self) {
        _symbol = @"BTCUSDT";
        _paperTrading = YES;
        _paperFillRate = 0.85;
        _orderQty = 0.001;
        _signalThreshold = 0.6;
        _signalTtlMs = 300;
        _entryOffsetBps = 1.0;
        _maxPositionSize = 0.1;
        _maxLeverage = 10.0;
        _maxDailyLoss = 500.0;
        _maxDrawdown = 0.1;
        _maxOrdersPerSec = 5;
        _modelBias = 0.0;
        _obLevels = 500;
        _ioThreads = 2;
        // AI Edition defaults
        _mlModelEnabled = YES;
        _adaptiveThresholdEnabled = YES;
        _adaptiveThresholdMin = 0.3;
        _adaptiveThresholdMax = 0.9;
        _regimeDetectionEnabled = YES;
        _requoteEnabled = YES;
        _requoteIntervalMs = 100;
        _fillProbEnabled = YES;
        _fillProbMarketThreshold = 0.6;
        _adaptiveSizingEnabled = YES;
        _baseOrderQty = 0.001;
        _minOrderQty = 0.0001;
        _maxOrderQty = 0.01;
        _cbEnabled = YES;
        _cbLossThreshold = 200.0;
        _cbDrawdownThreshold = 0.05;
        _cbConsecutiveLosses = 10;
        _cbCooldownSec = 300;
        _recordObSnapshots = YES;
        _recordFeatures = YES;
        _featureTickMs = 10;
    }
    return self;
}
@end

// ─── C Callback Trampolines ─────────────────────────────────────────────────

static void statusCallbackTrampoline(void* ctx, TCEngineStatus status) {
    LynrixCoreBridge* bridge = (__bridge LynrixCoreBridge*)ctx;
    TCEngineStatusObjC objcStatus = (TCEngineStatusObjC)status;
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([bridge.delegate respondsToSelector:@selector(engineDidChangeStatus:)]) {
            [bridge.delegate engineDidChangeStatus:objcStatus];
        }
    });
}

static void logCallbackTrampoline(void* ctx, TCLogLevel level, const char* message) {
    LynrixCoreBridge* bridge = (__bridge LynrixCoreBridge*)ctx;
    NSString* msg = [NSString stringWithUTF8String:message ?: ""];
    TCLogLevelObjC objcLevel = (TCLogLevelObjC)level;
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([bridge.delegate respondsToSelector:@selector(engineDidReceiveLog:level:)]) {
            [bridge.delegate engineDidReceiveLog:msg level:objcLevel];
        }
    });
}

// ─── Bridge Implementation ──────────────────────────────────────────────────

@interface LynrixCoreBridge () {
    TCEngineHandle _engine;
    // C1+E1: Cached snapshot — single atomic read per poll cycle
    TCFullSnapshot _cachedSnap;
    uint64_t _lastSnapVersion;
}
@end

@implementation LynrixCoreBridge

- (instancetype)initWithConfig:(TCConfigObjC *)config {
    self = [super init];
    if (self) {
        os_log_info(bridgeLog(), "Creating LynrixCoreBridge v2.4");
        
        @try {
            TCConfig cConfig = {};
            memset(&cConfig, 0, sizeof(TCConfig));
            
            cConfig.symbol = config.symbol ? config.symbol.UTF8String : "BTCUSDT";
            cConfig.paper_trading = config.paperTrading;
            cConfig.paper_fill_rate = config.paperFillRate;
            cConfig.ws_public_url = config.wsPublicUrl ? config.wsPublicUrl.UTF8String : NULL;
            cConfig.ws_private_url = config.wsPrivateUrl ? config.wsPrivateUrl.UTF8String : NULL;
            cConfig.rest_base_url = config.restBaseUrl ? config.restBaseUrl.UTF8String : NULL;
            cConfig.order_qty = config.orderQty;
            cConfig.signal_threshold = config.signalThreshold;
            cConfig.signal_ttl_ms = config.signalTtlMs;
            cConfig.entry_offset_bps = config.entryOffsetBps;
            cConfig.max_position_size = config.maxPositionSize;
            cConfig.max_leverage = config.maxLeverage;
            cConfig.max_daily_loss = config.maxDailyLoss;
            cConfig.max_drawdown = config.maxDrawdown;
            cConfig.max_orders_per_sec = config.maxOrdersPerSec;
            cConfig.model_bias = config.modelBias;
            cConfig.ob_levels = config.obLevels;
            cConfig.io_threads = config.ioThreads;
            cConfig.feature_tick_ms = config.featureTickMs;

            // AI Edition config
            cConfig.ml_model_path = config.mlModelPath ? config.mlModelPath.UTF8String : NULL;
            cConfig.ml_model_enabled = config.mlModelEnabled;
            cConfig.onnx_model_path = config.onnxModelPath ? config.onnxModelPath.UTF8String : NULL;
            cConfig.onnx_enabled = config.onnxEnabled;
            cConfig.onnx_intra_threads = config.onnxIntraThreads;
            cConfig.adaptive_threshold_enabled = config.adaptiveThresholdEnabled;
            cConfig.adaptive_threshold_min = config.adaptiveThresholdMin;
            cConfig.adaptive_threshold_max = config.adaptiveThresholdMax;
            cConfig.regime_detection_enabled = config.regimeDetectionEnabled;
            cConfig.requote_enabled = config.requoteEnabled;
            cConfig.requote_interval_ms = config.requoteIntervalMs;
            cConfig.fill_prob_enabled = config.fillProbEnabled;
            cConfig.fill_prob_market_threshold = config.fillProbMarketThreshold;
            cConfig.adaptive_sizing_enabled = config.adaptiveSizingEnabled;
            cConfig.base_order_qty = config.baseOrderQty;
            cConfig.min_order_qty = config.minOrderQty;
            cConfig.max_order_qty = config.maxOrderQty;
            cConfig.cb_enabled = config.cbEnabled;
            cConfig.cb_loss_threshold = config.cbLossThreshold;
            cConfig.cb_drawdown_threshold = config.cbDrawdownThreshold;
            cConfig.cb_consecutive_losses = config.cbConsecutiveLosses;
            cConfig.cb_cooldown_sec = config.cbCooldownSec;
            cConfig.record_ob_snapshots = config.recordObSnapshots;
            cConfig.record_features = config.recordFeatures;

            // Model weights
            double weights[11] = {};
            if (config.modelWeights) {
                NSUInteger count = MIN(config.modelWeights.count, 11);
                for (NSUInteger i = 0; i < count; ++i) {
                    weights[i] = config.modelWeights[i].doubleValue;
                }
                cConfig.model_weights = weights;
                cConfig.model_weights_count = (int)count;
            }

            if (config.logDir) {
                cConfig.log_dir = config.logDir.UTF8String;
            }

            _engine = tc_engine_create(&cConfig);
            
            if (!_engine) {
                os_log_error(bridgeLog(), "tc_engine_create returned NULL");
                return nil;
            }

            // Register callbacks
            tc_engine_set_status_callback(_engine, statusCallbackTrampoline, (__bridge void*)self);
            tc_engine_set_log_callback(_engine, logCallbackTrampoline, (__bridge void*)self);
            
            os_log_info(bridgeLog(), "Bridge created successfully, engine handle: %p", _engine);
        } @catch (NSException *exception) {
            os_log_fault(bridgeLog(), "Exception in initWithConfig: %{public}@ — %{public}@",
                        exception.name, exception.reason);
            _engine = nil;
            return nil;
        }
    }
    return self;
}

- (void)dealloc {
    os_log_info(bridgeLog(), "LynrixCoreBridge dealloc");
    @try {
        if (_engine) {
            tc_engine_destroy(_engine);
            _engine = nil;
        }
    } @catch (NSException *exception) {
        os_log_fault(bridgeLog(), "Exception in dealloc: %{public}@", exception.reason);
    }
}

- (void)setAPIKey:(NSString *)key secret:(NSString *)secret {
    if (_engine) {
        tc_engine_set_credentials(_engine, key.UTF8String, secret.UTF8String);
    }
}

- (BOOL)start {
    if (!_engine) {
        os_log_error(bridgeLog(), "start called but engine is NULL");
        return NO;
    }
    os_log_info(bridgeLog(), "Starting engine...");
    @try {
        BOOL result = tc_engine_start(_engine);
        os_log_info(bridgeLog(), "Engine start result: %d", result);
        return result;
    } @catch (NSException *exception) {
        os_log_fault(bridgeLog(), "Exception in start: %{public}@ — %{public}@",
                    exception.name, exception.reason);
        return NO;
    }
}

- (void)stop {
    if (_engine) {
        os_log_info(bridgeLog(), "Stopping engine...");
        @try {
            tc_engine_stop(_engine);
            os_log_info(bridgeLog(), "Engine stopped");
        } @catch (NSException *exception) {
            os_log_fault(bridgeLog(), "Exception in stop: %{public}@", exception.reason);
        }
    }
}

- (TCEngineStatusObjC)status {
    if (!_engine) return TCEngineStatusIdle;
    return (TCEngineStatusObjC)tc_engine_get_status(_engine);
}

// C1+E1: Single atomic snapshot read — call once per poll cycle
- (void)refreshSnapshot {
    if (!_engine) return;
    tc_engine_get_snapshot(_engine, &_cachedSnap);
    _lastSnapVersion = _cachedSnap.snapshot_version;
}

- (uint64_t)snapshotVersion {
    if (!_engine) return 0;
    return tc_engine_snapshot_version(_engine);
}

- (TCOrderBookData *)getOrderBook {
    return [self getOrderBookWithLevels:20];
}

// C1+E1+M1: Extract from cached snapshot — no separate C calls, no calloc/free
- (TCOrderBookData *)getOrderBookWithLevels:(NSUInteger)maxLevels {
    TCOrderBookData *data = [[TCOrderBookData alloc] init];
    if (!_engine) return data;

    data.bestBid = _cachedSnap.best_bid;
    data.bestAsk = _cachedSnap.best_ask;
    data.midPrice = _cachedSnap.mid_price;
    data.spread = _cachedSnap.spread;
    data.microprice = _cachedSnap.microprice;
    data.bidCount = _cachedSnap.bid_count;
    data.askCount = _cachedSnap.ask_count;
    data.lastUpdateNs = _cachedSnap.snapshot_ns;
    data.valid = _cachedSnap.ob_valid;

    NSUInteger bidN = MIN((NSUInteger)_cachedSnap.bid_count, maxLevels);
    NSUInteger askN = MIN((NSUInteger)_cachedSnap.ask_count, maxLevels);

    NSMutableArray<TCPriceLevelObjC *> *bids = [NSMutableArray arrayWithCapacity:bidN];
    for (NSUInteger i = 0; i < bidN; ++i) {
        TCPriceLevelObjC *lvl = [[TCPriceLevelObjC alloc] init];
        lvl.price = _cachedSnap.bids[i].price;
        lvl.qty = _cachedSnap.bids[i].qty;
        [bids addObject:lvl];
    }
    data.bids = bids;

    NSMutableArray<TCPriceLevelObjC *> *asks = [NSMutableArray arrayWithCapacity:askN];
    for (NSUInteger i = 0; i < askN; ++i) {
        TCPriceLevelObjC *lvl = [[TCPriceLevelObjC alloc] init];
        lvl.price = _cachedSnap.asks[i].price;
        lvl.qty = _cachedSnap.asks[i].qty;
        [asks addObject:lvl];
    }
    data.asks = asks;

    return data;
}

// C1+E1: Extract from cached snapshot
- (TCPositionData *)getPosition {
    TCPositionData *data = [[TCPositionData alloc] init];
    if (!_engine) return data;
    data.size = _cachedSnap.pos_size;
    data.entryPrice = _cachedSnap.pos_entry;
    data.unrealizedPnl = _cachedSnap.pos_unrealized;
    data.realizedPnl = _cachedSnap.pos_realized;
    data.fundingImpact = _cachedSnap.pos_funding;
    data.isLong = _cachedSnap.pos_is_long;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCMetricsData *)getMetrics {
    TCMetricsData *data = [[TCMetricsData alloc] init];
    if (!_engine) return data;
    data.obUpdates = _cachedSnap.ob_updates;
    data.tradesTotal = _cachedSnap.trades_total;
    data.signalsTotal = _cachedSnap.signals_total;
    data.ordersSent = _cachedSnap.orders_sent;
    data.ordersFilled = _cachedSnap.orders_filled;
    data.ordersCancelled = _cachedSnap.orders_cancelled;
    data.wsReconnects = _cachedSnap.ws_reconnects;
    data.e2eLatencyP50Ns = _cachedSnap.e2e_p50_ns;
    data.e2eLatencyP99Ns = _cachedSnap.e2e_p99_ns;
    data.featLatencyP50Ns = _cachedSnap.feat_p50_ns;
    data.featLatencyP99Ns = _cachedSnap.feat_p99_ns;
    data.modelLatencyP50Ns = _cachedSnap.model_p50_ns;
    data.modelLatencyP99Ns = _cachedSnap.model_p99_ns;
    return data;
}

// ─── Runtime Config ─────────────────────────────────────────────────────────

- (void)setPaperMode:(BOOL)paper {
    if (_engine) tc_engine_set_paper_mode(_engine, paper);
}

- (void)setSignalThreshold:(double)threshold {
    if (_engine) tc_engine_set_signal_threshold(_engine, threshold);
}

- (void)setOrderQty:(double)qty {
    if (_engine) tc_engine_set_order_qty(_engine, qty);
}

- (void)setMaxPosition:(double)maxPos {
    if (_engine) tc_engine_set_max_position(_engine, maxPos);
}

// ─── AI Edition Polling ─────────────────────────────────────────────────────

// C1+E1: Extract from cached snapshot
- (TCRegimeData *)getRegimeState {
    TCRegimeData *data = [[TCRegimeData alloc] init];
    if (!_engine) return data;
    const auto& r = _cachedSnap.regime;
    data.currentRegime = r.current_regime;
    data.previousRegime = r.previous_regime;
    data.confidence = r.confidence;
    data.volatility = r.volatility;
    data.trendScore = r.trend_score;
    data.mrScore = r.mr_score;
    data.liqScore = r.liq_score;
    data.regimeStartNs = r.regime_start_ns;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCPredictionData *)getPrediction {
    TCPredictionData *data = [[TCPredictionData alloc] init];
    if (!_engine) return data;
    const auto& p = _cachedSnap.prediction;
    data.h100msUp = p.h100ms_up; data.h100msDown = p.h100ms_down;
    data.h100msFlat = p.h100ms_flat; data.h100msMove = p.h100ms_move;
    data.h500msUp = p.h500ms_up; data.h500msDown = p.h500ms_down;
    data.h500msFlat = p.h500ms_flat; data.h500msMove = p.h500ms_move;
    data.h1sUp = p.h1s_up; data.h1sDown = p.h1s_down;
    data.h1sFlat = p.h1s_flat; data.h1sMove = p.h1s_move;
    data.h3sUp = p.h3s_up; data.h3sDown = p.h3s_down;
    data.h3sFlat = p.h3s_flat; data.h3sMove = p.h3s_move;
    data.probabilityUp = p.probability_up;
    data.probabilityDown = p.probability_down;
    data.modelConfidence = p.model_confidence;
    data.inferenceLatencyNs = p.inference_latency_ns;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCThresholdData *)getThresholdState {
    TCThresholdData *data = [[TCThresholdData alloc] init];
    if (!_engine) return data;
    const auto& t = _cachedSnap.threshold;
    data.currentThreshold = t.current_threshold;
    data.baseThreshold = t.base_threshold;
    data.volatilityAdj = t.volatility_adj;
    data.accuracyAdj = t.accuracy_adj;
    data.liquidityAdj = t.liquidity_adj;
    data.spreadAdj = t.spread_adj;
    data.recentAccuracy = t.recent_accuracy;
    data.totalSignals = t.total_signals;
    data.correctSignals = t.correct_signals;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCCircuitBreakerData *)getCircuitBreakerState {
    TCCircuitBreakerData *data = [[TCCircuitBreakerData alloc] init];
    if (!_engine) return data;
    data.tripped = _cachedSnap.cb_tripped;
    data.inCooldown = _cachedSnap.cb_cooldown;
    data.drawdownPct = _cachedSnap.cb_drawdown;
    data.consecutiveLosses = _cachedSnap.cb_consec_losses;
    data.peakPnl = _cachedSnap.cb_peak_pnl;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCAccuracyData *)getAccuracy {
    TCAccuracyData *data = [[TCAccuracyData alloc] init];
    if (!_engine) return data;
    const auto& a = _cachedSnap.accuracy;
    data.accuracy = a.accuracy;
    data.totalPredictions = a.total_predictions;
    data.correctPredictions = a.correct_predictions;
    data.precisionUp = a.precision_up;
    data.precisionDown = a.precision_down;
    data.precisionFlat = a.precision_flat;
    data.recallUp = a.recall_up;
    data.recallDown = a.recall_down;
    data.recallFlat = a.recall_flat;
    data.f1Up = a.f1_up;
    data.f1Down = a.f1_down;
    data.f1Flat = a.f1_flat;
    data.rollingAccuracy = a.rolling_accuracy;
    data.rollingWindow = a.rolling_window;
    data.horizonAccuracy100ms = a.horizon_accuracy_100ms;
    data.horizonAccuracy500ms = a.horizon_accuracy_500ms;
    data.horizonAccuracy1s = a.horizon_accuracy_1s;
    data.horizonAccuracy3s = a.horizon_accuracy_3s;
    data.calibrationError = a.calibration_error;
    data.usingOnnx = a.using_onnx;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCStrategyMetricsData *)getStrategyMetrics {
    TCStrategyMetricsData *data = [[TCStrategyMetricsData alloc] init];
    if (!_engine) return data;
    const auto& m = _cachedSnap.strategy_metrics;
    data.sharpeRatio = m.sharpe_ratio;
    data.sortinoRatio = m.sortino_ratio;
    data.maxDrawdownPct = m.max_drawdown_pct;
    data.currentDrawdown = m.current_drawdown;
    data.profitFactor = m.profit_factor;
    data.winRate = m.win_rate;
    data.avgWin = m.avg_win;
    data.avgLoss = m.avg_loss;
    data.expectancy = m.expectancy;
    data.totalPnl = m.total_pnl;
    data.bestTrade = m.best_trade;
    data.worstTrade = m.worst_trade;
    data.totalTrades = m.total_trades;
    data.winningTrades = m.winning_trades;
    data.losingTrades = m.losing_trades;
    data.consecutiveWins = m.consecutive_wins;
    data.consecutiveLosses = m.consecutive_losses;
    data.maxConsecutiveWins = m.max_consecutive_wins;
    data.maxConsecutiveLosses = m.max_consecutive_losses;
    data.dailyPnl = m.daily_pnl;
    data.hourlyPnl = m.hourly_pnl;
    data.calmarRatio = m.calmar_ratio;
    data.recoveryFactor = m.recovery_factor;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCStrategyHealthData *)getStrategyHealth {
    TCStrategyHealthData *data = [[TCStrategyHealthData alloc] init];
    if (!_engine) return data;
    const auto& h = _cachedSnap.strategy_health;
    data.healthLevel = h.health_level;
    data.healthScore = h.health_score;
    data.activityScale = h.activity_scale;
    data.thresholdOffset = h.threshold_offset;
    data.accuracyScore = h.accuracy_score;
    data.pnlScore = h.pnl_score;
    data.drawdownScore = h.drawdown_score;
    data.sharpeScore = h.sharpe_score;
    data.consistencyScore = h.consistency_score;
    data.fillRateScore = h.fill_rate_score;
    data.accuracyDeclining = h.accuracy_declining;
    data.pnlDeclining = h.pnl_declining;
    data.drawdownWarning = h.drawdown_warning;
    data.regimeChanges1h = h.regime_changes_1h;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCSystemMonitorData *)getSystemMonitor {
    TCSystemMonitorData *data = [[TCSystemMonitorData alloc] init];
    if (!_engine) return data;
    const auto& s = _cachedSnap.system_monitor;
    data.cpuUsagePct = s.cpu_usage_pct;
    data.memoryUsedMb = s.memory_used_mb;
    data.memoryPeakBytes = s.memory_peak_bytes;
    data.cpuCores = s.cpu_cores;
    data.wsLatencyP50Us = s.ws_latency_p50_us;
    data.wsLatencyP99Us = s.ws_latency_p99_us;
    data.featLatencyP50Us = s.feat_latency_p50_us;
    data.featLatencyP99Us = s.feat_latency_p99_us;
    data.modelLatencyP50Us = s.model_latency_p50_us;
    data.modelLatencyP99Us = s.model_latency_p99_us;
    data.e2eLatencyP50Us = s.e2e_latency_p50_us;
    data.e2eLatencyP99Us = s.e2e_latency_p99_us;
    data.exchangeLatencyMs = s.exchange_latency_ms;
    data.ticksPerSec = s.ticks_per_sec;
    data.signalsPerSec = s.signals_per_sec;
    data.ordersPerSec = s.orders_per_sec;
    data.uptimeHours = s.uptime_hours;
    data.gpuAvailable = s.gpu_available;
    data.gpuUsagePct = s.gpu_usage_pct;
    data.gpuName = [NSString stringWithUTF8String:s.gpu_name];
    data.inferenceBackend = [NSString stringWithUTF8String:s.inference_backend];
    data.wsRttUs = _cachedSnap.ws_rtt_us;  // E5: from top-level snapshot, not system_monitor
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCRLStateData *)getRLState {
    TCRLStateData *data = [[TCRLStateData alloc] init];
    if (!_engine) return data;
    const auto& r = _cachedSnap.rl_state;
    data.signalThresholdDelta = r.signal_threshold_delta;
    data.positionSizeScale = r.position_size_scale;
    data.orderOffsetBps = r.order_offset_bps;
    data.requoteFreqScale = r.requote_freq_scale;
    data.avgReward = r.avg_reward;
    data.valueEstimate = r.value_estimate;
    data.policyLoss = r.policy_loss;
    data.valueLoss = r.value_loss;
    data.totalSteps = r.total_steps;
    data.totalUpdates = r.total_updates;
    data.exploring = r.exploring;
    return data;
}

// C1+E1: Extract from cached snapshot
- (TCFeatureImportanceData *)getFeatureImportance {
    TCFeatureImportanceData *data = [[TCFeatureImportanceData alloc] init];
    if (!_engine) return data;
    const auto& fi = _cachedSnap.feature_importance;
    NSMutableArray *pi = [NSMutableArray arrayWithCapacity:25];
    NSMutableArray *mi = [NSMutableArray arrayWithCapacity:25];
    NSMutableArray *sv = [NSMutableArray arrayWithCapacity:25];
    NSMutableArray *co = [NSMutableArray arrayWithCapacity:25];
    NSMutableArray *rk = [NSMutableArray arrayWithCapacity:25];
    for (int i = 0; i < 25; ++i) {
        [pi addObject:@(fi.permutation_importance[i])];
        [mi addObject:@(fi.mutual_information[i])];
        [sv addObject:@(fi.shap_value[i])];
        [co addObject:@(fi.correlation[i])];
        [rk addObject:@(fi.ranking[i])];
    }
    data.permutationImportance = pi;
    data.mutualInformation = mi;
    data.shapValue = sv;
    data.correlation = co;
    data.ranking = rk;
    data.activeFeatures = fi.active_features;
    return data;
}

- (void)resetCircuitBreaker {
    if (_engine) tc_engine_reset_circuit_breaker(_engine);
}

// ─── v2.4: Chaos Engine ─────────────────────────────────────────────────────

- (TCChaosStateData *)getChaosState {
    TCChaosStateData *data = [[TCChaosStateData alloc] init];
    if (!_engine) return data;
    @try {
        TCChaosState cs = tc_engine_get_chaos_state(_engine);
        data.enabled = cs.enabled;
        data.latencySpikes = cs.latency_spikes;
        data.packetsDropped = cs.packets_dropped;
        data.fakeDeltasInjected = cs.fake_deltas_injected;
        data.oomSimulations = cs.oom_simulations;
        data.corruptedJsons = cs.corrupted_jsons;
        data.clockSkews = cs.clock_skews;
        data.totalInjectedLatencyNs = cs.total_injected_latency_ns;
        data.maxInjectedLatencyNs = cs.max_injected_latency_ns;
        data.totalInjections = cs.total_injections;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getChaosState: %{public}@", exception.reason);
    }
    return data;
}

- (void)setChaosEnabled:(BOOL)enabled {
    if (_engine) tc_engine_set_chaos_enabled(_engine, enabled);
}

- (void)setChaosNightlyProfile {
    if (_engine) tc_engine_set_chaos_nightly(_engine);
}

- (void)setChaosFlashCrashProfile {
    if (_engine) tc_engine_set_chaos_flash_crash(_engine);
}

- (void)resetChaosStats {
    if (_engine) tc_engine_reset_chaos_stats(_engine);
}

// ─── v2.4: Deterministic Replay ─────────────────────────────────────────────

- (TCReplayStateData *)getReplayState {
    TCReplayStateData *data = [[TCReplayStateData alloc] init];
    if (!_engine) return data;
    @try {
        TCReplayState rs = tc_engine_get_replay_state(_engine);
        data.loaded = rs.loaded;
        data.playing = rs.playing;
        data.eventCount = rs.event_count;
        data.eventsReplayed = rs.events_replayed;
        data.eventsFiltered = rs.events_filtered;
        data.replaySpeed = rs.replay_speed;
        data.checksumValid = rs.checksum_valid;
        data.sequenceMonotonic = rs.sequence_monotonic;
        data.replayDurationNs = rs.replay_duration_ns;
        // C5: loaded_file is now char[256] buffer, always safe to read
        data.loadedFile = [NSString stringWithUTF8String:rs.loaded_file];
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getReplayState: %{public}@", exception.reason);
    }
    return data;
}

- (BOOL)loadReplayFile:(NSString *)path {
    if (!_engine || !path) return NO;
    @try {
        return tc_engine_load_replay(_engine, path.UTF8String) ? YES : NO;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in loadReplayFile: %{public}@", exception.reason);
        return NO;
    }
}

- (void)startReplay {
    if (_engine) tc_engine_start_replay(_engine);
}

- (void)stopReplay {
    if (_engine) tc_engine_stop_replay(_engine);
}

- (void)setReplaySpeed:(double)speed {
    if (_engine) tc_engine_set_replay_speed(_engine, speed);
}

- (void)stepReplay {
    if (_engine) tc_engine_step_replay(_engine);
}

// ─── v2.4: VaR Engine ──────────────────────────────────────────────────────

- (TCVaRData *)getVaRState {
    TCVaRData *data = [[TCVaRData alloc] init];
    if (!_engine) return data;
    @try {
        TCVaRSnapshot v = tc_engine_get_var(_engine);
        data.var95 = v.var_95;
        data.var99 = v.var_99;
        data.cvar95 = v.cvar_95;
        data.cvar99 = v.cvar_99;
        data.parametricVar = v.parametric_var;
        data.historicalVar = v.historical_var;
        data.monteCarloVar = v.monte_carlo_var;
        data.monteCarloSamples = v.monte_carlo_samples;
        data.portfolioValue = v.portfolio_value;
        
        NSMutableArray *losses = [NSMutableArray arrayWithCapacity:8];
        for (int i = 0; i < 8; ++i) {
            [losses addObject:@(v.stress_scenario_losses[i])];
        }
        data.stressScenarioLosses = losses;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getVaRState: %{public}@", exception.reason);
    }
    return data;
}

// ─── v2.4: Order State Machine ──────────────────────────────────────────────

- (TCOSMSummaryData *)getOSMSummary {
    TCOSMSummaryData *data = [[TCOSMSummaryData alloc] init];
    if (!_engine) return data;
    @try {
        TCOSMSummary osm = tc_engine_get_osm_summary(_engine);
        data.activeOrders = osm.active_orders;
        data.totalTransitions = osm.total_transitions;
        data.avgFillTimeUs = osm.avg_fill_time_us;
        data.avgSlippage = osm.avg_slippage;
        data.icebergActive = osm.iceberg_active;
        data.icebergSlicesDone = osm.iceberg_slices_done;
        data.icebergSlicesTotal = osm.iceberg_slices_total;
        data.icebergFilledQty = osm.iceberg_filled_qty;
        data.icebergTotalQty = osm.iceberg_total_qty;
        data.twapActive = osm.twap_active;
        data.twapSlicesDone = osm.twap_slices_done;
        data.twapSlicesTotal = osm.twap_slices_total;
        data.marketImpactBps = osm.market_impact_bps;
        
        NSMutableArray<TCManagedOrderData *> *orders = [NSMutableArray arrayWithCapacity:osm.order_count];
        for (int i = 0; i < osm.order_count; ++i) {
            TCManagedOrderData *o = [[TCManagedOrderData alloc] init];
            o.orderId = [NSString stringWithUTF8String:osm.orders[i].order_id ?: ""];
            o.state = osm.orders[i].state;
            o.isBuy = osm.orders[i].is_buy;
            o.price = osm.orders[i].price;
            o.qty = osm.orders[i].qty;
            o.filledQty = osm.orders[i].filled_qty;
            o.avgFillPrice = osm.orders[i].avg_fill_price;
            o.fillProbability = osm.orders[i].fill_probability;
            o.createdNs = osm.orders[i].created_ns;
            o.lastUpdateNs = osm.orders[i].last_update_ns;
            o.cancelAttempts = osm.orders[i].cancel_attempts;
            [orders addObject:o];
        }
        data.orders = orders;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getOSMSummary: %{public}@", exception.reason);
    }
    return data;
}

// ─── v2.4: RL v2 Extended ───────────────────────────────────────────────────

- (TCRLv2StateData *)getRLv2State {
    TCRLv2StateData *data = [[TCRLv2StateData alloc] init];
    if (!_engine) return data;
    @try {
        TCRLv2State r = tc_engine_get_rl_v2_state(_engine);
        data.entropyAlpha = r.entropy_alpha;
        data.klDivergence = r.kl_divergence;
        data.clipFraction = r.clip_fraction;
        data.approxKl = r.approx_kl;
        data.explainedVariance = r.explained_variance;
        data.rollbackCount = r.rollback_count;
        data.epochsCompleted = r.epochs_completed;
        data.bufferSize = r.buffer_size;
        data.bufferCapacity = r.buffer_capacity;
        data.trainingActive = r.training_active;
        data.learningRate = r.learning_rate;
        
        NSMutableArray *sv = [NSMutableArray arrayWithCapacity:32];
        for (int i = 0; i < 32; ++i) {
            [sv addObject:@(r.state_vector[i])];
        }
        data.stateVector = sv;
        
        NSMutableArray *av = [NSMutableArray arrayWithCapacity:4];
        for (int i = 0; i < 4; ++i) {
            [av addObject:@(r.action_vector[i])];
        }
        data.actionVector = av;
        
        NSMutableArray *rh = [NSMutableArray arrayWithCapacity:r.reward_history_count];
        for (int i = 0; i < r.reward_history_count; ++i) {
            [rh addObject:@(r.reward_history[i])];
        }
        data.rewardHistory = rh;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getRLv2State: %{public}@", exception.reason);
    }
    return data;
}

// ─── v2.4: Per-Stage Histograms ─────────────────────────────────────────────

- (NSArray<TCStageHistogramData *> *)getStageHistograms {
    if (!_engine) return @[];
    @try {
        TCStageHistogramArray arr = tc_engine_get_stage_histograms(_engine);
        NSMutableArray<TCStageHistogramData *> *result = [NSMutableArray arrayWithCapacity:arr.count];
        for (int i = 0; i < arr.count; ++i) {
            TCStageHistogramData *h = [[TCStageHistogramData alloc] init];
            h.stageName = [NSString stringWithUTF8String:arr.stages[i].stage_name ?: ""];
            h.count = arr.stages[i].count;
            h.meanUs = arr.stages[i].mean_us;
            h.p50Us = arr.stages[i].p50_us;
            h.p90Us = arr.stages[i].p90_us;
            h.p95Us = arr.stages[i].p95_us;
            h.p99Us = arr.stages[i].p99_us;
            h.p999Us = arr.stages[i].p999_us;
            h.maxUs = arr.stages[i].max_us;
            h.stddevUs = arr.stages[i].stddev_us;
            [result addObject:h];
        }
        return result;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getStageHistograms: %{public}@", exception.reason);
        return @[];
    }
}

- (BOOL)exportFlamegraph:(NSString *)path {
    if (!_engine || !path) return NO;
    @try {
        return tc_engine_export_flamegraph(_engine, path.UTF8String) ? YES : NO;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in exportFlamegraph: %{public}@", exception.reason);
        return NO;
    }
}

- (BOOL)exportHistogramsCSV:(NSString *)path {
    if (!_engine || !path) return NO;
    @try {
        return tc_engine_export_histograms_csv(_engine, path.UTF8String) ? YES : NO;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in exportHistogramsCSV: %{public}@", exception.reason);
        return NO;
    }
}

// ─── Engine Control ─────────────────────────────────────────────────────────

- (void)emergencyStop {
    if (_engine) {
        os_log_error(bridgeLog(), "EMERGENCY STOP triggered");
        tc_engine_emergency_stop(_engine);
        if ([_delegate respondsToSelector:@selector(engineDidChangeStatus:)]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self->_delegate engineDidChangeStatus:TCEngineStatusIdle];
            });
        }
    }
}

- (BOOL)reloadModel:(NSString *)path {
    if (!_engine || !path) return NO;
    @try {
        bool ok = tc_engine_reload_model(_engine, [path UTF8String]);
        if (ok) {
            os_log_info(bridgeLog(), "Model reloaded from %{public}@", path);
        } else {
            os_log_error(bridgeLog(), "Failed to reload model from %{public}@", path);
        }
        return ok ? YES : NO;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in reloadModel: %{public}@", exception.reason);
        return NO;
    }
}

- (NSDictionary *)getFullSnapshot {
    if (!_engine) return @{};
    @try {
        TCFullSnapshot snap;
        tc_engine_get_snapshot(_engine, &snap);
        
        NSMutableArray *bids = [NSMutableArray arrayWithCapacity:snap.bid_count];
        NSMutableArray *asks = [NSMutableArray arrayWithCapacity:snap.ask_count];
        for (int i = 0; i < snap.bid_count; ++i) {
            [bids addObject:@{@"price": @(snap.bids[i].price), @"qty": @(snap.bids[i].qty)}];
        }
        for (int i = 0; i < snap.ask_count; ++i) {
            [asks addObject:@{@"price": @(snap.asks[i].price), @"qty": @(snap.asks[i].qty)}];
        }
        
        return @{
            @"bids": bids,
            @"asks": asks,
            @"bidCount": @(snap.bid_count),
            @"askCount": @(snap.ask_count),
            @"bestBid": @(snap.best_bid),
            @"bestAsk": @(snap.best_ask),
            @"midPrice": @(snap.mid_price),
            @"spread": @(snap.spread),
            @"microprice": @(snap.microprice),
            @"obValid": @(snap.ob_valid),
            @"posSize": @(snap.pos_size),
            @"posEntry": @(snap.pos_entry),
            @"posUnrealized": @(snap.pos_unrealized),
            @"posRealized": @(snap.pos_realized),
            @"posFunding": @(snap.pos_funding),
            @"posIsLong": @(snap.pos_is_long),
            @"obUpdates": @(snap.ob_updates),
            @"tradesTotal": @(snap.trades_total),
            @"signalsTotal": @(snap.signals_total),
            @"ordersSent": @(snap.orders_sent),
            @"ordersFilled": @(snap.orders_filled),
            @"ordersCancelled": @(snap.orders_cancelled),
            @"wsReconnects": @(snap.ws_reconnects),
            @"e2eP50Ns": @(snap.e2e_p50_ns),
            @"e2eP99Ns": @(snap.e2e_p99_ns),
            @"featP50Ns": @(snap.feat_p50_ns),
            @"featP99Ns": @(snap.feat_p99_ns),
            @"modelP50Ns": @(snap.model_p50_ns),
            @"modelP99Ns": @(snap.model_p99_ns),
            @"cbTripped": @(snap.cb_tripped),
            @"cbCooldown": @(snap.cb_cooldown),
            @"cbDrawdown": @(snap.cb_drawdown),
            @"engineRunning": @(snap.engine_running),
            @"snapshotNs": @(snap.snapshot_ns),
            @"usingOnnx": @(snap.using_onnx),
        };
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getFullSnapshot: %{public}@", exception.reason);
        return @{};
    }
}

@end
