// TradeJournal.swift — Trade journal model + post-trade analysis (Lynrix v2.5 Sprint 4)
// Structured completed-trade records with entry/exit, PnL, execution quality,
// risk state, incidents, and replay linkage for the Trade Review Studio.

import Foundation
import SwiftUI

// MARK: - Trade Side

enum TradeSide: String, Codable, Equatable {
    case long  = "long"
    case short = "short"

    var locKey: String {
        switch self {
        case .long:  return "common.long"
        case .short: return "common.short"
        }
    }

    var color: Color {
        switch self {
        case .long:  return LxColor.neonLime
        case .short: return LxColor.magentaPink
        }
    }

    var icon: String {
        switch self {
        case .long:  return "arrow.up.right"
        case .short: return "arrow.down.right"
        }
    }
}

// MARK: - Trade Outcome

enum TradeOutcome: String, Codable, Equatable {
    case win     = "win"
    case loss    = "loss"
    case breakEven = "breakeven"

    var locKey: String {
        switch self {
        case .win:       return "trade.outcome.win"
        case .loss:      return "trade.outcome.loss"
        case .breakEven: return "trade.outcome.breakeven"
        }
    }

    var color: Color {
        switch self {
        case .win:       return LxColor.neonLime
        case .loss:      return LxColor.magentaPink
        case .breakEven: return LxColor.coolSteel
        }
    }

    var icon: String {
        switch self {
        case .win:       return "checkmark.circle.fill"
        case .loss:      return "xmark.circle.fill"
        case .breakEven: return "minus.circle.fill"
        }
    }
}

// MARK: - Trade Rating

enum TradeRating: Int, Codable, CaseIterable, Equatable, Comparable {
    case excellent = 5
    case good      = 4
    case fair      = 3
    case poor      = 2
    case terrible  = 1

    var locKey: String {
        switch self {
        case .excellent: return "trade.rating.excellent"
        case .good:      return "trade.rating.good"
        case .fair:      return "trade.rating.fair"
        case .poor:      return "trade.rating.poor"
        case .terrible:  return "trade.rating.terrible"
        }
    }

    var color: Color {
        switch self {
        case .excellent: return LxColor.neonLime
        case .good:      return LxColor.electricCyan
        case .fair:      return LxColor.gold
        case .poor:      return LxColor.amber
        case .terrible:  return LxColor.bloodRed
        }
    }

    var stars: Int { rawValue }

    static func < (lhs: TradeRating, rhs: TradeRating) -> Bool {
        lhs.rawValue < rhs.rawValue
    }
}

// MARK: - Entry/Exit Snapshot

struct TradeExecutionSnapshot: Codable, Equatable {
    var orderId: String = ""
    var timestamp: Date = .distantPast
    var price: Double = 0               // requested price
    var fillPrice: Double = 0           // actual avg fill price
    var qty: Double = 0                 // qty
    var slippageBps: Double = 0         // |fillPrice - price| / price * 10000
    var fillTimeUs: Double = 0          // time to fill in microseconds
    var fillProbability: Double = 0     // predicted fill probability at submission
    var cancelAttempts: Int = 0         // cancel attempts during this leg
}

// MARK: - Risk Snapshot at Trade Time

struct TradeRiskSnapshot: Codable, Equatable {
    var regime: Int = 0                 // market regime at entry
    var regimeConfidence: Double = 0    // regime confidence
    var volatility: Double = 0          // market volatility
    var drawdownPct: Double = 0         // portfolio drawdown at trade time
    var cbTripped: Bool = false         // circuit breaker state
    var positionSizePct: Double = 0     // position size as % of max
    var leverageUsed: Double = 0        // effective leverage
    var var99AtEntry: Double = 0        // VaR 99 at entry time
}

// MARK: - Completed Trade Record

struct CompletedTrade: Identifiable, Codable, Equatable {
    let id: UUID
    let tradeNumber: Int                // sequential trade number
    let symbol: String
    let side: TradeSide
    let outcome: TradeOutcome

