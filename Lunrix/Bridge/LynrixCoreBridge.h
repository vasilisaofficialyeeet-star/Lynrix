// LynrixCoreBridge.h — Objective-C wrapper around the C API for Swift interop (v2.4)
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// ─── Data Models ────────────────────────────────────────────────────────────

@interface TCPriceLevelObjC : NSObject
@property (nonatomic, assign) double price;
@property (nonatomic, assign) double qty;
@end

@interface TCOrderBookData : NSObject
@property (nonatomic, assign) double bestBid;
@property (nonatomic, assign) double bestAsk;
@property (nonatomic, assign) double midPrice;
@property (nonatomic, assign) double spread;
@property (nonatomic, assign) double microprice;
@property (nonatomic, assign) NSUInteger bidCount;
@property (nonatomic, assign) NSUInteger askCount;
@property (nonatomic, assign) uint64_t lastUpdateNs;
@property (nonatomic, assign) BOOL valid;
@property (nonatomic, strong) NSArray<TCPriceLevelObjC *> *bids;
@property (nonatomic, strong) NSArray<TCPriceLevelObjC *> *asks;
@end

@interface TCPositionData : NSObject
@property (nonatomic, assign) double size;
@property (nonatomic, assign) double entryPrice;
@property (nonatomic, assign) double unrealizedPnl;
@property (nonatomic, assign) double realizedPnl;
@property (nonatomic, assign) double fundingImpact;
@property (nonatomic, assign) BOOL isLong;
@end

@interface TCMetricsData : NSObject
@property (nonatomic, assign) uint64_t obUpdates;
@property (nonatomic, assign) uint64_t tradesTotal;
@property (nonatomic, assign) uint64_t signalsTotal;
@property (nonatomic, assign) uint64_t ordersSent;
@property (nonatomic, assign) uint64_t ordersFilled;
@property (nonatomic, assign) uint64_t ordersCancelled;
@property (nonatomic, assign) uint64_t wsReconnects;
@property (nonatomic, assign) uint64_t e2eLatencyP50Ns;
@property (nonatomic, assign) uint64_t e2eLatencyP99Ns;
@property (nonatomic, assign) uint64_t featLatencyP50Ns;
@property (nonatomic, assign) uint64_t featLatencyP99Ns;
@property (nonatomic, assign) uint64_t modelLatencyP50Ns;
@property (nonatomic, assign) uint64_t modelLatencyP99Ns;
@end

@interface TCSignalData : NSObject
@property (nonatomic, assign) BOOL isBuy;
@property (nonatomic, assign) double price;
@property (nonatomic, assign) double qty;
@property (nonatomic, assign) double confidence;
@property (nonatomic, assign) uint64_t timestampNs;
@property (nonatomic, assign) int regime;
@property (nonatomic, assign) double fillProb;
@property (nonatomic, assign) double expectedPnl;
@end

// ─── AI Edition Data Models ─────────────────────────────────────────────────

@interface TCRegimeData : NSObject
@property (nonatomic, assign) int currentRegime;
@property (nonatomic, assign) int previousRegime;
@property (nonatomic, assign) double confidence;
@property (nonatomic, assign) double volatility;
@property (nonatomic, assign) double trendScore;
@property (nonatomic, assign) double mrScore;
@property (nonatomic, assign) double liqScore;
@property (nonatomic, assign) uint64_t regimeStartNs;
@end

@interface TCPredictionData : NSObject
@property (nonatomic, assign) double h100msUp, h100msDown, h100msFlat, h100msMove;
@property (nonatomic, assign) double h500msUp, h500msDown, h500msFlat, h500msMove;
@property (nonatomic, assign) double h1sUp,    h1sDown,    h1sFlat,    h1sMove;
@property (nonatomic, assign) double h3sUp,    h3sDown,    h3sFlat,    h3sMove;
@property (nonatomic, assign) double probabilityUp;
@property (nonatomic, assign) double probabilityDown;
@property (nonatomic, assign) double modelConfidence;
@property (nonatomic, assign) uint64_t inferenceLatencyNs;
@end

