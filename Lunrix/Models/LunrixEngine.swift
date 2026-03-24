// LynrixEngine.swift — Swift wrapper around Obj-C++ bridge (Lynrix v2.5)
// Manages engine lifecycle and polls data on a timer for UI updates

import Foundation
import Combine
import os.log

private let logger = Logger(subsystem: "com.lynrix.trader", category: "LynrixEngine")

// MARK: - Swift Data Models

struct PriceLevelModel: Identifiable, Equatable {
    let id: Int
    let price: Double
    let qty: Double
}

struct OrderBookSnapshot: Equatable {
    var bestBid: Double = 0
    var bestAsk: Double = 0
    var midPrice: Double = 0
    var spread: Double = 0
    var microprice: Double = 0
    var bidCount: Int = 0
    var askCount: Int = 0
    var valid: Bool = false
    var bids: [PriceLevelModel] = []
    var asks: [PriceLevelModel] = []
}

struct PositionSnapshot: Equatable {
    var size: Double = 0
    var entryPrice: Double = 0
    var unrealizedPnl: Double = 0
    var realizedPnl: Double = 0
    var fundingImpact: Double = 0
    var isLong: Bool = true
    
    var netPnl: Double { realizedPnl + unrealizedPnl + fundingImpact }
    var hasPosition: Bool { abs(size) > 1e-12 }
}

struct MetricsSnapshot: Equatable {
    var obUpdates: UInt64 = 0
    var tradesTotal: UInt64 = 0
    var signalsTotal: UInt64 = 0
    var ordersSent: UInt64 = 0
    var ordersFilled: UInt64 = 0
    var ordersCancelled: UInt64 = 0
    var wsReconnects: UInt64 = 0
    var e2eLatencyP50Us: Double = 0
    var e2eLatencyP99Us: Double = 0
    var featLatencyP50Us: Double = 0
    var featLatencyP99Us: Double = 0
    var modelLatencyP50Us: Double = 0
    var modelLatencyP99Us: Double = 0
}

// MARK: - AI Edition Models

struct RegimeSnapshot: Equatable {
    var current: Int = 0
    var previous: Int = 0
    var confidence: Double = 0
    var volatility: Double = 0
    var trendScore: Double = 0
    var mrScore: Double = 0
    var liqScore: Double = 0
    
    var regimeLocKey: String {
        LxColor.regimeLocKey(current)
    }
    
    var regimeColor: String {
        switch current {
        case 0: return "green"
        case 1: return "red"
        case 2: return "blue"
        case 3: return "purple"
        case 4: return "orange"
        default: return "gray"
        }
    }
}

struct PredictionSnapshot: Equatable {
    var probUp: Double = 0.333
    var probDown: Double = 0.333
    var modelConfidence: Double = 0
    var inferenceLatencyUs: Double = 0
    var h100ms: (up: Double, down: Double, flat: Double) = (0.333, 0.333, 0.334)
    var h500ms: (up: Double, down: Double, flat: Double) = (0.333, 0.333, 0.334)
    var h1s: (up: Double, down: Double, flat: Double) = (0.333, 0.333, 0.334)
    var h3s: (up: Double, down: Double, flat: Double) = (0.333, 0.333, 0.334)
    
    static func == (lhs: PredictionSnapshot, rhs: PredictionSnapshot) -> Bool {
        lhs.probUp == rhs.probUp && lhs.probDown == rhs.probDown &&
        lhs.modelConfidence == rhs.modelConfidence
    }
    
    var direction: String {
        if probUp > probDown + 0.05 { return "↑" }
        if probDown > probUp + 0.05 { return "↓" }
        return "→"
    }
}

struct ThresholdSnapshot: Equatable {
    var currentThreshold: Double = 0.6
    var baseThreshold: Double = 0.6
    var volatilityAdj: Double = 0
    var accuracyAdj: Double = 0
    var liquidityAdj: Double = 0
    var spreadAdj: Double = 0
    var recentAccuracy: Double = 0.5
    var totalSignals: Int = 0
    var correctSignals: Int = 0
}

struct CircuitBreakerSnapshot: Equatable {
    var tripped: Bool = false
    var inCooldown: Bool = false
    var drawdownPct: Double = 0
    var consecutiveLosses: Int = 0
    var peakPnl: Double = 0
}

struct AccuracySnapshot: Equatable {
    var accuracy: Double = 0
    var totalPredictions: Int = 0
    var correctPredictions: Int = 0
    var precisionUp: Double = 0
    var precisionDown: Double = 0
    var precisionFlat: Double = 0
    var recallUp: Double = 0
    var recallDown: Double = 0
    var recallFlat: Double = 0
    var f1Up: Double = 0
    var f1Down: Double = 0
    var f1Flat: Double = 0
    var rollingAccuracy: Double = 0
    var rollingWindow: Int = 200
    var horizonAccuracy100ms: Double = 0
    var horizonAccuracy500ms: Double = 0
    var horizonAccuracy1s: Double = 0
    var horizonAccuracy3s: Double = 0
    var calibrationError: Double = 0
    var usingOnnx: Bool = false
}

// MARK: - Strategy & System Models

struct StrategyMetricsModel: Equatable {
    var sharpeRatio: Double = 0
    var sortinoRatio: Double = 0
    var maxDrawdownPct: Double = 0
    var currentDrawdown: Double = 0
    var profitFactor: Double = 0
    var winRate: Double = 0
    var avgWin: Double = 0
    var avgLoss: Double = 0
    var expectancy: Double = 0
    var totalPnl: Double = 0
    var bestTrade: Double = 0
    var worstTrade: Double = 0
    var totalTrades: Int = 0
    var winningTrades: Int = 0
    var losingTrades: Int = 0
    var consecutiveWins: Int = 0
    var consecutiveLosses: Int = 0
    var maxConsecutiveWins: Int = 0
    var maxConsecutiveLosses: Int = 0
    var dailyPnl: Double = 0
    var hourlyPnl: Double = 0
    var calmarRatio: Double = 0
    var recoveryFactor: Double = 0
}

struct StrategyHealthModel: Equatable {
    var healthLevel: Int = 1
    var healthScore: Double = 1.0
    var activityScale: Double = 1.0
    var thresholdOffset: Double = 0
    var accuracyScore: Double = 0
    var pnlScore: Double = 0
    var drawdownScore: Double = 0
    var sharpeScore: Double = 0
    var consistencyScore: Double = 0
    var fillRateScore: Double = 0
    var accuracyDeclining: Bool = false
    var pnlDeclining: Bool = false
    var drawdownWarning: Bool = false
    var regimeChanges1h: Int = 0
    
    var levelLocKey: String {
        switch healthLevel {
        case 0: return "health.excellent"
        case 1: return "health.good"
        case 2: return "health.warning"
        case 3: return "health.critical"
        case 4: return "health.halted"
        default: return "health.unknown"
        }
    }
    
    var levelColor: String {
        switch healthLevel {
        case 0: return "green"
        case 1: return "blue"
        case 2: return "orange"
        case 3: return "red"
        case 4: return "gray"
        default: return "gray"
        }
    }
}