    // Entry
    var entry: TradeExecutionSnapshot
    // Exit
    var exit: TradeExecutionSnapshot

    // PnL
    var realizedPnl: Double = 0         // measured: from position snapshot delta
    var pnlPercent: Double = 0          // derived: realizedPnl / (entry.fillPrice * entry.qty)
    var fees: Double = 0                // estimated fees (derived)

    // Duration
    var holdDurationSec: Double = 0     // derived: exit.timestamp - entry.timestamp

    // Execution quality
    var entrySlippageBps: Double = 0    // derived: entry slippage
    var exitSlippageBps: Double = 0     // derived: exit slippage
    var totalSlippageBps: Double = 0    // derived: entry + exit slippage
    var executionScore: Double = 0      // derived: 0–100 composite execution quality

    // Risk context
    var riskAtEntry: TradeRiskSnapshot
    var riskAtExit: TradeRiskSnapshot

    // AI context
    var signalConfidence: Double = 0    // model confidence at signal time
    var predictionCorrect: Bool = false // was the directional prediction correct?

    // Rating (auto-computed, can be overridden)
    var autoRating: TradeRating
    var userRating: TradeRating?        // user override
    var userNotes: String = ""          // free-form journal notes

    // Incidents during trade
    var incidentIds: [UUID] = []        // IDs of incidents that occurred during this trade

    // Replay linkage
    var replayTimestampNs: UInt64 = 0   // nanosecond timestamp for replay seek

    var effectiveRating: TradeRating {
        userRating ?? autoRating
    }

    var durationFormatted: String {
        if holdDurationSec < 60 {
            return String(format: "%.1fs", holdDurationSec)
        } else if holdDurationSec < 3600 {
            return String(format: "%.1fm", holdDurationSec / 60.0)
        } else {
            return String(format: "%.1fh", holdDurationSec / 3600.0)
        }
    }

    static func == (lhs: CompletedTrade, rhs: CompletedTrade) -> Bool {
        lhs.id == rhs.id && lhs.userNotes == rhs.userNotes && lhs.userRating == rhs.userRating
    }
}

// MARK: - Trade Journal Statistics

struct TradeJournalStats: Equatable {
    var totalTrades: Int = 0
    var winCount: Int = 0
    var lossCount: Int = 0
    var breakEvenCount: Int = 0
    var winRate: Double = 0
    var totalPnl: Double = 0
    var avgPnl: Double = 0
    var avgWinPnl: Double = 0
    var avgLossPnl: Double = 0
    var bestTradePnl: Double = 0
    var worstTradePnl: Double = 0
    var profitFactor: Double = 0        // gross profit / gross loss
    var avgHoldDuration: Double = 0     // seconds
    var avgSlippage: Double = 0         // bps
    var avgExecutionScore: Double = 0   // 0–100
    var avgRating: Double = 0           // 1–5
    var longWinRate: Double = 0
    var shortWinRate: Double = 0
    var pnlByRegime: [Int: Double] = [:]  // regime → total PnL
    var tradesByHour: [Int: Int] = [:]    // hour → count
}

// MARK: - Trade Journal Filter

struct TradeJournalFilter: Equatable {
    var side: TradeSide? = nil
    var outcome: TradeOutcome? = nil
    var minRating: TradeRating? = nil
    var regime: Int? = nil
    var searchText: String = ""
    var dateFrom: Date? = nil
    var dateTo: Date? = nil

    var isActive: Bool {
        side != nil || outcome != nil || minRating != nil || regime != nil ||
        !searchText.isEmpty || dateFrom != nil || dateTo != nil
    }