@interface TCThresholdData : NSObject
@property (nonatomic, assign) double currentThreshold;
@property (nonatomic, assign) double baseThreshold;
@property (nonatomic, assign) double volatilityAdj;
@property (nonatomic, assign) double accuracyAdj;
@property (nonatomic, assign) double liquidityAdj;
@property (nonatomic, assign) double spreadAdj;
@property (nonatomic, assign) double recentAccuracy;
@property (nonatomic, assign) int totalSignals;
@property (nonatomic, assign) int correctSignals;
@end

@interface TCCircuitBreakerData : NSObject
@property (nonatomic, assign) BOOL tripped;
@property (nonatomic, assign) BOOL inCooldown;
@property (nonatomic, assign) double drawdownPct;
@property (nonatomic, assign) int consecutiveLosses;
@property (nonatomic, assign) double peakPnl;
@end

@interface TCAccuracyData : NSObject
@property (nonatomic, assign) double accuracy;
@property (nonatomic, assign) int totalPredictions;
@property (nonatomic, assign) int correctPredictions;
@property (nonatomic, assign) double precisionUp, precisionDown, precisionFlat;
@property (nonatomic, assign) double recallUp, recallDown, recallFlat;
@property (nonatomic, assign) double f1Up, f1Down, f1Flat;
@property (nonatomic, assign) double rollingAccuracy;
@property (nonatomic, assign) int rollingWindow;
@property (nonatomic, assign) double horizonAccuracy100ms;
@property (nonatomic, assign) double horizonAccuracy500ms;
@property (nonatomic, assign) double horizonAccuracy1s;
@property (nonatomic, assign) double horizonAccuracy3s;
@property (nonatomic, assign) double calibrationError;
@property (nonatomic, assign) BOOL usingOnnx;
@end

@interface TCStrategyMetricsData : NSObject
@property (nonatomic, assign) double sharpeRatio;
@property (nonatomic, assign) double sortinoRatio;
@property (nonatomic, assign) double maxDrawdownPct;
@property (nonatomic, assign) double currentDrawdown;
@property (nonatomic, assign) double profitFactor;
@property (nonatomic, assign) double winRate;
@property (nonatomic, assign) double avgWin;
@property (nonatomic, assign) double avgLoss;
@property (nonatomic, assign) double expectancy;
@property (nonatomic, assign) double totalPnl;
@property (nonatomic, assign) double bestTrade;
@property (nonatomic, assign) double worstTrade;
@property (nonatomic, assign) int totalTrades;
@property (nonatomic, assign) int winningTrades;
@property (nonatomic, assign) int losingTrades;
@property (nonatomic, assign) int consecutiveWins;
@property (nonatomic, assign) int consecutiveLosses;
@property (nonatomic, assign) int maxConsecutiveWins;
@property (nonatomic, assign) int maxConsecutiveLosses;
@property (nonatomic, assign) double dailyPnl;
@property (nonatomic, assign) double hourlyPnl;
@property (nonatomic, assign) double calmarRatio;
@property (nonatomic, assign) double recoveryFactor;
@end

@interface TCStrategyHealthData : NSObject
@property (nonatomic, assign) int healthLevel;
@property (nonatomic, assign) double healthScore;
@property (nonatomic, assign) double activityScale;
@property (nonatomic, assign) double thresholdOffset;
@property (nonatomic, assign) double accuracyScore;
@property (nonatomic, assign) double pnlScore;
@property (nonatomic, assign) double drawdownScore;
@property (nonatomic, assign) double sharpeScore;
@property (nonatomic, assign) double consistencyScore;
@property (nonatomic, assign) double fillRateScore;
@property (nonatomic, assign) BOOL accuracyDeclining;
@property (nonatomic, assign) BOOL pnlDeclining;
@property (nonatomic, assign) BOOL drawdownWarning;
@property (nonatomic, assign) int regimeChanges1h;
@end

