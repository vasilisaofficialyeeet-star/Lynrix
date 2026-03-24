// MarketRegimeIntelligence.swift — Market Regime Intelligence (Lynrix v2.5 Sprint 5)
// Regime transition tracking, stability metrics, distribution, and strategy correlation.
// All fields marked as measured (from C++ engine) or derived (computed in Swift).

import Foundation
import SwiftUI

// MARK: - Regime Transition

struct RegimeTransition: Identifiable, Equatable {
    let id: UUID
    let timestamp: Date
    let fromRegime: Int
    let toRegime: Int
    let confidenceAtTransition: Double
    let volatilityAtTransition: Double
}

// MARK: - Regime Distribution Entry

struct RegimeDistributionEntry: Identifiable, Equatable {
    let id: String            // regime index as string
    let regime: Int
    var tickCount: Int = 0
    var percentage: Double = 0
    var avgConfidence: Double = 0
    var avgVolatility: Double = 0
}

// MARK: - Regime Stability

struct RegimeStability: Equatable {
    var transitionsLastHour: Int = 0         // derived: count from transition history
    var avgDurationSec: Double = 0           // derived: avg time between transitions
    var currentDurationSec: Double = 0       // derived: time since last transition
    var isStable: Bool = true                // derived: transitions < threshold
    var stabilityScore: Double = 100         // derived: 0-100 (fewer transitions = higher)
}

// MARK: - Regime Performance Correlation

struct RegimePerformanceEntry: Equatable {
    var regime: Int = 0
    var tradeCount: Int = 0
    var winRate: Double = 0
    var avgPnl: Double = 0
    var totalPnl: Double = 0
    var avgExecutionScore: Double = 0
    var avgSlippage: Double = 0
}

// MARK: - Market Regime Intelligence Snapshot

struct MarketRegimeIntelligenceSnapshot: Equatable {
    // Current state (measured from engine)
    var currentRegime: Int = 0
    var previousRegime: Int = 0
    var confidence: Double = 0
    var volatility: Double = 0
    var trendScore: Double = 0
    var mrScore: Double = 0
    var liqScore: Double = 0

    // Derived analytics
    var stability: RegimeStability = .init()
    var distribution: [RegimeDistributionEntry] = []
    var recentTransitions: [RegimeTransition] = []
    var performanceByRegime: [RegimePerformanceEntry] = []
    var dominantRegime: Int = 0
    var regimeChanged: Bool = false
}

// MARK: - Market Regime Intelligence Store

final class MarketRegimeIntelligenceStore: ObservableObject {
    static let shared = MarketRegimeIntelligenceStore()

    @Published var snapshot: MarketRegimeIntelligenceSnapshot = .init()

    private var transitions: [RegimeTransition] = []
    private var lastRegime: Int = -1
    private var lastTransitionTime: Date = Date()
    private var regimeTickCounts: [Int: Int] = [:]
    private var regimeConfidenceSums: [Int: Double] = [:]
    private var regimeVolatilitySums: [Int: Double] = [:]
    private var totalTicks: Int = 0
    private let maxTransitions = 200

    // MARK: - Update (called from engine poll)