    func matches(_ trade: CompletedTrade) -> Bool {
        if let s = side, trade.side != s { return false }
        if let o = outcome, trade.outcome != o { return false }
        if let r = minRating, trade.effectiveRating < r { return false }
        if let reg = regime, trade.riskAtEntry.regime != reg { return false }
        if !searchText.isEmpty {
            let lower = searchText.lowercased()
            if !trade.symbol.lowercased().contains(lower) &&
               !trade.userNotes.lowercased().contains(lower) &&
               !trade.entry.orderId.lowercased().contains(lower) {
                return false
            }
        }
        if let from = dateFrom, trade.entry.timestamp < from { return false }
        if let to = dateTo, trade.exit.timestamp > to { return false }
        return true
    }
}

// MARK: - Trade Journal Sort

enum TradeJournalSort: String, CaseIterable {
    case newest     = "newest"
    case oldest     = "oldest"
    case bestPnl    = "bestPnl"
    case worstPnl   = "worstPnl"
    case highRating = "highRating"
    case lowRating  = "lowRating"
    case longest    = "longest"
    case shortest   = "shortest"

    var locKey: String {
        switch self {
        case .newest:     return "trade.sort.newest"
        case .oldest:     return "trade.sort.oldest"
        case .bestPnl:    return "trade.sort.bestPnl"
        case .worstPnl:   return "trade.sort.worstPnl"
        case .highRating: return "trade.sort.highRating"
        case .lowRating:  return "trade.sort.lowRating"
        case .longest:    return "trade.sort.longest"
        case .shortest:   return "trade.sort.shortest"
        }
    }

    func apply(_ trades: [CompletedTrade]) -> [CompletedTrade] {
        switch self {
        case .newest:     return trades.sorted { $0.exit.timestamp > $1.exit.timestamp }
        case .oldest:     return trades.sorted { $0.exit.timestamp < $1.exit.timestamp }
        case .bestPnl:    return trades.sorted { $0.realizedPnl > $1.realizedPnl }
        case .worstPnl:   return trades.sorted { $0.realizedPnl < $1.realizedPnl }
        case .highRating: return trades.sorted { $0.effectiveRating > $1.effectiveRating }
        case .lowRating:  return trades.sorted { $0.effectiveRating < $1.effectiveRating }
        case .longest:    return trades.sorted { $0.holdDurationSec > $1.holdDurationSec }
        case .shortest:   return trades.sorted { $0.holdDurationSec < $1.holdDurationSec }
        }
    }
}

// MARK: - Trade Journal Store

final class TradeJournalStore: ObservableObject {
    static let shared = TradeJournalStore()

    @Published var trades: [CompletedTrade] = []
    private var nextTradeNumber: Int = 1
    private let maxTrades = 1000

    // Pending trade tracking (for matching entry → exit)
    private var pendingEntry: TradeExecutionSnapshot?
    private var pendingRisk: TradeRiskSnapshot?
    private var pendingSide: TradeSide?
    private var pendingSignalConfidence: Double = 0
    private var pendingSymbol: String = ""
    private var previousPositionSize: Double = 0

    // MARK: - Trade Detection

    /// Called every poll with current position data to detect trade completions.
    /// A trade is detected when position flips (long→flat, short→flat, long→short, etc.)
    func detectTradeCompletion(
        position: PositionSnapshot,
        metrics: MetricsSnapshot,
        regime: RegimeSnapshot,
        circuitBreaker: CircuitBreakerSnapshot,
        varState: VaRStateModel,
        prediction: PredictionSnapshot,
        osmOrders: [ManagedOrderModel],
        executionScore: Double,
        symbol: String
    ) {
        let currentSize = position.size

        // Detect position close or flip
        if previousPositionSize != 0 && currentSize == 0 {
            // Position closed → complete trade
            completeCurrentTrade(
                exitPosition: position,
                metrics: metrics,
                regime: regime,
                circuitBreaker: circuitBreaker,
                varState: varState,
                osmOrders: osmOrders,
                executionScore: executionScore
            )
        } else if previousPositionSize != 0 && currentSize != 0 &&
                  (previousPositionSize > 0) != (currentSize > 0) {
            // Position flipped → complete old trade, start new one
            completeCurrentTrade(
                exitPosition: position,
                metrics: metrics,
                regime: regime,
                circuitBreaker: circuitBreaker,
                varState: varState,
                osmOrders: osmOrders,
                executionScore: executionScore
            )
            startNewTrade(
                position: position,
                regime: regime,
                circuitBreaker: circuitBreaker,
                varState: varState,
                prediction: prediction,
                symbol: symbol
            )
        } else if previousPositionSize == 0 && currentSize != 0 {
            // New position opened → start tracking
            startNewTrade(
                position: position,
                regime: regime,
                circuitBreaker: circuitBreaker,
                varState: varState,
                prediction: prediction,
                symbol: symbol
            )
        }

        previousPositionSize = currentSize
    }

