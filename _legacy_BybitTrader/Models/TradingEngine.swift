// TradingEngine.swift — Swift wrapper around Obj-C++ bridge
// Manages engine lifecycle and polls data on a timer for UI updates

import Foundation
import Combine
import os.log

private let logger = Logger(subsystem: "com.bybittrader.app", category: "TradingEngine")

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
    
    var regimeName: String {
        switch current {
        case 0: return "Низкая вол."
        case 1: return "Высокая вол."
        case 2: return "Тренд"
        case 3: return "Возврат к ср."
        case 4: return "Вакуум ликв."
        default: return "Неизвестно"
        }
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
    // Multi-horizon
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
    var healthLevel: Int = 1  // 0=Excellent..4=Halted
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
    
    var levelName: String {
        switch healthLevel {
        case 0: return "Отлично"
        case 1: return "Хорошо"
        case 2: return "Внимание"
        case 3: return "Критично"
        case 4: return "Остановлено"
        default: return "Неизвестно"
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
    
    var label: String {
        switch self {
        case .idle: return "ОЖИДАНИЕ"
        case .connecting: return "ПОДКЛЮЧЕНИЕ"
        case .connected: return "ПОДКЛЮЧЕНО"
        case .trading: return "ТОРГОВЛЯ"
        case .error: return "ОШИБКА"
        case .stopping: return "ОСТАНОВКА"
        }
    }
    
    var isActive: Bool {
        self == .trading || self == .connecting || self == .connected
    }
}

// MARK: - TradingEngine

final class TradingEngine: NSObject, ObservableObject, TCEngineDelegate {
    
    // Published state for SwiftUI
    @Published var status: EngineStatus = .idle
    @Published var orderBook: OrderBookSnapshot = .init()
    @Published var position: PositionSnapshot = .init()
    @Published var metrics: MetricsSnapshot = .init()
    @Published var logs: [LogEntry] = []
    @Published var signals: [SignalEntry] = []
    @Published var trades: [TradeEntry] = []
    @Published var paperMode: Bool = true
    @Published var showPanicAlert: Bool = false
    @Published var panicMessage: String = ""
    @Published var isReconnecting: Bool = false
    
    // AI Edition state
    @Published var regime: RegimeSnapshot = .init()
    @Published var prediction: PredictionSnapshot = .init()
    @Published var threshold: ThresholdSnapshot = .init()
    @Published var circuitBreaker: CircuitBreakerSnapshot = .init()
    @Published var pnlHistory: [Double] = []
    @Published var drawdownHistory: [Double] = []
    @Published var accuracyHistory: [Double] = []
    @Published var accuracy: AccuracySnapshot = .init()
    
    // Strategy & System state
    @Published var strategyMetrics: StrategyMetricsModel = .init()
    @Published var strategyHealth: StrategyHealthModel = .init()
    @Published var systemMonitor: SystemMonitorModel = .init()
    @Published var rlState: RLStateModel = .init()
    @Published var featureImportance: FeatureImportanceModel = .init()
    
    private var bridge: TradingCoreBridge?
    private var pollTimer: DispatchSourceTimer?
    private var lastReconnectCount: UInt64 = 0
    private let maxLogs = 500
    private let maxSignals = 200
    private let maxTrades = 500
    private let pollInterval: TimeInterval = 0.1 // 100ms = 10 FPS (GUI-safe)
    private var lastStartConfig: (key: String?, secret: String?)?
    
    // Configuration
    var config: TradingConfig {
        didSet { applyConfig() }
    }
    
    struct TradingConfig {
        var symbol: String = "BTCUSDT"
        var paperTrading: Bool = true
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
        logger.info("TradingEngine initialized")
        addLog(.info, "BybitTrader AI Edition v2.0 инициализирован")
        addLog(.info, "Система: \(ProcessInfo.processInfo.operatingSystemVersionString)")
        addLog(.info, "Память: \(ProcessInfo.processInfo.physicalMemory / 1_073_741_824) ГБ")
        addLog(.info, "Ядра CPU: \(ProcessInfo.processInfo.processorCount)")
    }
    
    deinit {
        logger.info("TradingEngine deinit")
        stop()
    }
    
    // MARK: - Lifecycle
    
    func start(apiKey: String? = nil, apiSecret: String? = nil) {
        guard status == .idle || status == .error else {
            logger.warning("start() called in invalid state: \(self.status.label)")
            return
        }
        
        logger.info("Starting engine: symbol=\(self.config.symbol) paper=\(self.config.paperTrading)")
        addLog(.info, "Запуск движка...")
        addLog(.info, "Символ: \(config.symbol) | Бумажная: \(config.paperTrading) | Объём: \(config.orderQty)")
        
        let objcConfig = TCConfigObjC()
        objcConfig.symbol = config.symbol
        objcConfig.paperTrading = config.paperTrading
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
        
        bridge = TradingCoreBridge(config: objcConfig)
        guard bridge != nil else {
            logger.error("Bridge creation returned nil")
            addLog(.error, "Ошибка создания моста — C++ движок не инициализирован")
            status = .error
            return
        }
        bridge?.delegate = self
        logger.info("Bridge created successfully")
        
        if let key = apiKey, !key.isEmpty,
           let secret = apiSecret, !secret.isEmpty {
            bridge?.setAPIKey(key, secret: secret)
            addLog(.info, "API ключи установлены (длина ключа: \(key.count))")
        } else {
            addLog(.warn, "API ключи не указаны — приватные каналы недоступны")
        }
        
        paperMode = config.paperTrading
        lastStartConfig = (key: apiKey, secret: apiSecret)
        
        if bridge?.start() == true {
            startPolling()
            logger.info("Engine started successfully")
            addLog(.info, "Движок запущен — опрос \(Int(1.0/pollInterval)) FPS")
        } else {
            logger.error("Engine start() returned false")
            addLog(.error, "Ошибка запуска движка — проверьте логи C++")
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
        addLog(.info, "Движок остановлен")
        logger.info("Engine stopped")
    }
    
    // MARK: - Engine Control
    
    func emergencyStop() {
        logger.error("EMERGENCY STOP triggered by user")
        addLog(.error, "АВАРИЙНАЯ ОСТАНОВКА — отмена всех ордеров")
        stopPolling()
        bridge?.emergencyStop()
        bridge = nil
        status = .idle
    }
    
    func reloadModel(path: String? = nil) {
        guard let bridge = bridge else {
            addLog(.error, "Невозможно перезагрузить модель — движок не запущен")
            return
        }
        let modelPath = path ?? config.onnxModelPath
        guard !modelPath.isEmpty else {
            addLog(.error, "Путь к модели не указан")
            return
        }
        addLog(.info, "Перезагрузка модели: \(modelPath)")
        if bridge.reloadModel(modelPath) {
            addLog(.info, "Модель успешно перезагружена")
        } else {
            addLog(.error, "Ошибка перезагрузки модели")
        }
    }
    
    func restart() {
        logger.info("Restarting engine...")
        addLog(.info, "Перезапуск движка...")
        stop()
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in
            guard let self = self else { return }
            self.start(apiKey: self.lastStartConfig?.key,
                       apiSecret: self.lastStartConfig?.secret)
        }
    }
    
    // MARK: - Polling (DispatchSourceTimer — immune to UI stalls)
    
    private func startPolling() {
        let timer = DispatchSource.makeTimerSource(queue: DispatchQueue.main)
        timer.schedule(deadline: .now(), repeating: pollInterval)
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
        
        // Update orderbook at 30 FPS
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
        if newOB != orderBook { orderBook = newOB }
        
        // Update position (lower frequency OK)
        let posData = bridge.getPosition()
        let newPos = PositionSnapshot(
            size: posData.size,
            entryPrice: posData.entryPrice,
            unrealizedPnl: posData.unrealizedPnl,
            realizedPnl: posData.realizedPnl,
            fundingImpact: posData.fundingImpact,
            isLong: posData.isLong
        )
        if newPos != position { position = newPos }
        
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
            // Detect reconnects
            if newMet.wsReconnects > lastReconnectCount {
                isReconnecting = true
                addLog(.warn, "WebSocket reconnecting... (\(newMet.wsReconnects) total)")
                DispatchQueue.main.asyncAfter(deadline: .now() + 3.0) { [weak self] in
                    self?.isReconnecting = false
                }
            }
            lastReconnectCount = newMet.wsReconnects
            metrics = newMet
        }
        
        // AI Edition polling
        pollAIState()
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
        if newRegime != regime { regime = newRegime }
        
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
        if newPred != prediction { prediction = newPred }
        
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
        if newTh != threshold { threshold = newTh }
        
        // Circuit breaker
        let cbData = bridge.getCircuitBreakerState()
        let newCB = CircuitBreakerSnapshot(
            tripped: cbData.tripped,
            inCooldown: cbData.inCooldown,
            drawdownPct: cbData.drawdownPct
        )
        if newCB != circuitBreaker {
            if newCB.tripped && !circuitBreaker.tripped {
                addLog(.error, "CIRCUIT BREAKER СРАБОТАЛ")
                triggerPanic("Circuit Breaker сработал — торговля приостановлена")
            }
            circuitBreaker = newCB
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
        if newAcc != accuracy { accuracy = newAcc }

        // Track PnL history for chart
        let net = position.netPnl
        if pnlHistory.isEmpty || pnlHistory.last != net {
            pnlHistory.append(net)
            if pnlHistory.count > 500 {
                pnlHistory.removeFirst()
            }
        }

        // Track drawdown history
        let dd = circuitBreaker.drawdownPct
        if drawdownHistory.isEmpty || drawdownHistory.last != dd {
            drawdownHistory.append(dd)
            if drawdownHistory.count > 500 {
                drawdownHistory.removeFirst()
            }
        }

        // Track accuracy history
        let acc = accuracy.rollingAccuracy
        if accuracyHistory.isEmpty || accuracyHistory.last != acc {
            accuracyHistory.append(acc)
            if accuracyHistory.count > 500 {
                accuracyHistory.removeFirst()
            }
        }
        
        // Strategy metrics
        do {
            let smData = bridge.getStrategyMetrics()
            let newSM = StrategyMetricsModel(
                sharpeRatio: smData.sharpeRatio,
                sortinoRatio: smData.sortinoRatio,
                maxDrawdownPct: smData.maxDrawdownPct,
                currentDrawdown: smData.currentDrawdown,
                profitFactor: smData.profitFactor,
                winRate: smData.winRate,
                avgWin: smData.avgWin,
                avgLoss: smData.avgLoss,
                expectancy: smData.expectancy,
                totalPnl: smData.totalPnl,
                bestTrade: smData.bestTrade,
                worstTrade: smData.worstTrade,
                totalTrades: Int(smData.totalTrades),
                winningTrades: Int(smData.winningTrades),
                losingTrades: Int(smData.losingTrades),
                consecutiveWins: Int(smData.consecutiveWins),
                consecutiveLosses: Int(smData.consecutiveLosses),
                maxConsecutiveWins: Int(smData.maxConsecutiveWins),
                maxConsecutiveLosses: Int(smData.maxConsecutiveLosses),
                dailyPnl: smData.dailyPnl,
                hourlyPnl: smData.hourlyPnl,
                calmarRatio: smData.calmarRatio,
                recoveryFactor: smData.recoveryFactor
            )
            if newSM != strategyMetrics { strategyMetrics = newSM }
        }
        
        // Strategy health
        do {
            let shData = bridge.getStrategyHealth()
            let newSH = StrategyHealthModel(
                healthLevel: Int(shData.healthLevel),
                healthScore: shData.healthScore,
                activityScale: shData.activityScale,
                thresholdOffset: shData.thresholdOffset,
                accuracyScore: shData.accuracyScore,
                pnlScore: shData.pnlScore,
                drawdownScore: shData.drawdownScore,
                sharpeScore: shData.sharpeScore,
                consistencyScore: shData.consistencyScore,
                fillRateScore: shData.fillRateScore,
                accuracyDeclining: shData.accuracyDeclining,
                pnlDeclining: shData.pnlDeclining,
                drawdownWarning: shData.drawdownWarning,
                regimeChanges1h: Int(shData.regimeChanges1h)
            )
            if newSH != strategyHealth { strategyHealth = newSH }
        }
        
        // System monitor
        do {
            let sysData = bridge.getSystemMonitor()
            let newSys = SystemMonitorModel(
                cpuUsagePct: sysData.cpuUsagePct,
                memoryUsedMb: sysData.memoryUsedMb,
                memoryPeakBytes: sysData.memoryPeakBytes,
                cpuCores: Int(sysData.cpuCores),
                wsLatencyP50Us: sysData.wsLatencyP50Us,
                wsLatencyP99Us: sysData.wsLatencyP99Us,
                featLatencyP50Us: sysData.featLatencyP50Us,
                featLatencyP99Us: sysData.featLatencyP99Us,
                modelLatencyP50Us: sysData.modelLatencyP50Us,
                modelLatencyP99Us: sysData.modelLatencyP99Us,
                e2eLatencyP50Us: sysData.e2eLatencyP50Us,
                e2eLatencyP99Us: sysData.e2eLatencyP99Us,
                exchangeLatencyMs: sysData.exchangeLatencyMs,
                ticksPerSec: sysData.ticksPerSec,
                signalsPerSec: sysData.signalsPerSec,
                ordersPerSec: sysData.ordersPerSec,
                uptimeHours: sysData.uptimeHours,
                gpuAvailable: sysData.gpuAvailable,
                gpuUsagePct: sysData.gpuUsagePct,
                gpuName: sysData.gpuName.isEmpty ? "N/A" : sysData.gpuName,
                inferenceBackend: sysData.inferenceBackend.isEmpty ? "CPU" : sysData.inferenceBackend
            )
            if newSys != systemMonitor { systemMonitor = newSys }
        }
        
        // RL state
        do {
            let rlData = bridge.getRLState()
            let newRL = RLStateModel(
                signalThresholdDelta: rlData.signalThresholdDelta,
                positionSizeScale: rlData.positionSizeScale,
                orderOffsetBps: rlData.orderOffsetBps,
                requoteFreqScale: rlData.requoteFreqScale,
                avgReward: rlData.avgReward,
                valueEstimate: rlData.valueEstimate,
                policyLoss: rlData.policyLoss,
                valueLoss: rlData.valueLoss,
                totalSteps: Int(rlData.totalSteps),
                totalUpdates: Int(rlData.totalUpdates),
                exploring: rlData.exploring
            )
            if newRL != rlState { rlState = newRL }
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
            if newFI != featureImportance { featureImportance = newFI }
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
        addLog(.warn, paperMode ? "Switched to PAPER mode" : "Switched to LIVE mode")
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
    
    func clearLogs() { logs.removeAll() }
    
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
        logger.info("Engine status changed: \(newStatus.label)")
        self.status = newStatus
        addLog(.info, "Статус: \(newStatus.label)")
        
        if newStatus == .error {
            addLog(.error, "ENGINE CRASHED — движок перешёл в состояние ОШИБКА")
            stopPolling()
            bridge?.stop()
            bridge = nil
            triggerPanic("ENGINE CRASHED — нажмите Перезапуск для восстановления")
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
