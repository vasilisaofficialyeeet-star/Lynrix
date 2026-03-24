// TradingCoreBridge.mm — Objective-C++ implementation bridging C API to Objective-C
#import "TradingCoreBridge.h"
#include "trading_core_api.h"
#include <os/log.h>

static os_log_t bridgeLog() {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("com.bybittrader.app", "Bridge");
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

@implementation TCConfigObjC
- (instancetype)init {
    self = [super init];
    if (self) {
        _symbol = @"BTCUSDT";
        _paperTrading = YES;
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
    TradingCoreBridge* bridge = (__bridge TradingCoreBridge*)ctx;
    TCEngineStatusObjC objcStatus = (TCEngineStatusObjC)status;
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([bridge.delegate respondsToSelector:@selector(engineDidChangeStatus:)]) {
            [bridge.delegate engineDidChangeStatus:objcStatus];
        }
    });
}

static void logCallbackTrampoline(void* ctx, TCLogLevel level, const char* message) {
    TradingCoreBridge* bridge = (__bridge TradingCoreBridge*)ctx;
    NSString* msg = [NSString stringWithUTF8String:message ?: ""];
    TCLogLevelObjC objcLevel = (TCLogLevelObjC)level;
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([bridge.delegate respondsToSelector:@selector(engineDidReceiveLog:level:)]) {
            [bridge.delegate engineDidReceiveLog:msg level:objcLevel];
        }
    });
}

// ─── Bridge Implementation ──────────────────────────────────────────────────

@interface TradingCoreBridge () {
    TCEngineHandle _engine;
}
@end

@implementation TradingCoreBridge