    private func startNewTrade(
        position: PositionSnapshot,
        regime: RegimeSnapshot,
        circuitBreaker: CircuitBreakerSnapshot,
        varState: VaRStateModel,
        prediction: PredictionSnapshot,
        symbol: String
    ) {
        pendingSide = position.isLong ? .long : .short
        pendingSymbol = symbol
        pendingSignalConfidence = prediction.modelConfidence

        pendingEntry = TradeExecutionSnapshot(
            orderId: "",
            timestamp: Date(),
            price: position.entryPrice,
            fillPrice: position.entryPrice,
            qty: abs(position.size),
            slippageBps: 0,
            fillTimeUs: 0,
            fillProbability: 0,
            cancelAttempts: 0
        )

        pendingRisk = TradeRiskSnapshot(
            regime: regime.current,
            regimeConfidence: regime.confidence,
            volatility: regime.volatility,
            drawdownPct: circuitBreaker.drawdownPct,
            cbTripped: circuitBreaker.tripped,
            positionSizePct: 0,
            leverageUsed: 0,
            var99AtEntry: varState.var99
        )
    }

    private func completeCurrentTrade(
        exitPosition: PositionSnapshot,
        metrics: MetricsSnapshot,
        regime: RegimeSnapshot,
        circuitBreaker: CircuitBreakerSnapshot,
        varState: VaRStateModel,
        osmOrders: [ManagedOrderModel],
        executionScore: Double
    ) {
        guard let entry = pendingEntry,
              let riskEntry = pendingRisk,
              let side = pendingSide else { return }

        let now = Date()
        let holdDuration = now.timeIntervalSince(entry.timestamp)
        let pnl = exitPosition.realizedPnl // delta from last trade would be more precise
        let pnlPct = (entry.fillPrice > 0 && entry.qty > 0) ?
            pnl / (entry.fillPrice * entry.qty) * 100.0 : 0

        let outcome: TradeOutcome
        if abs(pnl) < 0.0001 {
            outcome = .breakEven
        } else if pnl > 0 {
            outcome = .win
        } else {
            outcome = .loss
        }

        let exitSnapshot = TradeExecutionSnapshot(
            orderId: "",
            timestamp: now,
            price: exitPosition.entryPrice,
            fillPrice: exitPosition.entryPrice,
            qty: entry.qty,
            slippageBps: 0,
            fillTimeUs: 0,
            fillProbability: 0,
            cancelAttempts: 0
        )

        let riskExit = TradeRiskSnapshot(
            regime: regime.current,
            regimeConfidence: regime.confidence,
            volatility: regime.volatility,
            drawdownPct: circuitBreaker.drawdownPct,
            cbTripped: circuitBreaker.tripped,
            positionSizePct: 0,
            leverageUsed: 0,
            var99AtEntry: varState.var99
        )

        // Auto-rate based on PnL, execution, and risk
        let autoRating = computeAutoRating(
            pnl: pnl,
            executionScore: executionScore,
            slippage: entry.slippageBps + exitSnapshot.slippageBps,
            holdDuration: holdDuration,
            predictionCorrect: (side == .long && pnl > 0) || (side == .short && pnl > 0)
        )

        // Collect incidents that occurred during this trade
        let tradeIncidents = IncidentStore.shared.incidents.filter { incident in
            incident.timestamp >= entry.timestamp && incident.timestamp <= now
        }.map { $0.id }

        let trade = CompletedTrade(
            id: UUID(),
            tradeNumber: nextTradeNumber,
            symbol: pendingSymbol,
            side: side,
            outcome: outcome,
            entry: entry,
            exit: exitSnapshot,
            realizedPnl: pnl,
            pnlPercent: pnlPct,
            fees: 0,
            holdDurationSec: holdDuration,
            entrySlippageBps: entry.slippageBps,
            exitSlippageBps: exitSnapshot.slippageBps,
            totalSlippageBps: entry.slippageBps + exitSnapshot.slippageBps,
            executionScore: executionScore,
            riskAtEntry: riskEntry,
            riskAtExit: riskExit,
            signalConfidence: pendingSignalConfidence,
            predictionCorrect: (side == .long && pnl > 0) || (side == .short && pnl > 0),
            autoRating: autoRating,
            incidentIds: tradeIncidents,
            replayTimestampNs: 0
        )

        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.trades.insert(trade, at: 0)
            if self.trades.count > self.maxTrades {
                self.trades.removeLast(self.trades.count - self.maxTrades)
            }
            self.nextTradeNumber += 1
        }