struct SystemMonitorModel: Equatable {
    var cpuUsagePct: Double = 0
    var memoryUsedMb: Double = 0
    var memoryPeakBytes: UInt64 = 0
    var cpuCores: Int = 0
    var wsLatencyP50Us: Double = 0
    var wsLatencyP99Us: Double = 0
    var featLatencyP50Us: Double = 0
    var featLatencyP99Us: Double = 0
    var modelLatencyP50Us: Double = 0
    var modelLatencyP99Us: Double = 0
    var e2eLatencyP50Us: Double = 0
    var e2eLatencyP99Us: Double = 0
    var exchangeLatencyMs: Double = 0
    var ticksPerSec: Double = 0
    var signalsPerSec: Double = 0
    var ordersPerSec: Double = 0
    var uptimeHours: Double = 0
    var gpuAvailable: Bool = false
    var gpuUsagePct: Double = 0
    var gpuName: String = "N/A"
    var inferenceBackend: String = "CPU"
}

struct RLStateModel: Equatable {
    var signalThresholdDelta: Double = 0
    var positionSizeScale: Double = 1.0
    var orderOffsetBps: Double = 0
    var requoteFreqScale: Double = 1.0
    var avgReward: Double = 0
    var valueEstimate: Double = 0
    var policyLoss: Double = 0
    var valueLoss: Double = 0
    var totalSteps: Int = 0
    var totalUpdates: Int = 0
    var exploring: Bool = true
}

struct FeatureImportanceModel: Equatable {
    var permutationImportance: [Double] = Array(repeating: 0, count: 25)
    var mutualInformation: [Double] = Array(repeating: 0, count: 25)
    var shapValue: [Double] = Array(repeating: 0, count: 25)
    var correlation: [Double] = Array(repeating: 0, count: 25)
    var ranking: [Int] = Array(0..<25)
    var activeFeatures: Int = 0
}

// MARK: - v2.5 Models

struct ChaosStateModel: Equatable {
    var enabled: Bool = false
    var latencySpikes: UInt64 = 0
    var packetsDropped: UInt64 = 0
    var fakeDeltasInjected: UInt64 = 0
    var oomSimulations: UInt64 = 0
    var corruptedJsons: UInt64 = 0
    var clockSkews: UInt64 = 0
    var totalInjectedLatencyNs: UInt64 = 0
    var maxInjectedLatencyNs: UInt64 = 0
    var totalInjections: UInt64 = 0
}

struct ReplayStateModel: Equatable {
    var loaded: Bool = false
    var playing: Bool = false
    var eventCount: UInt64 = 0
    var eventsReplayed: UInt64 = 0
    var eventsFiltered: UInt64 = 0
    var replaySpeed: Double = 1.0
    var checksumValid: Bool = true
    var sequenceMonotonic: Bool = true
    var replayDurationNs: UInt64 = 0
    var loadedFile: String = ""
    
    var progress: Double {
        guard eventCount > 0 else { return 0 }
        return Double(eventsReplayed) / Double(eventCount)
    }
}

struct VaRStateModel: Equatable {
    var var95: Double = 0
    var var99: Double = 0
    var cvar95: Double = 0
    var cvar99: Double = 0
    var parametricVar: Double = 0
    var historicalVar: Double = 0
    var monteCarloVar: Double = 0
    var stressScenarioLosses: [Double] = Array(repeating: 0, count: 8)
    var stressScenarioNames: [String] = [
        "Flash Crash -10%", "Vol Spike 3x", "Liquidity Drought",
        "Correlated Sell-off", "Rate Shock +200bp", "FX Dislocation",
        "Crypto Contagion", "Black Swan -25%"
    ]
    var monteCarloSamples: Int = 0
    var portfolioValue: Double = 0
}

struct ManagedOrderModel: Identifiable, Equatable {
    let id: String
    var state: Int = 0
    var isBuy: Bool = true
    var price: Double = 0
    var qty: Double = 0
    var filledQty: Double = 0
    var avgFillPrice: Double = 0
    var fillProbability: Double = 0
    var createdNs: UInt64 = 0
    var lastUpdateNs: UInt64 = 0
    var cancelAttempts: Int = 0
    
    var stateLocKey: String {
        switch state {
        case 0: return "order.state.unknown"
        case 1: return "order.state.pending"
        case 2: return "order.state.open"
        case 3: return "order.state.partialFill"
        case 4: return "order.state.filled"
        case 5: return "order.state.cancelling"
        case 6: return "order.state.cancelled"
        case 7: return "order.state.expired"
        default: return "order.state.unknown"
        }
    }
    
    var stateColor: String {
        switch state {
        case 0: return "blue"
        case 1: return "yellow"
        case 2: return "green"
        case 3: return "cyan"
        case 4: return "green"
        case 5: return "orange"
        case 6: return "gray"
        case 7: return "gray"
        default: return "gray"
        }
    }
}

struct OSMSummaryModel: Equatable {
    var activeOrders: Int = 0
    var totalTransitions: Int = 0
    var avgFillTimeUs: Double = 0
    var avgSlippage: Double = 0
    var icebergActive: Bool = false
    var icebergSlicesDone: Int = 0
    var icebergSlicesTotal: Int = 0
    var icebergFilledQty: Double = 0
    var icebergTotalQty: Double = 0
    var twapActive: Bool = false
    var twapSlicesDone: Int = 0
    var twapSlicesTotal: Int = 0
    var marketImpactBps: Double = 0
    var orders: [ManagedOrderModel] = []
}

struct RLv2StateModel: Equatable {
    var entropyAlpha: Double = 0.01
    var klDivergence: Double = 0
    var clipFraction: Double = 0
    var approxKl: Double = 0
    var explainedVariance: Double = 0
    var rollbackCount: Int = 0
    var epochsCompleted: Int = 0
    var bufferSize: Int = 0
    var bufferCapacity: Int = 0
    var trainingActive: Bool = false
    var learningRate: Double = 3e-4
    var stateVector: [Double] = Array(repeating: 0, count: 32)
    var actionVector: [Double] = Array(repeating: 0, count: 4)
    var rewardHistory: [Double] = []
}

struct StageHistogramModel: Identifiable, Equatable {
    let id: String
    var stageName: String = ""
    var count: UInt64 = 0
    var meanUs: Double = 0
    var p50Us: Double = 0
    var p90Us: Double = 0
    var p95Us: Double = 0
    var p99Us: Double = 0
    var p999Us: Double = 0
    var maxUs: Double = 0
    var stddevUs: Double = 0
    
    var jitterAlert: Bool { p99Us > 100 || maxUs > 1000 }
}

// MARK: - Common Models

struct LogEntry: Identifiable, Equatable {
    let id: UUID
    let timestamp: Date
    let level: LogLevel
    let message: String
    
    enum LogLevel: String {
        case debug = "DEBUG"
        case info = "INFO"
        case warn = "WARN"
        case error = "ERROR"
        
        var color: String {
            switch self {
            case .debug: return "gray"
            case .info: return "primary"
            case .warn: return "orange"
            case .error: return "red"
            }
        }
    }
}

struct SignalEntry: Identifiable, Equatable {
    let id: UUID
    let timestamp: Date
    let isBuy: Bool
    let price: Double
    let qty: Double
    let confidence: Double
    var regime: Int = 0
    var fillProb: Double = 0
    var expectedPnl: Double = 0
}

struct TradeEntry: Identifiable, Equatable {
    let id: UUID
    let timestamp: Date
    let price: Double
    let qty: Double
    let isBuyerMaker: Bool
    
    var side: String { isBuyerMaker ? "SELL" : "BUY" }
    var sideColor: String { isBuyerMaker ? "red" : "green" }
}