- (instancetype)initWithConfig:(TCConfigObjC *)config {
    self = [super init];
    if (self) {
        os_log_info(bridgeLog(), "Creating TradingCoreBridge");
        
        @try {
            TCConfig cConfig = {};
            memset(&cConfig, 0, sizeof(TCConfig));
            
            cConfig.symbol = config.symbol ? config.symbol.UTF8String : "BTCUSDT";
            cConfig.paper_trading = config.paperTrading;
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
    os_log_info(bridgeLog(), "TradingCoreBridge dealloc");
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

- (TCOrderBookData *)getOrderBook {
    return [self getOrderBookWithLevels:20];
}

- (TCOrderBookData *)getOrderBookWithLevels:(NSUInteger)maxLevels {
    TCOrderBookData *data = [[TCOrderBookData alloc] init];
    if (!_engine) return data;

    @try {
    TCOrderBookSummary summary = tc_engine_get_ob_summary(_engine);
    data.bestBid = summary.best_bid;
    data.bestAsk = summary.best_ask;
    data.midPrice = summary.mid_price;
    data.spread = summary.spread;
    data.microprice = summary.microprice;
    data.bidCount = summary.bid_count;
    data.askCount = summary.ask_count;
    data.lastUpdateNs = summary.last_update_ns;
    data.valid = summary.valid;

    // Fetch levels
    TCPriceLevel* bidBuf = (TCPriceLevel*)calloc(maxLevels, sizeof(TCPriceLevel));
    TCPriceLevel* askBuf = (TCPriceLevel*)calloc(maxLevels, sizeof(TCPriceLevel));

    size_t bidCount = tc_engine_get_bids(_engine, bidBuf, maxLevels);
    size_t askCount = tc_engine_get_asks(_engine, askBuf, maxLevels);

    NSMutableArray<TCPriceLevelObjC *> *bids = [NSMutableArray arrayWithCapacity:bidCount];
    for (size_t i = 0; i < bidCount; ++i) {
        TCPriceLevelObjC *lvl = [[TCPriceLevelObjC alloc] init];
        lvl.price = bidBuf[i].price;
        lvl.qty = bidBuf[i].qty;
        [bids addObject:lvl];
    }
    data.bids = bids;

    NSMutableArray<TCPriceLevelObjC *> *asks = [NSMutableArray arrayWithCapacity:askCount];
    for (size_t i = 0; i < askCount; ++i) {
        TCPriceLevelObjC *lvl = [[TCPriceLevelObjC alloc] init];
        lvl.price = askBuf[i].price;
        lvl.qty = askBuf[i].qty;
        [asks addObject:lvl];
    }
    data.asks = asks;

    free(bidBuf);
    free(askBuf);
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getOrderBook: %{public}@", exception.reason);
    }

    return data;
}

- (TCPositionData *)getPosition {
    TCPositionData *data = [[TCPositionData alloc] init];
    if (!_engine) return data;

    @try {
        TCPosition pos = tc_engine_get_position(_engine);
        data.size = pos.size;
        data.entryPrice = pos.entry_price;
        data.unrealizedPnl = pos.unrealized_pnl;
        data.realizedPnl = pos.realized_pnl;
        data.fundingImpact = pos.funding_impact;
        data.isLong = pos.is_long;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getPosition: %{public}@", exception.reason);
    }
    return data;
}

- (TCMetricsData *)getMetrics {
    TCMetricsData *data = [[TCMetricsData alloc] init];
    if (!_engine) return data;

    @try {
        TCMetricsSnapshot m = tc_engine_get_metrics(_engine);
        data.obUpdates = m.ob_updates;
        data.tradesTotal = m.trades_total;
        data.signalsTotal = m.signals_total;
        data.ordersSent = m.orders_sent;
        data.ordersFilled = m.orders_filled;
        data.ordersCancelled = m.orders_cancelled;
        data.wsReconnects = m.ws_reconnects;
        data.e2eLatencyP50Ns = m.e2e_latency_p50_ns;
        data.e2eLatencyP99Ns = m.e2e_latency_p99_ns;
        data.featLatencyP50Ns = m.feat_latency_p50_ns;
        data.featLatencyP99Ns = m.feat_latency_p99_ns;
        data.modelLatencyP50Ns = m.model_latency_p50_ns;
        data.modelLatencyP99Ns = m.model_latency_p99_ns;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getMetrics: %{public}@", exception.reason);
    }
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

- (TCRegimeData *)getRegimeState {
    TCRegimeData *data = [[TCRegimeData alloc] init];
    if (!_engine) return data;
    @try {
        TCRegimeState r = tc_engine_get_regime(_engine);
        data.currentRegime = r.current_regime;
        data.previousRegime = r.previous_regime;
        data.confidence = r.confidence;
        data.volatility = r.volatility;
        data.trendScore = r.trend_score;
        data.mrScore = r.mr_score;
        data.liqScore = r.liq_score;
        data.regimeStartNs = r.regime_start_ns;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getRegimeState: %{public}@", exception.reason);
    }
    return data;
}

- (TCPredictionData *)getPrediction {
    TCPredictionData *data = [[TCPredictionData alloc] init];
    if (!_engine) return data;
    @try {
        TCModelPrediction p = tc_engine_get_prediction(_engine);
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
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getPrediction: %{public}@", exception.reason);
    }
    return data;
}

- (TCThresholdData *)getThresholdState {
    TCThresholdData *data = [[TCThresholdData alloc] init];
    if (!_engine) return data;
    @try {
        TCAdaptiveThresholdState t = tc_engine_get_threshold_state(_engine);
        data.currentThreshold = t.current_threshold;
        data.baseThreshold = t.base_threshold;
        data.volatilityAdj = t.volatility_adj;
        data.accuracyAdj = t.accuracy_adj;
        data.liquidityAdj = t.liquidity_adj;
        data.spreadAdj = t.spread_adj;
        data.recentAccuracy = t.recent_accuracy;
        data.totalSignals = t.total_signals;
        data.correctSignals = t.correct_signals;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getThresholdState: %{public}@", exception.reason);
    }
    return data;
}

- (TCCircuitBreakerData *)getCircuitBreakerState {
    TCCircuitBreakerData *data = [[TCCircuitBreakerData alloc] init];
    if (!_engine) return data;
    @try {
        TCCircuitBreakerState cb = tc_engine_get_circuit_breaker(_engine);
        data.tripped = cb.tripped;
        data.inCooldown = cb.in_cooldown;
        data.drawdownPct = cb.drawdown_pct;
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getCircuitBreakerState: %{public}@", exception.reason);
    }
    return data;
}

- (TCAccuracyData *)getAccuracy {
    TCAccuracyData *data = [[TCAccuracyData alloc] init];
    if (!_engine) return data;
    @try {
        TCAccuracyMetrics a = tc_engine_get_accuracy(_engine);
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
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getAccuracy: %{public}@", exception.reason);
    }
    return data;
}

- (TCStrategyMetricsData *)getStrategyMetrics {
    TCStrategyMetricsData *data = [[TCStrategyMetricsData alloc] init];
    if (!_engine) return data;
    @try {
        TCStrategyMetrics m = tc_engine_get_strategy_metrics(_engine);
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
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getStrategyMetrics: %{public}@", exception.reason);
    }
    return data;
}

- (TCStrategyHealthData *)getStrategyHealth {
    TCStrategyHealthData *data = [[TCStrategyHealthData alloc] init];
    if (!_engine) return data;
    @try {
        TCStrategyHealth h = tc_engine_get_strategy_health(_engine);
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
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getStrategyHealth: %{public}@", exception.reason);
    }
    return data;
}

- (TCSystemMonitorData *)getSystemMonitor {
    TCSystemMonitorData *data = [[TCSystemMonitorData alloc] init];
    if (!_engine) return data;
    @try {
        TCSystemMonitor s = tc_engine_get_system_monitor(_engine);
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
        data.gpuName = s.gpu_name ? [NSString stringWithUTF8String:s.gpu_name] : @"N/A";
        data.inferenceBackend = s.inference_backend ? [NSString stringWithUTF8String:s.inference_backend] : @"CPU";
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getSystemMonitor: %{public}@", exception.reason);
    }
    return data;
}

- (TCRLStateData *)getRLState {
    TCRLStateData *data = [[TCRLStateData alloc] init];
    if (!_engine) return data;
    @try {
        TCRLState r = tc_engine_get_rl_state(_engine);
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
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getRLState: %{public}@", exception.reason);
    }
    return data;
}

- (TCFeatureImportanceData *)getFeatureImportance {
    TCFeatureImportanceData *data = [[TCFeatureImportanceData alloc] init];
    if (!_engine) return data;
    @try {
        TCFeatureImportance fi = tc_engine_get_feature_importance(_engine);
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
    } @catch (NSException *exception) {
        os_log_error(bridgeLog(), "Exception in getFeatureImportance: %{public}@", exception.reason);
    }
    return data;
}

- (void)resetCircuitBreaker {
    if (_engine) tc_engine_reset_circuit_breaker(_engine);
}

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
        
        // Convert bids/asks to arrays
        NSMutableArray *bids = [NSMutableArray arrayWithCapacity:snap.bid_count];
        NSMutableArray *asks = [NSMutableArray arrayWithCapacity:snap.ask_count];
        for (int i = 0; i < snap.bid_count; ++i) {
            [bids addObject:@{@"price": @(snap.bids[i].price), @"qty": @(snap.bids[i].qty)}];
        }
        for (int i = 0; i < snap.ask_count; ++i) {
            [asks addObject:@{@"price": @(snap.asks[i].price), @"qty": @(snap.asks[i].qty)}];
        }
        
        return @{
            // OrderBook
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
            // Position
            @"posSize": @(snap.pos_size),
            @"posEntry": @(snap.pos_entry),
            @"posUnrealized": @(snap.pos_unrealized),
            @"posRealized": @(snap.pos_realized),
            @"posFunding": @(snap.pos_funding),
            @"posIsLong": @(snap.pos_is_long),
            // Metrics
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
            // Circuit Breaker
            @"cbTripped": @(snap.cb_tripped),
            @"cbCooldown": @(snap.cb_cooldown),
            @"cbDrawdown": @(snap.cb_drawdown),
            // Engine state
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