@interface TCSystemMonitorData : NSObject
@property (nonatomic, assign) double cpuUsagePct;
@property (nonatomic, assign) double memoryUsedMb;
@property (nonatomic, assign) uint64_t memoryPeakBytes;
@property (nonatomic, assign) int cpuCores;
@property (nonatomic, assign) double wsLatencyP50Us;
@property (nonatomic, assign) double wsLatencyP99Us;
@property (nonatomic, assign) double featLatencyP50Us;
@property (nonatomic, assign) double featLatencyP99Us;
@property (nonatomic, assign) double modelLatencyP50Us;
@property (nonatomic, assign) double modelLatencyP99Us;
@property (nonatomic, assign) double e2eLatencyP50Us;
@property (nonatomic, assign) double e2eLatencyP99Us;
@property (nonatomic, assign) double exchangeLatencyMs;
@property (nonatomic, assign) double ticksPerSec;
@property (nonatomic, assign) double signalsPerSec;
@property (nonatomic, assign) double ordersPerSec;
@property (nonatomic, assign) double uptimeHours;
@property (nonatomic, assign) BOOL gpuAvailable;
@property (nonatomic, assign) double gpuUsagePct;
@property (nonatomic, copy) NSString *gpuName;
@property (nonatomic, copy) NSString *inferenceBackend;
@property (nonatomic, assign) uint64_t wsRttUs;  // E5: WebSocket round-trip time
@end

@interface TCRLStateData : NSObject
@property (nonatomic, assign) double signalThresholdDelta;
@property (nonatomic, assign) double positionSizeScale;
@property (nonatomic, assign) double orderOffsetBps;
@property (nonatomic, assign) double requoteFreqScale;
@property (nonatomic, assign) double avgReward;
@property (nonatomic, assign) double valueEstimate;
@property (nonatomic, assign) double policyLoss;
@property (nonatomic, assign) double valueLoss;
@property (nonatomic, assign) int totalSteps;
@property (nonatomic, assign) int totalUpdates;
@property (nonatomic, assign) BOOL exploring;
@end

@interface TCFeatureImportanceData : NSObject
@property (nonatomic, strong) NSArray<NSNumber *> *permutationImportance;
@property (nonatomic, strong) NSArray<NSNumber *> *mutualInformation;
@property (nonatomic, strong) NSArray<NSNumber *> *shapValue;
@property (nonatomic, strong) NSArray<NSNumber *> *correlation;
@property (nonatomic, strong) NSArray<NSNumber *> *ranking;
@property (nonatomic, assign) int activeFeatures;
@end

// ─── v2.4: Chaos Engine Data ──────────────────────────────────────────────

@interface TCChaosStateData : NSObject
@property (nonatomic, assign) BOOL enabled;
@property (nonatomic, assign) uint64_t latencySpikes;
@property (nonatomic, assign) uint64_t packetsDropped;
@property (nonatomic, assign) uint64_t fakeDeltasInjected;
@property (nonatomic, assign) uint64_t oomSimulations;
@property (nonatomic, assign) uint64_t corruptedJsons;
@property (nonatomic, assign) uint64_t clockSkews;
@property (nonatomic, assign) uint64_t totalInjectedLatencyNs;
@property (nonatomic, assign) uint64_t maxInjectedLatencyNs;
@property (nonatomic, assign) uint64_t totalInjections;
@end

// ─── v2.4: Replay Data ─────────────────────────────────────────────────────

