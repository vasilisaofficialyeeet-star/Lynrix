// AllocationIntelligence.swift — Allocation Intelligence (Lynrix v2.5 Sprint 5)
// Regime-aware position sizing, risk budget, signal quality, and allocation recommendations.
// All fields marked as measured (from C++ engine) or derived (computed in Swift).

import Foundation
import SwiftUI

// MARK: - Allocation Signal

enum AllocationSignal: Int, Equatable, Comparable {
    case strongReduce = 0
    case reduce       = 1
    case hold         = 2
    case increase     = 3
    case strongIncrease = 4

    var locKey: String {
        switch self {
        case .strongReduce:   return "alloc.signal.strongReduce"
        case .reduce:         return "alloc.signal.reduce"
        case .hold:           return "alloc.signal.hold"
        case .increase:       return "alloc.signal.increase"
        case .strongIncrease: return "alloc.signal.strongIncrease"
        }
    }

    var icon: String {
        switch self {
        case .strongReduce:   return "arrow.down.to.line"
        case .reduce:         return "arrow.down"
        case .hold:           return "equal"
        case .increase:       return "arrow.up"
        case .strongIncrease: return "arrow.up.to.line"
        }
    }

    var color: Color {
        switch self {
        case .strongReduce:   return LxColor.bloodRed
        case .reduce:         return LxColor.magentaPink
        case .hold:           return LxColor.coolSteel
        case .increase:       return LxColor.neonLime
        case .strongIncrease: return LxColor.electricCyan
        }
    }

    static func < (lhs: AllocationSignal, rhs: AllocationSignal) -> Bool {
        lhs.rawValue < rhs.rawValue
    }
}

// MARK: - Risk Budget Utilization

struct RiskBudgetUtilization: Equatable {
    var drawdownUsedPct: Double = 0       // measured: currentDrawdown / maxDrawdown
    var dailyLossUsedPct: Double = 0      // derived: dailyPnl / maxDailyLoss (when negative)
    var varUsedPct: Double = 0            // derived: var99 / portfolioValue
    var positionUsedPct: Double = 0       // measured: position.qty / maxPositionSize
    var leverageUsedPct: Double = 0       // derived: position leverage / maxLeverage
    var overallUtilization: Double = 0    // derived: weighted max of components
    var budgetRemaining: Double = 1.0     // derived: 1 - overallUtilization
}

// MARK: - Signal Quality Assessment

struct SignalQualityAssessment: Equatable {
    var recentAccuracy: Double = 0        // measured: from threshold.recentAccuracy
    var modelConfidence: Double = 0       // measured: from prediction.modelConfidence
    var regimeConfidence: Double = 0      // measured: from regime.confidence
    var executionScore: Double = 0        // measured: from executionAnalytics.score.overall
    var signalEfficiency: Double = 0      // derived: composite quality score
    var qualityGrade: String = "—"        // derived: A/B/C/D/F
}

// MARK: - Regime Allocation Context

struct RegimeAllocationContext: Equatable {
    var regime: Int = 0
    var regimeFavorable: Bool = true       // derived: regime compatible with strategy
    var suggestedSizeScale: Double = 1.0   // derived: regime-based position size multiplier
    var suggestedThresholdAdj: Double = 0  // derived: regime-based threshold adjustment
    var rationale: String = ""             // derived: explanation key
}

// MARK: - Allocation Recommendation

struct AllocationRecommendation: Equatable {
    var signal: AllocationSignal = .hold
    var confidenceInSignal: Double = 0.5
    var suggestedPositionScale: Double = 1.0
    var reasons: [String] = []            // localization keys
    var riskBudget: RiskBudgetUtilization = .init()
    var signalQuality: SignalQualityAssessment = .init()
    var regimeContext: RegimeAllocationContext = .init()
    var compositeScore: Double = 50       // 0-100: overall allocation attractiveness
}

// MARK: - Allocation Intelligence Computer

enum AllocationIntelligenceComputer {