        // Clear pending
        pendingEntry = nil
        pendingRisk = nil
        pendingSide = nil
        pendingSignalConfidence = 0
    }

    // MARK: - Auto Rating

    private func computeAutoRating(
        pnl: Double,
        executionScore: Double,
        slippage: Double,
        holdDuration: Double,
        predictionCorrect: Bool
    ) -> TradeRating {
        var score = 0.0

        // PnL contribution (40%)
        if pnl > 0 {
            score += 40.0
        } else if pnl > -0.001 {
            score += 20.0
        }

        // Execution quality (30%)
        score += executionScore * 0.3

        // Low slippage bonus (15%)
        if slippage < 1.0 {
            score += 15.0
        } else if slippage < 3.0 {
            score += 10.0
        } else if slippage < 5.0 {
            score += 5.0
        }

        // Prediction accuracy (15%)
        if predictionCorrect {
            score += 15.0
        }

        if score >= 80 { return .excellent }
        if score >= 65 { return .good }
        if score >= 45 { return .fair }
        if score >= 25 { return .poor }
        return .terrible
    }

    // MARK: - User Actions

    func setUserRating(_ tradeId: UUID, rating: TradeRating) {
        if let idx = trades.firstIndex(where: { $0.id == tradeId }) {
            trades[idx].userRating = rating
        }
    }

    func setUserNotes(_ tradeId: UUID, notes: String) {
        if let idx = trades.firstIndex(where: { $0.id == tradeId }) {
            trades[idx].userNotes = notes
        }
    }

    func clearUserRating(_ tradeId: UUID) {
        if let idx = trades.firstIndex(where: { $0.id == tradeId }) {
            trades[idx].userRating = nil
        }
    }

    // MARK: - Statistics

    func computeStats(for filteredTrades: [CompletedTrade]? = nil) -> TradeJournalStats {
        let source = filteredTrades ?? trades
        guard !source.isEmpty else { return TradeJournalStats() }

        var stats = TradeJournalStats()
        stats.totalTrades = source.count

        var grossProfit = 0.0
        var grossLoss = 0.0
        var totalWinPnl = 0.0
        var totalLossPnl = 0.0
        var longWins = 0, longTotal = 0
        var shortWins = 0, shortTotal = 0

        for trade in source {
            switch trade.outcome {
            case .win:
                stats.winCount += 1
                totalWinPnl += trade.realizedPnl
                grossProfit += trade.realizedPnl
            case .loss:
                stats.lossCount += 1
                totalLossPnl += trade.realizedPnl
                grossLoss += abs(trade.realizedPnl)
            case .breakEven:
                stats.breakEvenCount += 1
            }

            stats.totalPnl += trade.realizedPnl
            stats.avgSlippage += trade.totalSlippageBps
            stats.avgExecutionScore += trade.executionScore
            stats.avgRating += Double(trade.effectiveRating.rawValue)
            stats.avgHoldDuration += trade.holdDurationSec

            if trade.realizedPnl > stats.bestTradePnl { stats.bestTradePnl = trade.realizedPnl }
            if trade.realizedPnl < stats.worstTradePnl { stats.worstTradePnl = trade.realizedPnl }

            // By side
            if trade.side == .long {
                longTotal += 1
                if trade.outcome == .win { longWins += 1 }
            } else {
                shortTotal += 1
                if trade.outcome == .win { shortWins += 1 }
            }

            // By regime
            let reg = trade.riskAtEntry.regime
            stats.pnlByRegime[reg, default: 0] += trade.realizedPnl

            // By hour
            let hour = Calendar.current.component(.hour, from: trade.entry.timestamp)
            stats.tradesByHour[hour, default: 0] += 1
        }

        let n = Double(source.count)
        stats.winRate = n > 0 ? Double(stats.winCount) / n : 0
        stats.avgPnl = n > 0 ? stats.totalPnl / n : 0
        stats.avgWinPnl = stats.winCount > 0 ? totalWinPnl / Double(stats.winCount) : 0
        stats.avgLossPnl = stats.lossCount > 0 ? totalLossPnl / Double(stats.lossCount) : 0
        stats.profitFactor = grossLoss > 0 ? grossProfit / grossLoss : (grossProfit > 0 ? .infinity : 0)
        stats.avgHoldDuration = n > 0 ? stats.avgHoldDuration / n : 0
        stats.avgSlippage = n > 0 ? stats.avgSlippage / n : 0
        stats.avgExecutionScore = n > 0 ? stats.avgExecutionScore / n : 0
        stats.avgRating = n > 0 ? stats.avgRating / n : 0
        stats.longWinRate = longTotal > 0 ? Double(longWins) / Double(longTotal) : 0
        stats.shortWinRate = shortTotal > 0 ? Double(shortWins) / Double(shortTotal) : 0

        return stats
    }

    // MARK: - Export

    private static let isoFormatter: ISO8601DateFormatter = {
        let f = ISO8601DateFormatter()
        return f
    }()
    
    func exportCSV() -> String {
        var csv = "Trade#,Symbol,Side,Outcome,EntryTime,ExitTime,EntryPrice,ExitPrice,"
        csv += "Qty,PnL,PnL%,HoldDuration,Slippage(bps),ExecScore,Rating,Regime,Notes\n"

        let formatter = Self.isoFormatter
        for trade in trades.sorted(by: { $0.tradeNumber < $1.tradeNumber }) {
            csv += "\(trade.tradeNumber),"
            csv += "\(trade.symbol),"
            csv += "\(trade.side.rawValue),"
            csv += "\(trade.outcome.rawValue),"
            csv += "\(formatter.string(from: trade.entry.timestamp)),"
            csv += "\(formatter.string(from: trade.exit.timestamp)),"
            csv += String(format: "%.6f,", trade.entry.fillPrice)
            csv += String(format: "%.6f,", trade.exit.fillPrice)
            csv += String(format: "%.6f,", trade.entry.qty)
            csv += String(format: "%.6f,", trade.realizedPnl)
            csv += String(format: "%.2f,", trade.pnlPercent)
            csv += String(format: "%.1f,", trade.holdDurationSec)
            csv += String(format: "%.2f,", trade.totalSlippageBps)
            csv += String(format: "%.1f,", trade.executionScore)
            csv += "\(trade.effectiveRating.rawValue),"
            csv += "\(trade.riskAtEntry.regime),"
            csv += "\"\(trade.userNotes.replacingOccurrences(of: "\"", with: "\"\""))\"\n"
        }
        return csv
    }

    func clear() {
        trades.removeAll()
        nextTradeNumber = 1
        pendingEntry = nil
        pendingRisk = nil
        pendingSide = nil
    }
}