enum EngineStatus: Int, Equatable {
    case idle = 0
    case connecting = 1
    case connected = 2
    case trading = 3
    case error = 4
    case stopping = 5
    
    var locKey: String {
        switch self {
        case .idle:       return "engine.status.idle"
        case .connecting: return "engine.status.connecting"
        case .connected:  return "engine.status.connected"
        case .trading:    return "engine.status.trading"
        case .error:      return "engine.status.error"
        case .stopping:   return "engine.status.stopping"
        }
    }
    
    var isActive: Bool {
        self == .trading || self == .connecting || self == .connected
    }
}

// MARK: - LynrixEngine

final class LynrixEngine: NSObject, ObservableObject, TCEngineDelegate {
    
    // PERF: Only rarely-changing state uses @Published (triggers objectWillChange per-mutation).
    // High-churn state is plain vars — poll() sends ONE objectWillChange.send() per cycle.
    @Published var status: EngineStatus = .idle
    @Published var paperMode: Bool = true
    @Published var tradingMode: TradingMode = .paper
    @Published var killSwitch: KillSwitchState = .init()
    @Published var showPanicAlert: Bool = false
    @Published var panicMessage: String = ""
    @Published var isReconnecting: Bool = false
    
    // High-churn state — updated every poll cycle, coalesced into single objectWillChange
    var orderBook: OrderBookSnapshot = .init()
    var position: PositionSnapshot = .init()
    var metrics: MetricsSnapshot = .init()
    var logs: [LogEntry] = []
    var signals: [SignalEntry] = []
    var trades: [TradeEntry] = []
    
    // AI Edition state (high-churn)
    var regime: RegimeSnapshot = .init()
    var prediction: PredictionSnapshot = .init()
    var threshold: ThresholdSnapshot = .init()
    var circuitBreaker: CircuitBreakerSnapshot = .init()
    var pnlHistory: [Double] = []
    var drawdownHistory: [Double] = []
    var accuracyHistory: [Double] = []
    var accuracy: AccuracySnapshot = .init()
    
    // Strategy & System state (high-churn)
    var strategyMetrics: StrategyMetricsModel = .init()
    var strategyHealth: StrategyHealthModel = .init()
    var systemMonitor: SystemMonitorModel = .init()
    var rlState: RLStateModel = .init()
    var featureImportance: FeatureImportanceModel = .init()
    
    // v2.5 state (high-churn)
    var chaosState: ChaosStateModel = .init()
    var replayState: ReplayStateModel = .init()
    var varState: VaRStateModel = .init()
    var osmSummary: OSMSummaryModel = .init()
    var rlv2State: RLv2StateModel = .init()
    var stageHistograms: [StageHistogramModel] = []
    
    // Sprint 3: Execution Intelligence (high-churn)
    var executionAnalytics: ExecutionAnalytics = .init()
    private var previousAnalytics: ExecutionAnalytics = .init()
    private var execAnalyticsPollCount: Int = 0
    private var slowPollCount: Int = 0  // Sprint 4: throttle slow-changing state
    
    // M6: Focused submodels — views can @ObservedObject these for targeted redraws
    let marketData = MarketDataState()
    let positionState = PositionState()
    let aiState = AIState()
    let executionState = ExecutionSubstate()
    let strategyState = StrategySubstate()
    let systemState = SystemSubstate()
    
    private var bridge: LynrixCoreBridge?
    private var pollTimer: DispatchSourceTimer?
    private var lastReconnectCount: UInt64 = 0
    private let maxLogs = 500
    private let maxSignals = 200
    private let maxTrades = 500
    private let pollIntervalFast: TimeInterval = 0.1   // D-A6: 10 FPS when actively trading
    private let pollIntervalSlow: TimeInterval = 0.333 // D-A6:  3 FPS when idle
    private var currentPollInterval: TimeInterval = 0.1
    private var lastStartConfig: (key: String?, secret: String?)?
    private var lastSnapshotVersion: UInt64 = 0  // M2: version-based poll skip
    private var idlePollTicks: Int = 0            // D-A6: consecutive idle polls
    private var lastSignalsTotal: UInt64 = 0      // D-A6: track signal activity
    
    // Configuration — @Published so SettingsView can observe changes
    @Published var config: TradingConfig {
        didSet {
            applyConfig()
            ProfileManager.shared.saveCurrentConfig(self)
        }
    }
    
    struct TradingConfig {
        var symbol: String = "BTCUSDT"
        var paperTrading: Bool = true
        var paperFillRate: Double = 0.85  // R3: paper fill gate
        var orderQty: Double = 0.001
        var signalThreshold: Double = 0.6
        var signalTtlMs: Int = 300
        var entryOffsetBps: Double = 1.0
        var maxPositionSize: Double = 0.1
        var maxLeverage: Double = 10.0
        var maxDailyLoss: Double = 500.0
        var maxDrawdown: Double = 0.1
        var maxOrdersPerSec: Int = 5
        var obLevels: Int = 500
        var ioThreads: Int = 2
        var logDir: String = "./logs"
        // AI Edition
        var mlModelEnabled: Bool = true
        var adaptiveThresholdEnabled: Bool = true
        var regimeDetectionEnabled: Bool = true
        var requoteEnabled: Bool = true
        var adaptiveSizingEnabled: Bool = true
        var cbEnabled: Bool = true
        var cbLossThreshold: Double = 200.0
        var cbDrawdownThreshold: Double = 0.05
        var cbConsecutiveLosses: Int = 10
        var cbCooldownSec: Int = 300
        var featureTickMs: Int = 10
        // ONNX
        var onnxEnabled: Bool = false
        var onnxModelPath: String = ""
        var onnxIntraThreads: Int = 4
    }
    
    init(config: TradingConfig = TradingConfig()) {
        self.config = config
        super.init()
        logger.info("LynrixEngine initialized")
        addLog(.info, "Lynrix v2.5.0 — The Ideal Edition initialized")
        addLog(.info, "System: \(ProcessInfo.processInfo.operatingSystemVersionString)")
        addLog(.info, "Memory: \(ProcessInfo.processInfo.physicalMemory / 1_073_741_824) GB")
        addLog(.info, "CPU Cores: \(ProcessInfo.processInfo.processorCount)")
    }
    
    deinit {
        logger.info("LynrixEngine deinit")
        stop()
    }
    
    // MARK: - Lifecycle
    