    static func compute(
        regime: RegimeSnapshot,
        prediction: PredictionSnapshot,
        threshold: ThresholdSnapshot,
        strategyMetrics: StrategyMetricsModel,
        strategyHealth: StrategyHealthModel,
        circuitBreaker: CircuitBreakerSnapshot,
        position: PositionSnapshot,
        varState: VaRStateModel,
        executionAnalytics: ExecutionAnalytics,
        config: LynrixEngine.TradingConfig
    ) -> AllocationRecommendation {

        var rec = AllocationRecommendation()

        // Risk Budget
        var budget = RiskBudgetUtilization()
        budget.drawdownUsedPct = config.maxDrawdown > 0 ? strategyMetrics.currentDrawdown / config.maxDrawdown : 0
        budget.dailyLossUsedPct = strategyMetrics.dailyPnl < 0 && config.maxDailyLoss > 0
            ? abs(strategyMetrics.dailyPnl) / config.maxDailyLoss : 0
        budget.varUsedPct = varState.portfolioValue > 0 ? abs(varState.var99) / varState.portfolioValue : 0
        budget.positionUsedPct = config.maxPositionSize > 0 ? abs(position.size) / config.maxPositionSize : 0
        budget.leverageUsedPct = 0
        budget.overallUtilization = max(budget.drawdownUsedPct, budget.dailyLossUsedPct,
                                        budget.positionUsedPct, budget.leverageUsedPct) * 0.7 +
                                    budget.varUsedPct * 0.3
        budget.budgetRemaining = max(0, 1.0 - budget.overallUtilization)
        rec.riskBudget = budget

        // Signal Quality
        var sq = SignalQualityAssessment()
        sq.recentAccuracy = threshold.recentAccuracy
        sq.modelConfidence = prediction.modelConfidence
        sq.regimeConfidence = regime.confidence
        sq.executionScore = executionAnalytics.score.overall / 100.0
        sq.signalEfficiency = sq.recentAccuracy * 0.35 + sq.modelConfidence * 0.25 +
                              sq.regimeConfidence * 0.20 + sq.executionScore * 0.20
        sq.qualityGrade = gradeFromScore(sq.signalEfficiency)
        rec.signalQuality = sq

        // Regime Context
        var rc = RegimeAllocationContext()
        rc.regime = regime.current
        switch regime.current {
        case 0: // Trending
            rc.regimeFavorable = true
            rc.suggestedSizeScale = 1.2
            rc.suggestedThresholdAdj = -0.02
            rc.rationale = "alloc.regime.trending"
        case 1: // Mean Reverting
            rc.regimeFavorable = true
            rc.suggestedSizeScale = 1.0
            rc.suggestedThresholdAdj = 0
            rc.rationale = "alloc.regime.meanReverting"
        case 2: // Volatile
            rc.regimeFavorable = false
            rc.suggestedSizeScale = 0.6
            rc.suggestedThresholdAdj = 0.05
            rc.rationale = "alloc.regime.volatile"
        case 3: // Quiet
            rc.regimeFavorable = false
            rc.suggestedSizeScale = 0.5
            rc.suggestedThresholdAdj = 0.08
            rc.rationale = "alloc.regime.quiet"
        default: // Choppy / Unknown
            rc.regimeFavorable = false
            rc.suggestedSizeScale = 0.4
            rc.suggestedThresholdAdj = 0.1
            rc.rationale = "alloc.regime.choppy"
        }
        rec.regimeContext = rc

        // Composite score
        let healthFactor = strategyHealth.healthScore
        let budgetFactor = budget.budgetRemaining
        let qualityFactor = sq.signalEfficiency
        let regimeFactor = rc.regimeFavorable ? 1.0 : 0.5

        rec.compositeScore = (healthFactor * 30 + budgetFactor * 100 * 25 +
                              qualityFactor * 100 * 25 + regimeFactor * 100 * 20) / 100.0

        // Circuit breaker override
        if circuitBreaker.tripped || circuitBreaker.inCooldown {
            rec.signal = .strongReduce
            rec.confidenceInSignal = 0.95
            rec.suggestedPositionScale = 0
            rec.reasons.append("alloc.reason.circuitBreaker")
            rec.compositeScore = 0
            return rec
        }

        // Determine signal
        var reasons: [String] = []

        if budget.overallUtilization > 0.9 {
            rec.signal = .strongReduce
            reasons.append("alloc.reason.riskBudgetExhausted")
        } else if budget.overallUtilization > 0.7 {
            rec.signal = .reduce
            reasons.append("alloc.reason.riskBudgetHigh")
        } else if healthFactor < 0.3 {
            rec.signal = .reduce
            reasons.append("alloc.reason.healthPoor")
        } else if !rc.regimeFavorable && sq.signalEfficiency < 0.4 {
            rec.signal = .reduce
            reasons.append("alloc.reason.regimeUnfavorable")
            reasons.append("alloc.reason.signalQualityLow")
        } else if rc.regimeFavorable && sq.signalEfficiency > 0.6 && budget.budgetRemaining > 0.5 {
            rec.signal = .strongIncrease
            reasons.append("alloc.reason.regimeFavorable")
            reasons.append("alloc.reason.signalQualityHigh")
            reasons.append("alloc.reason.budgetAvailable")
        } else if sq.signalEfficiency > 0.5 && budget.budgetRemaining > 0.3 {
            rec.signal = .increase
            reasons.append("alloc.reason.signalQualityGood")
        } else {
            rec.signal = .hold
            reasons.append("alloc.reason.neutral")
        }

        rec.reasons = reasons
        rec.suggestedPositionScale = rc.suggestedSizeScale * min(1.0, budget.budgetRemaining * 1.5)
        rec.confidenceInSignal = min(1.0, sq.signalEfficiency * 0.6 + regime.confidence * 0.4)

        return rec
    }

    private static func gradeFromScore(_ score: Double) -> String {
        if score >= 0.8 { return "A" }
        if score >= 0.65 { return "B" }
        if score >= 0.5 { return "C" }
        if score >= 0.35 { return "D" }
        return "F"
    }
}
