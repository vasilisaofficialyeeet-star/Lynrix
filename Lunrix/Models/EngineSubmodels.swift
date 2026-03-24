// EngineSubmodels.swift — Focused ObservableObject submodels for LynrixEngine
// M6 fix: Instead of 40+ @Published on one object (triggering all subscribers),
// views can @ObservedObject the specific submodel they need.
// LynrixEngine.swift retains backward-compat @Published for gradual migration.

import Foundation
import Combine

// MARK: - Market Data (OrderBook + Trades)

final class MarketDataState: ObservableObject {
    @Published var orderBook: OrderBookSnapshot = .init()
    @Published var trades: [TradeEntry] = []
}

// MARK: - Position & PnL

final class PositionState: ObservableObject {
    @Published var position: PositionSnapshot = .init()
    @Published var pnlHistory: [Double] = []
    @Published var drawdownHistory: [Double] = []
}

// MARK: - AI / ML State

final class AIState: ObservableObject {
    @Published var regime: RegimeSnapshot = .init()
    @Published var prediction: PredictionSnapshot = .init()
    @Published var threshold: ThresholdSnapshot = .init()
    @Published var accuracy: AccuracySnapshot = .init()
    @Published var accuracyHistory: [Double] = []
    @Published var circuitBreaker: CircuitBreakerSnapshot = .init()
    @Published var featureImportance: FeatureImportanceModel = .init()
}

// MARK: - Execution State

final class ExecutionSubstate: ObservableObject {
    @Published var osmSummary: OSMSummaryModel = .init()
    @Published var executionAnalytics: ExecutionAnalytics = .init()
    @Published var metrics: MetricsSnapshot = .init()
    @Published var signals: [SignalEntry] = []
}

// MARK: - Strategy State

final class StrategySubstate: ObservableObject {
    @Published var strategyMetrics: StrategyMetricsModel = .init()
    @Published var strategyHealth: StrategyHealthModel = .init()
    @Published var rlState: RLStateModel = .init()
    @Published var rlv2State: RLv2StateModel = .init()
    @Published var varState: VaRStateModel = .init()
}

// MARK: - System & Diagnostics

final class SystemSubstate: ObservableObject {
    @Published var systemMonitor: SystemMonitorModel = .init()
    @Published var stageHistograms: [StageHistogramModel] = []
    @Published var chaosState: ChaosStateModel = .init()
    @Published var replayState: ReplayStateModel = .init()
}