    func update(regime: RegimeSnapshot) {
        totalTicks += 1
        regimeTickCounts[regime.current, default: 0] += 1
        regimeConfidenceSums[regime.current, default: 0] += regime.confidence
        regimeVolatilitySums[regime.current, default: 0] += regime.volatility

        let regimeChanged = lastRegime >= 0 && regime.current != lastRegime
        if regimeChanged {
            let transition = RegimeTransition(
                id: UUID(),
                timestamp: Date(),
                fromRegime: lastRegime,
                toRegime: regime.current,
                confidenceAtTransition: regime.confidence,
                volatilityAtTransition: regime.volatility
            )
            transitions.append(transition)
            if transitions.count > maxTransitions {
                transitions.removeFirst(transitions.count - maxTransitions)
            }
            lastTransitionTime = Date()
        }
        lastRegime = regime.current

        // Rebuild snapshot
        var snap = MarketRegimeIntelligenceSnapshot()
        snap.currentRegime = regime.current
        snap.previousRegime = regime.previous
        snap.confidence = regime.confidence
        snap.volatility = regime.volatility
        snap.trendScore = regime.trendScore
        snap.mrScore = regime.mrScore
        snap.liqScore = regime.liqScore
        snap.regimeChanged = regimeChanged

        // Distribution
        var dist: [RegimeDistributionEntry] = []
        for regimeIdx in 0...4 {
            let count = regimeTickCounts[regimeIdx, default: 0]
            let pct = totalTicks > 0 ? Double(count) / Double(totalTicks) : 0
            let avgConf = count > 0 ? regimeConfidenceSums[regimeIdx, default: 0] / Double(count) : 0
            let avgVol = count > 0 ? regimeVolatilitySums[regimeIdx, default: 0] / Double(count) : 0
            dist.append(RegimeDistributionEntry(
                id: "\(regimeIdx)", regime: regimeIdx,
                tickCount: count, percentage: pct,
                avgConfidence: avgConf, avgVolatility: avgVol
            ))
        }
        snap.distribution = dist
        snap.dominantRegime = dist.max(by: { $0.tickCount < $1.tickCount })?.regime ?? 0

        // Recent transitions (last 20)
        snap.recentTransitions = Array(transitions.suffix(20).reversed())

        // Stability
        let oneHourAgo = Date().addingTimeInterval(-3600)
        let recentCount = transitions.filter { $0.timestamp > oneHourAgo }.count
        let currentDuration = Date().timeIntervalSince(lastTransitionTime)

        var stability = RegimeStability()
        stability.transitionsLastHour = recentCount
        stability.currentDurationSec = currentDuration
        stability.isStable = recentCount < 10
        stability.stabilityScore = max(0, 100 - Double(recentCount) * 5)

        if transitions.count >= 2 {
            let intervals = zip(transitions.dropFirst(), transitions).map {
                $0.timestamp.timeIntervalSince($1.timestamp)
            }
            stability.avgDurationSec = intervals.isEmpty ? 0 : intervals.reduce(0, +) / Double(intervals.count)
        }
        snap.stability = stability

        // Performance by regime (from trade journal)
        snap.performanceByRegime = computeRegimePerformance()

        DispatchQueue.main.async { [weak self] in
            self?.snapshot = snap
        }
    }

    private func computeRegimePerformance() -> [RegimePerformanceEntry] {
        let trades = TradeJournalStore.shared.trades
        guard !trades.isEmpty else { return [] }

        var byRegime: [Int: [CompletedTrade]] = [:]
        for trade in trades {
            byRegime[trade.riskAtEntry.regime, default: []].append(trade)
        }

        return byRegime.map { (regime, regTrades) in
            let wins = regTrades.filter { $0.outcome == .win }.count
            let totalPnl = regTrades.reduce(0.0) { $0 + $1.realizedPnl }
            let avgExec = regTrades.isEmpty ? 0 : regTrades.reduce(0.0) { $0 + $1.executionScore } / Double(regTrades.count)
            let avgSlip = regTrades.isEmpty ? 0 : regTrades.reduce(0.0) { $0 + $1.totalSlippageBps } / Double(regTrades.count)
            return RegimePerformanceEntry(
                regime: regime,
                tradeCount: regTrades.count,
                winRate: regTrades.isEmpty ? 0 : Double(wins) / Double(regTrades.count),
                avgPnl: regTrades.isEmpty ? 0 : totalPnl / Double(regTrades.count),
                totalPnl: totalPnl,
                avgExecutionScore: avgExec,
                avgSlippage: avgSlip
            )
        }.sorted { $0.regime < $1.regime }
    }

    func reset() {
        transitions.removeAll()
        regimeTickCounts.removeAll()
        regimeConfidenceSums.removeAll()
        regimeVolatilitySums.removeAll()
        totalTicks = 0
        lastRegime = -1
        snapshot = .init()
    }
}