@interface TCReplayStateData : NSObject
@property (nonatomic, assign) BOOL loaded;
@property (nonatomic, assign) BOOL playing;
@property (nonatomic, assign) uint64_t eventCount;
@property (nonatomic, assign) uint64_t eventsReplayed;
@property (nonatomic, assign) uint64_t eventsFiltered;
@property (nonatomic, assign) double replaySpeed;
@property (nonatomic, assign) BOOL checksumValid;
@property (nonatomic, assign) BOOL sequenceMonotonic;
@property (nonatomic, assign) uint64_t replayDurationNs;
@property (nonatomic, copy) NSString *loadedFile;
@end

// ─── v2.4: VaR Data ────────────────────────────────────────────────────────

@interface TCVaRData : NSObject
@property (nonatomic, assign) double var95;
@property (nonatomic, assign) double var99;
@property (nonatomic, assign) double cvar95;
@property (nonatomic, assign) double cvar99;
@property (nonatomic, assign) double parametricVar;
@property (nonatomic, assign) double historicalVar;
@property (nonatomic, assign) double monteCarloVar;
@property (nonatomic, strong) NSArray<NSNumber *> *stressScenarioLosses;
@property (nonatomic, strong) NSArray<NSString *> *stressScenarioNames;
@property (nonatomic, assign) int monteCarloSamples;
@property (nonatomic, assign) double portfolioValue;
@end

// ─── v2.4: Order State Machine Data ─────────────────────────────────────────

@interface TCManagedOrderData : NSObject
@property (nonatomic, copy) NSString *orderId;
@property (nonatomic, assign) int state;  // 0=New..7=Expired
@property (nonatomic, assign) BOOL isBuy;
@property (nonatomic, assign) double price;
@property (nonatomic, assign) double qty;
@property (nonatomic, assign) double filledQty;
@property (nonatomic, assign) double avgFillPrice;
@property (nonatomic, assign) double fillProbability;
@property (nonatomic, assign) uint64_t createdNs;
@property (nonatomic, assign) uint64_t lastUpdateNs;
@property (nonatomic, assign) int cancelAttempts;
@end

@interface TCOSMSummaryData : NSObject
@property (nonatomic, assign) int activeOrders;
@property (nonatomic, assign) int totalTransitions;
@property (nonatomic, assign) double avgFillTimeUs;
@property (nonatomic, assign) double avgSlippage;
@property (nonatomic, assign) BOOL icebergActive;
@property (nonatomic, assign) int icebergSlicesDone;
@property (nonatomic, assign) int icebergSlicesTotal;
@property (nonatomic, assign) double icebergFilledQty;
@property (nonatomic, assign) double icebergTotalQty;
@property (nonatomic, assign) BOOL twapActive;
@property (nonatomic, assign) int twapSlicesDone;
@property (nonatomic, assign) int twapSlicesTotal;
@property (nonatomic, assign) double marketImpactBps;
@property (nonatomic, strong) NSArray<TCManagedOrderData *> *orders;
@end

// ─── v2.4: RL v2 (PPO+SAC Hybrid) Extended Data ────────────────────────────

@interface TCRLv2StateData : NSObject
@property (nonatomic, assign) double entropyAlpha;
@property (nonatomic, assign) double klDivergence;
@property (nonatomic, assign) double clipFraction;
@property (nonatomic, assign) double approxKl;
@property (nonatomic, assign) double explainedVariance;
@property (nonatomic, assign) int rollbackCount;
@property (nonatomic, assign) int epochsCompleted;
@property (nonatomic, assign) int bufferSize;
@property (nonatomic, assign) int bufferCapacity;
@property (nonatomic, assign) BOOL trainingActive;
@property (nonatomic, assign) double learningRate;
@property (nonatomic, strong) NSArray<NSNumber *> *stateVector;     // 32-dim
@property (nonatomic, strong) NSArray<NSNumber *> *actionVector;    // 4-dim
@property (nonatomic, strong) NSArray<NSNumber *> *rewardHistory;   // last 100
@end

// ─── v2.4: Per-Stage Histogram Data ─────────────────────────────────────────