    func start(apiKey: String? = nil, apiSecret: String? = nil) {
        guard status == .idle || status == .error else {
            logger.warning("start() called in invalid state: \(self.status.rawValue)")
            return
        }
        
        if killSwitch.globalHalt {
            logger.error("start() blocked — global kill switch active: \(self.killSwitch.globalHaltReason)")
            addLog(.error, "Cannot start — global kill switch active")
            return
        }
        
        logger.info("Starting engine: symbol=\(self.config.symbol) paper=\(self.config.paperTrading)")
        addLog(.info, "Starting engine...")
        addLog(.info, "Symbol: \(config.symbol) | Paper: \(config.paperTrading) | Qty: \(config.orderQty)")
        
        let objcConfig = TCConfigObjC()
        objcConfig.symbol = config.symbol
        objcConfig.paperTrading = config.paperTrading
        objcConfig.paperFillRate = config.paperFillRate
        objcConfig.orderQty = config.orderQty
        objcConfig.signalThreshold = config.signalThreshold
        objcConfig.signalTtlMs = Int32(config.signalTtlMs)
        objcConfig.entryOffsetBps = config.entryOffsetBps
        objcConfig.maxPositionSize = config.maxPositionSize
        objcConfig.maxLeverage = config.maxLeverage
        objcConfig.maxDailyLoss = config.maxDailyLoss
        objcConfig.maxDrawdown = config.maxDrawdown
        objcConfig.maxOrdersPerSec = Int32(config.maxOrdersPerSec)
        objcConfig.obLevels = Int32(config.obLevels)
        objcConfig.ioThreads = Int32(config.ioThreads)
        objcConfig.logDir = config.logDir
        objcConfig.featureTickMs = Int32(config.featureTickMs)
        
        // AI Edition config
        objcConfig.mlModelEnabled = config.mlModelEnabled
        objcConfig.adaptiveThresholdEnabled = config.adaptiveThresholdEnabled
        objcConfig.regimeDetectionEnabled = config.regimeDetectionEnabled
        objcConfig.requoteEnabled = config.requoteEnabled
        objcConfig.adaptiveSizingEnabled = config.adaptiveSizingEnabled
        objcConfig.cbEnabled = config.cbEnabled
        objcConfig.cbLossThreshold = config.cbLossThreshold
        objcConfig.cbDrawdownThreshold = config.cbDrawdownThreshold
        objcConfig.cbConsecutiveLosses = Int32(config.cbConsecutiveLosses)
        objcConfig.cbCooldownSec = Int32(config.cbCooldownSec)
        objcConfig.onnxEnabled = config.onnxEnabled
        objcConfig.onnxModelPath = config.onnxModelPath.isEmpty ? nil : config.onnxModelPath
        objcConfig.onnxIntraThreads = Int32(config.onnxIntraThreads)
        
        bridge = LynrixCoreBridge(config: objcConfig)
        guard bridge != nil else {
            logger.error("Bridge creation returned nil")
            addLog(.error, "Bridge creation failed — C++ engine not initialized")
            status = .error
            return
        }
        bridge?.delegate = self
        logger.info("Bridge created successfully")
        
        // D-F1: Unified key loading — auto-load from Keychain if not provided
        let resolvedKey: String = {
            if let k = apiKey, !k.isEmpty { return k }
            return KeychainManager.shared.loadAPIKey() ?? ""
        }()
        let resolvedSecret: String = {
            if let s = apiSecret, !s.isEmpty { return s }
            return KeychainManager.shared.loadAPISecret() ?? ""
        }()
        
        if !resolvedKey.isEmpty && !resolvedSecret.isEmpty {
            bridge?.setAPIKey(resolvedKey, secret: resolvedSecret)
            addLog(.info, "API keys set (key length: \(resolvedKey.count))")
        } else {
            addLog(.warn, "No API keys — private channels unavailable")
        }
        
        paperMode = config.paperTrading
        lastStartConfig = (key: apiKey, secret: apiSecret)
        
        if bridge?.start() == true {
            startPolling()
            logger.info("Engine started successfully")
            addLog(.info, "Engine started — polling at \(Int(1.0/pollIntervalFast)) FPS (adaptive)")
        } else {
            logger.error("Engine start() returned false")
            addLog(.error, "Engine start failed — check C++ logs")
            status = .error
            bridge = nil
        }
    }
    
    func stop() {
        logger.info("Stopping engine...")
        stopPolling()
        bridge?.stop()
        bridge = nil
        status = .idle
        lastSnapshotVersion = 0  // M2: reset so restart doesn't skip first poll
        addLog(.info, "Engine stopped")
        logger.info("Engine stopped")
    }
    
    // MARK: - Engine Control
    
    func emergencyStop() {
        logger.error("EMERGENCY STOP triggered by user")
        addLog(.error, "EMERGENCY STOP — cancelling all orders")
        IncidentStore.shared.record(
            severity: .critical,
            category: .killSwitch,
            titleKey: "incident.emergencyStop",
            detail: "Manual emergency stop by user"
        )
        stopPolling()
        bridge?.emergencyStop()
        bridge = nil
        status = .idle
    }
    
    func reloadModel(path: String? = nil) {
        guard let bridge = bridge else {
            addLog(.error, "Cannot reload model — engine not running")
            return
        }
        let modelPath = path ?? config.onnxModelPath
        guard !modelPath.isEmpty else {
            addLog(.error, "Model path not specified")
            return
        }
        addLog(.info, "Reloading model: \(modelPath)")
        if bridge.reloadModel(modelPath) {
            addLog(.info, "Model reloaded successfully")
        } else {
            addLog(.error, "Model reload failed")
        }
    }
    
    func restart() {
        logger.info("Restarting engine...")
        addLog(.info, "Restarting engine...")
        stop()
        // D-F3: Clear histories on restart to avoid stale chart data
        pnlHistory.removeAll()
        drawdownHistory.removeAll()
        accuracyHistory.removeAll()
        objectWillChange.send()
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in
            guard let self = self else { return }
            self.start(apiKey: self.lastStartConfig?.key,
                       apiSecret: self.lastStartConfig?.secret)
        }
    }
    
    // MARK: - v2.5: Chaos Engine Control
    
    func enableChaosNightly() {
        bridge?.setChaosNightlyProfile()
        bridge?.setChaosEnabled(true)
        addLog(.warn, "Chaos Engine enabled (Nightly profile)")
    }
    
    func enableChaosFlashCrash() {
        bridge?.setChaosFlashCrashProfile()
        bridge?.setChaosEnabled(true)
        addLog(.warn, "Chaos Engine enabled (Flash Crash profile)")
    }
    
    func disableChaos() {
        bridge?.setChaosEnabled(false)
        addLog(.info, "Chaos Engine disabled")
    }
    
    func resetChaosStats() {
        bridge?.resetChaosStats()
        addLog(.info, "Chaos stats reset")
    }
    
    // MARK: - v2.5: Replay Control
    
    func loadReplayFile(url: URL) {
        guard let bridge = bridge else {
            addLog(.error, "Cannot load replay — engine not running")
            return
        }
        if bridge.loadReplayFile(url.path) {
            addLog(.info, "Replay loaded: \(url.lastPathComponent)")
        } else {
            addLog(.error, "Failed to load replay: \(url.lastPathComponent)")
        }
    }
    
    func startReplay() {
        bridge?.startReplay()
        addLog(.info, "Replay started")
    }
    
    func stopReplay() {
        bridge?.stopReplay()
        addLog(.info, "Replay stopped")
    }
    
    func setReplaySpeed(_ speed: Double) {
        bridge?.setReplaySpeed(speed)
    }
    
    func stepReplay() {
        bridge?.stepReplay()
    }
    
    // MARK: - v2.5: Export
    
    func exportFlamegraph(to url: URL) {
        if bridge?.exportFlamegraph(url.path) == true {
            addLog(.info, "Flamegraph exported to \(url.lastPathComponent)")
        } else {
            addLog(.error, "Flamegraph export failed")
        }
    }
    
    func exportHistograms(to url: URL) {
        if bridge?.exportHistogramsCSV(url.path) == true {
            addLog(.info, "Histograms CSV exported to \(url.lastPathComponent)")
        } else {
            addLog(.error, "Histograms CSV export failed")
        }
    }
    
    // MARK: - Polling
    
    private func startPolling() {
        currentPollInterval = pollIntervalFast
        idlePollTicks = 0
        rescheduleTimer()
    }
    
    private func rescheduleTimer() {
        pollTimer?.cancel()
        let timer = DispatchSource.makeTimerSource(queue: DispatchQueue.main)
        timer.schedule(deadline: .now(), repeating: currentPollInterval)
        timer.setEventHandler { [weak self] in
            self?.poll()
        }
        pollTimer = timer
        timer.resume()
    }
    
    private func stopPolling() {
        pollTimer?.cancel()
        pollTimer = nil
    }
    
    private func poll() {
        guard let bridge = bridge else {
            logger.warning("poll() called but bridge is nil")
            stopPolling()
            return
        }
        
        // M2: Version-based snapshot skip — single atomic read per cycle
        let ver = bridge.snapshotVersion()
        if ver == lastSnapshotVersion && ver != 0 { return }
        bridge.refreshSnapshot()
        lastSnapshotVersion = ver
        
        // Update orderbook
        let obData = bridge.getOrderBook(withLevels: 20)
        let newOB = OrderBookSnapshot(
            bestBid: obData.bestBid,
            bestAsk: obData.bestAsk,
            midPrice: obData.midPrice,
            spread: obData.spread,
            microprice: obData.microprice,
            bidCount: Int(obData.bidCount),
            askCount: Int(obData.askCount),
            valid: obData.valid,
            bids: obData.bids.enumerated().map { PriceLevelModel(id: $0.offset, price: $0.element.price, qty: $0.element.qty) },
            asks: obData.asks.enumerated().map { PriceLevelModel(id: $0.offset, price: $0.element.price, qty: $0.element.qty) }
        )
        if newOB != orderBook { orderBook = newOB; marketData.orderBook = newOB }
        
        // Update position
        let posData = bridge.getPosition()
        let newPos = PositionSnapshot(
            size: posData.size,
            entryPrice: posData.entryPrice,
            unrealizedPnl: posData.unrealizedPnl,
            realizedPnl: posData.realizedPnl,
            fundingImpact: posData.fundingImpact,
            isLong: posData.isLong
        )
        if newPos != position {
            // C6: Use circuit breaker loss threshold for loss notification (was incorrectly maxPositionSize * orderQty)
            let lossThreshold = config.cbLossThreshold * 0.5
            if lossThreshold > 0 {
                NotificationManager.shared.notifyLargeLoss(pnl: newPos.unrealizedPnl, threshold: lossThreshold)
            }
            position = newPos; positionState.position = newPos
        }
        
        // Update metrics
        let metData = bridge.getMetrics()
        let newMet = MetricsSnapshot(
            obUpdates: metData.obUpdates,
            tradesTotal: metData.tradesTotal,
            signalsTotal: metData.signalsTotal,
            ordersSent: metData.ordersSent,
            ordersFilled: metData.ordersFilled,
            ordersCancelled: metData.ordersCancelled,
            wsReconnects: metData.wsReconnects,
            e2eLatencyP50Us: Double(metData.e2eLatencyP50Ns) / 1000.0,
            e2eLatencyP99Us: Double(metData.e2eLatencyP99Ns) / 1000.0,
            featLatencyP50Us: Double(metData.featLatencyP50Ns) / 1000.0,
            featLatencyP99Us: Double(metData.featLatencyP99Ns) / 1000.0,
            modelLatencyP50Us: Double(metData.modelLatencyP50Ns) / 1000.0,
            modelLatencyP99Us: Double(metData.modelLatencyP99Ns) / 1000.0
        )
        if newMet != metrics {
            if newMet.wsReconnects > lastReconnectCount {
                isReconnecting = true
                addLog(.warn, "WebSocket reconnecting... (\(newMet.wsReconnects) total)")
                // D3: Notify if disconnected while holding a position
                NotificationManager.shared.notifyDisconnectWithPosition(positionSize: position.size)
                DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) { [weak self] in
                    self?.isReconnecting = false
                }
            }
            lastReconnectCount = newMet.wsReconnects
            metrics = newMet
            executionState.metrics = newMet
        }
        
        // AI + v2.5 polling
        pollAIState()
        pollV24State()
        
        // PERF P0: Single coalesced notification for ALL poll updates.
        // High-churn vars are no longer @Published, so we send ONE objectWillChange here.
        objectWillChange.send()
        
        // D-A6: Adaptive poll rate — drop to 3 FPS when idle, 10 FPS when active
        let hasNewSignals = metrics.signalsTotal > lastSignalsTotal
        lastSignalsTotal = metrics.signalsTotal
        let isActive = position.size != 0 || hasNewSignals
        if isActive {
            idlePollTicks = 0
            if currentPollInterval != pollIntervalFast {
                currentPollInterval = pollIntervalFast
                rescheduleTimer()
            }
        } else {
            idlePollTicks += 1
            if idlePollTicks > 30 && currentPollInterval != pollIntervalSlow {
                currentPollInterval = pollIntervalSlow
                rescheduleTimer()
            }
        }
    }
    
    private func pollAIState() {
        guard let bridge = bridge else { return }
        
        // Regime
        let regData = bridge.getRegimeState()
        let newRegime = RegimeSnapshot(
            current: Int(regData.currentRegime),
            previous: Int(regData.previousRegime),
            confidence: regData.confidence,
            volatility: regData.volatility,
            trendScore: regData.trendScore,
            mrScore: regData.mrScore,
            liqScore: regData.liqScore
        )
        if newRegime != regime { regime = newRegime; aiState.regime = newRegime }
        MarketRegimeIntelligenceStore.shared.update(regime: newRegime)
        
        // Prediction
        let predData = bridge.getPrediction()
        let newPred = PredictionSnapshot(
            probUp: predData.probabilityUp,
            probDown: predData.probabilityDown,
            modelConfidence: predData.modelConfidence,
            inferenceLatencyUs: Double(predData.inferenceLatencyNs) / 1000.0,
            h100ms: (predData.h100msUp, predData.h100msDown, predData.h100msFlat),
            h500ms: (predData.h500msUp, predData.h500msDown, predData.h500msFlat),
            h1s: (predData.h1sUp, predData.h1sDown, predData.h1sFlat),
            h3s: (predData.h3sUp, predData.h3sDown, predData.h3sFlat)
        )
        if newPred != prediction { prediction = newPred; aiState.prediction = newPred }
        
        // Threshold
        let thData = bridge.getThresholdState()
        let newTh = ThresholdSnapshot(
            currentThreshold: thData.currentThreshold,
            baseThreshold: thData.baseThreshold,
            volatilityAdj: thData.volatilityAdj,
            accuracyAdj: thData.accuracyAdj,
            liquidityAdj: thData.liquidityAdj,
            spreadAdj: thData.spreadAdj,
            recentAccuracy: thData.recentAccuracy,
            totalSignals: Int(thData.totalSignals),
            correctSignals: Int(thData.correctSignals)
        )
        if newTh != threshold { threshold = newTh; aiState.threshold = newTh }
        
        // Circuit breaker
        let cbData = bridge.getCircuitBreakerState()
        let newCB = CircuitBreakerSnapshot(
            tripped: cbData.tripped,
            inCooldown: cbData.inCooldown,
            drawdownPct: cbData.drawdownPct,
            consecutiveLosses: Int(cbData.consecutiveLosses),
            peakPnl: cbData.peakPnl
        )
        if newCB != circuitBreaker {
            if newCB.tripped && !circuitBreaker.tripped {
                addLog(.error, "CIRCUIT BREAKER TRIPPED")
                killSwitch.circuitBreakerHalt = true
                IncidentStore.shared.record(
                    severity: .critical,
                    category: .circuitBreaker,
                    titleKey: "incident.cbTripped",
                    detail: "Drawdown: \(String(format: "%.2f%%", newCB.drawdownPct * 100)), Consec losses: \(newCB.consecutiveLosses)"
                )
                triggerPanic("Circuit Breaker tripped — trading halted")
                // D3: Push macOS notification for CB trip
                NotificationManager.shared.notifyCircuitBreakerTripped()
            } else if !newCB.tripped && circuitBreaker.tripped {
                killSwitch.circuitBreakerHalt = false
                IncidentStore.shared.record(
                    severity: .info,
                    category: .circuitBreaker,
                    titleKey: "incident.cbReset"
                )
            }
            circuitBreaker = newCB
            aiState.circuitBreaker = newCB
        }
        
        // Accuracy
        let accData = bridge.getAccuracy()
        let newAcc = AccuracySnapshot(
            accuracy: accData.accuracy,
            totalPredictions: Int(accData.totalPredictions),
            correctPredictions: Int(accData.correctPredictions),
            precisionUp: accData.precisionUp,
            precisionDown: accData.precisionDown,
            precisionFlat: accData.precisionFlat,
            recallUp: accData.recallUp,
            recallDown: accData.recallDown,
            recallFlat: accData.recallFlat,
            f1Up: accData.f1Up,
            f1Down: accData.f1Down,
            f1Flat: accData.f1Flat,
            rollingAccuracy: accData.rollingAccuracy,
            rollingWindow: Int(accData.rollingWindow),
            horizonAccuracy100ms: accData.horizonAccuracy100ms,
            horizonAccuracy500ms: accData.horizonAccuracy500ms,
            horizonAccuracy1s: accData.horizonAccuracy1s,
            horizonAccuracy3s: accData.horizonAccuracy3s,
            calibrationError: accData.calibrationError,
            usingOnnx: accData.usingOnnx
        )
        if newAcc != accuracy { accuracy = newAcc; aiState.accuracy = newAcc }
        
        // Sprint 4: Throttle slow-changing state to every 3rd poll (~300ms)
        // History arrays, strategy, system monitor, RL, feature importance
        slowPollCount += 1
        let isSlowPoll = slowPollCount >= 3
        if isSlowPoll { slowPollCount = 0 }
        
        // Track histories (throttled — reduces chart re-renders)
        if isSlowPoll {
            let net = position.netPnl
            if pnlHistory.isEmpty || pnlHistory.last != net {
                pnlHistory.append(net)
                if pnlHistory.count > 200 { pnlHistory.removeFirst() }
            }
            let dd = circuitBreaker.drawdownPct
            if drawdownHistory.isEmpty || drawdownHistory.last != dd {
                drawdownHistory.append(dd)
                if drawdownHistory.count > 200 { drawdownHistory.removeFirst() }
            }
            let acc = accuracy.rollingAccuracy
            if accuracyHistory.isEmpty || accuracyHistory.last != acc {
                accuracyHistory.append(acc)
                if accuracyHistory.count > 200 { accuracyHistory.removeFirst() }
            }
        }
        
        // Strategy metrics (throttled)
        guard isSlowPoll else { return }
        do {
            let smData = bridge.getStrategyMetrics()
            let newSM = StrategyMetricsModel(
                sharpeRatio: smData.sharpeRatio, sortinoRatio: smData.sortinoRatio,
                maxDrawdownPct: smData.maxDrawdownPct, currentDrawdown: smData.currentDrawdown,
                profitFactor: smData.profitFactor, winRate: smData.winRate,
                avgWin: smData.avgWin, avgLoss: smData.avgLoss,
                expectancy: smData.expectancy, totalPnl: smData.totalPnl,
                bestTrade: smData.bestTrade, worstTrade: smData.worstTrade,
                totalTrades: Int(smData.totalTrades), winningTrades: Int(smData.winningTrades),
                losingTrades: Int(smData.losingTrades), consecutiveWins: Int(smData.consecutiveWins),
                consecutiveLosses: Int(smData.consecutiveLosses),
                maxConsecutiveWins: Int(smData.maxConsecutiveWins),
                maxConsecutiveLosses: Int(smData.maxConsecutiveLosses),
                dailyPnl: smData.dailyPnl, hourlyPnl: smData.hourlyPnl,
                calmarRatio: smData.calmarRatio, recoveryFactor: smData.recoveryFactor
            )
            if newSM != strategyMetrics { strategyMetrics = newSM; strategyState.strategyMetrics = newSM }
        }
        
        // Strategy health
        do {
            let shData = bridge.getStrategyHealth()
            let newSH = StrategyHealthModel(
                healthLevel: Int(shData.healthLevel), healthScore: shData.healthScore,
                activityScale: shData.activityScale, thresholdOffset: shData.thresholdOffset,
                accuracyScore: shData.accuracyScore, pnlScore: shData.pnlScore,
                drawdownScore: shData.drawdownScore, sharpeScore: shData.sharpeScore,
                consistencyScore: shData.consistencyScore, fillRateScore: shData.fillRateScore,
                accuracyDeclining: shData.accuracyDeclining, pnlDeclining: shData.pnlDeclining,
                drawdownWarning: shData.drawdownWarning, regimeChanges1h: Int(shData.regimeChanges1h)
            )
            if newSH != strategyHealth { strategyHealth = newSH; strategyState.strategyHealth = newSH }
        }
        
        // System monitor
        do {
            let sysData = bridge.getSystemMonitor()
            let newSys = SystemMonitorModel(
                cpuUsagePct: sysData.cpuUsagePct, memoryUsedMb: sysData.memoryUsedMb,
                memoryPeakBytes: sysData.memoryPeakBytes, cpuCores: Int(sysData.cpuCores),
                wsLatencyP50Us: sysData.wsLatencyP50Us, wsLatencyP99Us: sysData.wsLatencyP99Us,
                featLatencyP50Us: sysData.featLatencyP50Us, featLatencyP99Us: sysData.featLatencyP99Us,
                modelLatencyP50Us: sysData.modelLatencyP50Us, modelLatencyP99Us: sysData.modelLatencyP99Us,
                e2eLatencyP50Us: sysData.e2eLatencyP50Us, e2eLatencyP99Us: sysData.e2eLatencyP99Us,
                exchangeLatencyMs: sysData.exchangeLatencyMs,
                ticksPerSec: sysData.ticksPerSec, signalsPerSec: sysData.signalsPerSec,
                ordersPerSec: sysData.ordersPerSec, uptimeHours: sysData.uptimeHours,
                gpuAvailable: sysData.gpuAvailable, gpuUsagePct: sysData.gpuUsagePct,
                gpuName: sysData.gpuName.isEmpty ? "N/A" : sysData.gpuName,
                inferenceBackend: sysData.inferenceBackend.isEmpty ? "CPU" : sysData.inferenceBackend
            )
            if newSys != systemMonitor { systemMonitor = newSys; systemState.systemMonitor = newSys }
        }
        
        // RL state
        do {
            let rlData = bridge.getRLState()
            let newRL = RLStateModel(
                signalThresholdDelta: rlData.signalThresholdDelta,
                positionSizeScale: rlData.positionSizeScale,
                orderOffsetBps: rlData.orderOffsetBps,
                requoteFreqScale: rlData.requoteFreqScale,
                avgReward: rlData.avgReward, valueEstimate: rlData.valueEstimate,
                policyLoss: rlData.policyLoss, valueLoss: rlData.valueLoss,
                totalSteps: Int(rlData.totalSteps), totalUpdates: Int(rlData.totalUpdates),
                exploring: rlData.exploring
            )
            if newRL != rlState { rlState = newRL; strategyState.rlState = newRL }
        }
        
        // Feature importance
        do {
            let fiData = bridge.getFeatureImportance()
            let newFI = FeatureImportanceModel(
                permutationImportance: fiData.permutationImportance.map { $0.doubleValue },
                mutualInformation: fiData.mutualInformation.map { $0.doubleValue },
                shapValue: fiData.shapValue.map { $0.doubleValue },
                correlation: fiData.correlation.map { $0.doubleValue },
                ranking: fiData.ranking.map { $0.intValue },
                activeFeatures: Int(fiData.activeFeatures)
            )
            if newFI != featureImportance { featureImportance = newFI; aiState.featureImportance = newFI }
        }
    }
    
    private func pollV24State() {
        guard let bridge = bridge else { return }
        
        // Chaos state
        do {
            let csData = bridge.getChaosState()
            let newCS = ChaosStateModel(
                enabled: csData.enabled,
                latencySpikes: csData.latencySpikes,
                packetsDropped: csData.packetsDropped,
                fakeDeltasInjected: csData.fakeDeltasInjected,
                oomSimulations: csData.oomSimulations,
                corruptedJsons: csData.corruptedJsons,
                clockSkews: csData.clockSkews,
                totalInjectedLatencyNs: csData.totalInjectedLatencyNs,
                maxInjectedLatencyNs: csData.maxInjectedLatencyNs,
                totalInjections: csData.totalInjections
            )
            if newCS != chaosState { chaosState = newCS; systemState.chaosState = newCS }
        }
        
        // Replay state
        do {
            let rsData = bridge.getReplayState()
            let newRS = ReplayStateModel(
                loaded: rsData.loaded, playing: rsData.playing,
                eventCount: rsData.eventCount, eventsReplayed: rsData.eventsReplayed,
                eventsFiltered: rsData.eventsFiltered, replaySpeed: rsData.replaySpeed,
                checksumValid: rsData.checksumValid, sequenceMonotonic: rsData.sequenceMonotonic,
                replayDurationNs: rsData.replayDurationNs, loadedFile: rsData.loadedFile
            )
            if newRS != replayState { replayState = newRS; systemState.replayState = newRS }
        }
        
        // VaR state
        do {
            let vData = bridge.getVaRState()
            let newV = VaRStateModel(
                var95: vData.var95, var99: vData.var99,
                cvar95: vData.cvar95, cvar99: vData.cvar99,
                parametricVar: vData.parametricVar, historicalVar: vData.historicalVar,
                monteCarloVar: vData.monteCarloVar,
                stressScenarioLosses: vData.stressScenarioLosses.map { $0.doubleValue },
                monteCarloSamples: Int(vData.monteCarloSamples),
                portfolioValue: vData.portfolioValue
            )
            if newV != varState { varState = newV; strategyState.varState = newV }
        }
        
        // OSM summary
        do {
            let osmData = bridge.getOSMSummary()
            let newOSM = OSMSummaryModel(
                activeOrders: Int(osmData.activeOrders),
                totalTransitions: Int(osmData.totalTransitions),
                avgFillTimeUs: osmData.avgFillTimeUs,
                avgSlippage: osmData.avgSlippage,
                icebergActive: osmData.icebergActive,
                icebergSlicesDone: Int(osmData.icebergSlicesDone),
                icebergSlicesTotal: Int(osmData.icebergSlicesTotal),
                icebergFilledQty: osmData.icebergFilledQty,
                icebergTotalQty: osmData.icebergTotalQty,
                twapActive: osmData.twapActive,
                twapSlicesDone: Int(osmData.twapSlicesDone),
                twapSlicesTotal: Int(osmData.twapSlicesTotal),
                marketImpactBps: osmData.marketImpactBps,
                orders: osmData.orders.map { o in
                    ManagedOrderModel(
                        id: o.orderId, state: Int(o.state), isBuy: o.isBuy,
                        price: o.price, qty: o.qty, filledQty: o.filledQty,
                        avgFillPrice: o.avgFillPrice, fillProbability: o.fillProbability,
                        createdNs: o.createdNs, lastUpdateNs: o.lastUpdateNs,
                        cancelAttempts: Int(o.cancelAttempts)
                    )
                }
            )
            if newOSM != osmSummary { osmSummary = newOSM; executionState.osmSummary = newOSM }
        }
        
        // RL v2 extended
        do {
            let r2Data = bridge.getRLv2State()
            let newR2 = RLv2StateModel(
                entropyAlpha: r2Data.entropyAlpha, klDivergence: r2Data.klDivergence,
                clipFraction: r2Data.clipFraction, approxKl: r2Data.approxKl,
                explainedVariance: r2Data.explainedVariance,
                rollbackCount: Int(r2Data.rollbackCount),
                epochsCompleted: Int(r2Data.epochsCompleted),
                bufferSize: Int(r2Data.bufferSize),
                bufferCapacity: Int(r2Data.bufferCapacity),
                trainingActive: r2Data.trainingActive,
                learningRate: r2Data.learningRate,
                stateVector: r2Data.stateVector.map { $0.doubleValue },
                actionVector: r2Data.actionVector.map { $0.doubleValue },
                rewardHistory: r2Data.rewardHistory.map { $0.doubleValue }
            )
            if newR2 != rlv2State { rlv2State = newR2; strategyState.rlv2State = newR2 }
        }
        
        // Stage histograms
        do {
            let histData = bridge.getStageHistograms()
            let newHist = histData.map { h in
                StageHistogramModel(
                    id: h.stageName, stageName: h.stageName,
                    count: h.count, meanUs: h.meanUs,
                    p50Us: h.p50Us, p90Us: h.p90Us,
                    p95Us: h.p95Us, p99Us: h.p99Us,
                    p999Us: h.p999Us, maxUs: h.maxUs,
                    stddevUs: h.stddevUs
                )
            }
            if newHist != stageHistograms { stageHistograms = newHist; systemState.stageHistograms = newHist }
        }
        
        // Sprint 3: Execution Intelligence (throttled to every 5th poll = ~500ms)
        execAnalyticsPollCount += 1
        if execAnalyticsPollCount >= 5 {
            execAnalyticsPollCount = 0
            let newAnalytics = ExecutionAnalyticsComputer.compute(
                osm: osmSummary,
                metrics: metrics,
                system: systemMonitor,
                stages: stageHistograms,
                strategy: strategyMetrics,
                health: strategyHealth,
                previousEvents: executionAnalytics.recentEvents,
                previousInsights: executionAnalytics.insights
            )
            
            // Generate incidents for severe degradation (edge-triggered)
            let incidents = ExecutionAnalyticsComputer.generateIncidents(
                analytics: newAnalytics,
                previousAnalytics: previousAnalytics
            )
            for inc in incidents {
                IncidentStore.shared.record(
                    severity: inc.severity,
                    category: inc.category,
                    titleKey: inc.titleKey,
                    detail: inc.detail
                )
            }
            
            previousAnalytics = executionAnalytics
            if newAnalytics != executionAnalytics { executionAnalytics = newAnalytics }
            
            // Sprint 4: Trade Journal — detect trade completions
            TradeJournalStore.shared.detectTradeCompletion(
                position: position,
                metrics: metrics,
                regime: regime,
                circuitBreaker: circuitBreaker,
                varState: varState,
                prediction: prediction,
                osmOrders: osmSummary.orders,
                executionScore: executionAnalytics.score.overall,
                symbol: config.symbol
            )
        }
    }
    
    // MARK: - Config
    
    private func applyConfig() {
        bridge?.setSignalThreshold(config.signalThreshold)
        bridge?.setOrderQty(config.orderQty)
        bridge?.setMaxPosition(config.maxPositionSize)
    }
    
    func togglePaperMode() {
        paperMode.toggle()
        bridge?.setPaperMode(paperMode)
        let newMode: TradingMode = paperMode ? .paper : .live
        let oldMode = tradingMode
        tradingMode = newMode
        addLog(.warn, "Mode: \(oldMode.rawValue.uppercased()) → \(newMode.rawValue.uppercased())")
        IncidentStore.shared.record(
            severity: newMode == .live ? .critical : .info,
            category: .modeChange,
            titleKey: newMode == .live ? "incident.switchedToLive" : "incident.switchedToPaper",
            detail: "\(oldMode.rawValue) → \(newMode.rawValue)"
        )
    }
    
    func setTradingMode(_ mode: TradingMode) {
        let oldMode = tradingMode
        tradingMode = mode
        paperMode = mode.isSimulated
        bridge?.setPaperMode(paperMode)
        addLog(.warn, "Mode: \(oldMode.rawValue.uppercased()) → \(mode.rawValue.uppercased())")
        IncidentStore.shared.record(
            severity: mode == .live ? .critical : .info,
            category: .modeChange,
            titleKey: "incident.modeChanged",
            detail: "\(oldMode.rawValue) → \(mode.rawValue)"
        )
    }
    
    // MARK: - Kill Switches
    
    func activateGlobalKill(reason: String) {
        logger.error("GLOBAL KILL SWITCH activated: \(reason)")
        killSwitch.activateGlobal(reason: reason)
        addLog(.error, "GLOBAL KILL: \(reason)")
        IncidentStore.shared.record(
            severity: .critical,
            category: .killSwitch,
            titleKey: "incident.globalKillActivated",
            detail: reason
        )
        emergencyStop()
    }
    
    func resetGlobalKill() {
        logger.info("Global kill switch reset")
        killSwitch.resetGlobal()
        addLog(.info, "Global kill switch reset")
        IncidentStore.shared.record(
            severity: .info,
            category: .killSwitch,
            titleKey: "incident.globalKillReset"
        )
    }
    
    func activateStrategyKill(reason: String) {
        logger.error("STRATEGY KILL activated: \(reason)")
        killSwitch.activateStrategy(reason: reason)
        addLog(.error, "STRATEGY HALT: \(reason)")
        IncidentStore.shared.record(
            severity: .critical,
            category: .riskHalt,
            titleKey: "incident.strategyHalted",
            detail: reason
        )
    }
    
    func resetStrategyKill() {
        killSwitch.resetStrategy()
        addLog(.info, "Strategy halt reset")
        IncidentStore.shared.record(
            severity: .info,
            category: .riskHalt,
            titleKey: "incident.strategyHaltReset"
        )
    }
    
    func validateConfig() -> ConfigValidationResult {
        ConfigValidator.validate(config, mode: tradingMode, hasCredentials: KeychainManager.shared.hasCredentials)
    }
    
    var canStart: Bool {
        !killSwitch.globalHalt && (status == .idle || status == .error)
    }
    
    // MARK: - Logging
    
    func addLog(_ level: LogEntry.LogLevel, _ message: String) {
        let entry = LogEntry(id: UUID(), timestamp: Date(), level: level, message: message)
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.logs.append(entry)
            if self.logs.count > self.maxLogs {
                self.logs.removeFirst(self.logs.count - self.maxLogs)
            }
            self.objectWillChange.send()
        }
    }
    
    func addSignal(_ isBuy: Bool, price: Double, qty: Double, confidence: Double) {
        let entry = SignalEntry(id: UUID(), timestamp: Date(), isBuy: isBuy, price: price, qty: qty, confidence: confidence)
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.signals.insert(entry, at: 0)
            if self.signals.count > self.maxSignals {
                self.signals.removeLast(self.signals.count - self.maxSignals)
            }
            self.objectWillChange.send()
        }
    }
    
    func addTrade(price: Double, qty: Double, isBuyerMaker: Bool) {
        let entry = TradeEntry(id: UUID(), timestamp: Date(), price: price, qty: qty, isBuyerMaker: isBuyerMaker)
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.trades.insert(entry, at: 0)
            if self.trades.count > self.maxTrades {
                self.trades.removeLast(self.trades.count - self.maxTrades)
            }
            self.objectWillChange.send()
        }
    }
    
    func triggerPanic(_ message: String) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.panicMessage = message
            self.showPanicAlert = true
            self.addLog(.error, "PANIC: \(message)")
        }
    }
    
    func clearLogs() { logs.removeAll(); objectWillChange.send() }
    
    func exportLogs() -> String {
        logs.map { "[\($0.timestamp)] [\($0.level.rawValue)] \($0.message)" }.joined(separator: "\n")
    }
    
    func exportTradesCSV() -> String {
        var csv = "timestamp,price,qty,side\n"
        let fmt = ISO8601DateFormatter()
        for trade in trades.reversed() {
            csv += "\(fmt.string(from: trade.timestamp)),\(trade.price),\(trade.qty),\(trade.side)\n"
        }
        return csv
    }
    
    func exportSignalsCSV() -> String {
        var csv = "timestamp,side,price,qty,confidence\n"
        let fmt = ISO8601DateFormatter()
        for sig in signals.reversed() {
            csv += "\(fmt.string(from: sig.timestamp)),\(sig.isBuy ? "BUY" : "SELL"),\(sig.price),\(sig.qty),\(sig.confidence)\n"
        }
        return csv
    }
    
    // MARK: - TCEngineDelegate
    
    func engineDidChangeStatus(_ status: TCEngineStatusObjC) {
        let newStatus = EngineStatus(rawValue: status.rawValue) ?? .idle
        logger.info("Engine status changed: \(newStatus.rawValue)")
        self.status = newStatus
        addLog(.info, "Status: \(newStatus.locKey)")
        
        if newStatus == .error {
            addLog(.error, "ENGINE CRASHED — engine entered ERROR state")
            IncidentStore.shared.record(
                severity: .critical,
                category: .system,
                titleKey: "incident.engineCrash",
                detail: "Engine entered ERROR state"
            )
            stopPolling()
            bridge?.stop()
            bridge = nil
            triggerPanic("ENGINE CRASHED — press Restart to recover")
        }
    }
    
    func engineDidReceiveLog(_ message: String, level: TCLogLevelObjC) {
        let swiftLevel: LogEntry.LogLevel
        switch level {
        case .debug: swiftLevel = .debug
        case .info:  swiftLevel = .info
        case .warn:  swiftLevel = .warn
        case .error: swiftLevel = .error
        @unknown default: swiftLevel = .info
        }
        addLog(swiftLevel, message)
    }
    
    func engineDidReceiveSignal(_ signal: TCSignalData) {
        addSignal(signal.isBuy, price: signal.price, qty: signal.qty, confidence: signal.confidence)
    }
}