@interface TCStageHistogramData : NSObject
@property (nonatomic, copy) NSString *stageName;
@property (nonatomic, assign) uint64_t count;
@property (nonatomic, assign) double meanUs;
@property (nonatomic, assign) double p50Us;
@property (nonatomic, assign) double p90Us;
@property (nonatomic, assign) double p95Us;
@property (nonatomic, assign) double p99Us;
@property (nonatomic, assign) double p999Us;
@property (nonatomic, assign) double maxUs;
@property (nonatomic, assign) double stddevUs;
@end

// ─── Engine Status ──────────────────────────────────────────────────────────

typedef NS_ENUM(NSInteger, TCEngineStatusObjC) {
    TCEngineStatusIdle       = 0,
    TCEngineStatusConnecting = 1,
    TCEngineStatusConnected  = 2,
    TCEngineStatusTrading    = 3,
    TCEngineStatusError      = 4,
    TCEngineStatusStopping   = 5,
};

typedef NS_ENUM(NSInteger, TCLogLevelObjC) {
    TCLogLevelDebug = 0,
    TCLogLevelInfo  = 1,
    TCLogLevelWarn  = 2,
    TCLogLevelError = 3,
};

// ─── Configuration ──────────────────────────────────────────────────────────

@interface TCConfigObjC : NSObject
@property (nonatomic, copy) NSString *symbol;
@property (nonatomic, assign) BOOL paperTrading;
@property (nonatomic, assign) double paperFillRate;  // R3: probability gate [0.0, 1.0]
@property (nonatomic, copy, nullable) NSString *wsPublicUrl;
@property (nonatomic, copy, nullable) NSString *wsPrivateUrl;
@property (nonatomic, copy, nullable) NSString *restBaseUrl;
@property (nonatomic, assign) double orderQty;
@property (nonatomic, assign) double signalThreshold;
@property (nonatomic, assign) int signalTtlMs;
@property (nonatomic, assign) double entryOffsetBps;
@property (nonatomic, assign) double maxPositionSize;
@property (nonatomic, assign) double maxLeverage;
@property (nonatomic, assign) double maxDailyLoss;
@property (nonatomic, assign) double maxDrawdown;
@property (nonatomic, assign) int maxOrdersPerSec;
@property (nonatomic, assign) double modelBias;
@property (nonatomic, strong, nullable) NSArray<NSNumber *> *modelWeights;
// AI Edition
@property (nonatomic, copy, nullable) NSString *mlModelPath;
@property (nonatomic, assign) BOOL mlModelEnabled;
@property (nonatomic, copy, nullable) NSString *onnxModelPath;
@property (nonatomic, assign) BOOL onnxEnabled;
@property (nonatomic, assign) int onnxIntraThreads;
@property (nonatomic, assign) BOOL adaptiveThresholdEnabled;
@property (nonatomic, assign) double adaptiveThresholdMin;
@property (nonatomic, assign) double adaptiveThresholdMax;
@property (nonatomic, assign) BOOL regimeDetectionEnabled;
@property (nonatomic, assign) BOOL requoteEnabled;
@property (nonatomic, assign) int requoteIntervalMs;
@property (nonatomic, assign) BOOL fillProbEnabled;
@property (nonatomic, assign) double fillProbMarketThreshold;
@property (nonatomic, assign) BOOL adaptiveSizingEnabled;
@property (nonatomic, assign) double baseOrderQty;
@property (nonatomic, assign) double minOrderQty;
@property (nonatomic, assign) double maxOrderQty;
@property (nonatomic, assign) BOOL cbEnabled;
@property (nonatomic, assign) double cbLossThreshold;
@property (nonatomic, assign) double cbDrawdownThreshold;
@property (nonatomic, assign) int cbConsecutiveLosses;
@property (nonatomic, assign) int cbCooldownSec;
@property (nonatomic, assign) BOOL recordObSnapshots;
@property (nonatomic, assign) BOOL recordFeatures;
@property (nonatomic, copy, nullable) NSString *logDir;
@property (nonatomic, assign) int obLevels;
@property (nonatomic, assign) int ioThreads;
@property (nonatomic, assign) int featureTickMs;
@end

// ─── Callback Protocols ─────────────────────────────────────────────────────

@protocol TCEngineDelegate <NSObject>
@optional
- (void)engineDidChangeStatus:(TCEngineStatusObjC)status;
- (void)engineDidReceiveLog:(NSString *)message level:(TCLogLevelObjC)level;
- (void)engineDidUpdateOrderBook:(TCOrderBookData *)data;
- (void)engineDidReceiveSignal:(TCSignalData *)signal;
@end

// ─── Engine Bridge ──────────────────────────────────────────────────────────

@interface LynrixCoreBridge : NSObject

@property (nonatomic, weak, nullable) id<TCEngineDelegate> delegate;
@property (nonatomic, readonly) TCEngineStatusObjC status;

- (instancetype)initWithConfig:(TCConfigObjC *)config;
- (void)setAPIKey:(NSString *)key secret:(NSString *)secret;
- (BOOL)start;
- (void)stop;

// C1+E1: Snapshot-based polling (single atomic read, zero data races)
// Call refreshSnapshot once per poll cycle, then read sub-objects.
- (void)refreshSnapshot;
- (uint64_t)snapshotVersion;

// Polling — all extract from cached snapshot after refreshSnapshot
- (TCOrderBookData *)getOrderBook;
- (TCOrderBookData *)getOrderBookWithLevels:(NSUInteger)maxLevels;
- (TCPositionData *)getPosition;
- (TCMetricsData *)getMetrics;

// Runtime config
- (void)setPaperMode:(BOOL)paper;
- (void)setSignalThreshold:(double)threshold;
- (void)setOrderQty:(double)qty;
- (void)setMaxPosition:(double)maxPos;

// AI Edition polling
- (TCRegimeData *)getRegimeState;
- (TCPredictionData *)getPrediction;
- (TCThresholdData *)getThresholdState;
- (TCCircuitBreakerData *)getCircuitBreakerState;
- (TCAccuracyData *)getAccuracy;
- (TCStrategyMetricsData *)getStrategyMetrics;
- (TCStrategyHealthData *)getStrategyHealth;
- (TCSystemMonitorData *)getSystemMonitor;
- (TCRLStateData *)getRLState;
- (TCFeatureImportanceData *)getFeatureImportance;
- (void)resetCircuitBreaker;

// v2.4: Chaos Engine control
- (TCChaosStateData *)getChaosState;
- (void)setChaosEnabled:(BOOL)enabled;
- (void)setChaosNightlyProfile;
- (void)setChaosFlashCrashProfile;
- (void)resetChaosStats;

// v2.4: Deterministic Replay
- (TCReplayStateData *)getReplayState;
- (BOOL)loadReplayFile:(NSString *)path;
- (void)startReplay;
- (void)stopReplay;
- (void)setReplaySpeed:(double)speed;
- (void)stepReplay;

// v2.4: VaR Engine
- (TCVaRData *)getVaRState;

// v2.4: Order State Machine
- (TCOSMSummaryData *)getOSMSummary;

// v2.4: RL v2 Extended
- (TCRLv2StateData *)getRLv2State;

// v2.4: Per-stage histograms
- (NSArray<TCStageHistogramData *> *)getStageHistograms;
- (BOOL)exportFlamegraph:(NSString *)path;
- (BOOL)exportHistogramsCSV:(NSString *)path;

// Engine control
- (void)emergencyStop;
- (BOOL)reloadModel:(NSString *)path;

// Legacy NSDictionary snapshot (deprecated — use refreshSnapshot + typed getters)
- (NSDictionary *)getFullSnapshot;

@end

NS_ASSUME_NONNULL_END
