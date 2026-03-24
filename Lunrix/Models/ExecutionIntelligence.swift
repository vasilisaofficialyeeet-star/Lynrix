// ExecutionIntelligence.swift — Execution analytics model layer (Lynrix v2.5 Sprint 3)
// Centralized execution intelligence: slippage, fill quality, latency decomposition,
// execution scoring, and actionable insights — all derived from measured engine data.

import Foundation
import SwiftUI

// MARK: - Execution Event

/// Snapshot of a single order's execution quality at observation time.
/// Fields marked "measured" come directly from the C++ engine.
/// Fields marked "derived" are computed from measured values.
struct ExecutionEvent: Identifiable, Equatable {
    let id: String                  // order ID (measured)
    let observedAt: Date            // when this snapshot was taken
    let isBuy: Bool                 // measured
    let expectedPrice: Double       // order price at submission (measured)
    let fillPrice: Double           // avg fill price (measured, 0 if unfilled)
    let qty: Double                 // requested qty (measured)
    let filledQty: Double           // filled qty (measured)
    let slippageBps: Double         // derived: |fillPrice - expectedPrice| / expectedPrice * 10000
    let fillTimeUs: Double          // derived: (lastUpdateNs - createdNs) / 1000
    let fillCompleteness: Double    // derived: filledQty / qty
    let state: Int                  // measured: order state enum
    let fillProbability: Double     // measured: predicted fill probability
    let cancelAttempts: Int         // measured
    let grade: FillQualityGrade     // derived from slippageBps

    static func == (lhs: ExecutionEvent, rhs: ExecutionEvent) -> Bool {
        lhs.id == rhs.id && lhs.state == rhs.state && lhs.filledQty == rhs.filledQty
    }
}

// MARK: - Fill Quality Grade

enum FillQualityGrade: String, CaseIterable {
    case excellent = "excellent"    // < 1 bps
    case good      = "good"        // 1–3 bps
    case fair      = "fair"        // 3–5 bps
    case poor      = "poor"        // > 5 bps

    var color: Color {
        switch self {
        case .excellent: return LxColor.neonLime
        case .good:      return LxColor.electricCyan
        case .fair:      return LxColor.amber
        case .poor:      return LxColor.bloodRed
        }
    }

    var locKey: String {
        switch self {
        case .excellent: return "exec.gradeExcellent"
        case .good:      return "exec.gradeGood"
        case .fair:      return "exec.gradeFair"
        case .poor:      return "exec.gradePoor"
        }
    }

    var icon: String {
        switch self {
        case .excellent: return "checkmark.seal.fill"
        case .good:      return "checkmark.circle.fill"
        case .fair:      return "exclamationmark.circle.fill"
        case .poor:      return "xmark.circle.fill"
        }
    }

    static func from(slippageBps: Double) -> FillQualityGrade {
        if slippageBps < 1.0 { return .excellent }
        if slippageBps < 3.0 { return .good }
        if slippageBps < 5.0 { return .fair }
        return .poor
    }
}

// MARK: - Slippage Analytics

struct SlippageAnalytics: Equatable {
    var avgSlippageBps: Double = 0          // measured (EMA from C++)
    var marketImpactBps: Double = 0         // measured (EMA from C++)
    var recentOrderSlippages: [Double] = [] // derived: per-order slippage for recent fills
    var maxRecentSlippage: Double = 0       // derived: max of recent
    var minRecentSlippage: Double = 0       // derived: min of recent
    var buyAvgSlippage: Double = 0          // derived: avg for buy orders
    var sellAvgSlippage: Double = 0         // derived: avg for sell orders
    var grade: FillQualityGrade = .good     // derived from avgSlippageBps

    var isElevated: Bool { avgSlippageBps > 3.0 }
    var isCritical: Bool { avgSlippageBps > 5.0 }
}

// MARK: - Fill Quality Summary

struct FillQualitySummary: Equatable {
    var fillRate: Double = 0                // derived: ordersFilled / ordersSent
    var cancelRate: Double = 0              // derived: ordersCancelled / ordersSent
    var avgFillTimeUs: Double = 0           // measured (EMA from C++)
    var signalEfficiency: Double = 0        // derived: ordersSent / signalsTotal
    var partialFillCount: Int = 0           // derived: count of orders in PartialFill state
    var totalFilledOrders: UInt64 = 0       // measured
    var totalSentOrders: UInt64 = 0         // measured
    var totalCancelledOrders: UInt64 = 0    // measured

    var fillRateGrade: FillQualityGrade {
        if fillRate >= 0.9 { return .excellent }
        if fillRate >= 0.7 { return .good }
        if fillRate >= 0.5 { return .fair }
        return .poor
    }
}

// MARK: - Latency Stage

struct LatencyStage: Identifiable, Equatable {
    let id: String
    let name: String            // measured: stage name from histogram
    var p50Us: Double = 0       // measured
    var p99Us: Double = 0       // measured
    var meanUs: Double = 0      // measured
    var maxUs: Double = 0       // measured
    var stddevUs: Double = 0    // measured
    var count: UInt64 = 0       // measured
    var contributionPct: Double = 0  // derived: stage p50 / total p50
    var isBottleneck: Bool = false   // derived: highest contribution

    var locKey: String {
        "exec.stage.\(id)"
    }
}

// MARK: - Latency Decomposition

struct LatencyDecomposition: Equatable {
    var stages: [LatencyStage] = []         // derived from StageHistogramModel[]
    var totalP50Us: Double = 0              // measured: e2e p50
    var totalP99Us: Double = 0              // measured: e2e p99
    var exchangeLatencyMs: Double = 0       // measured
    var bottleneckStage: String = ""        // derived: stage with highest contribution
    var internalLatencyPct: Double = 0      // derived: internal stages / total
}

// MARK: - Execution Score

struct ExecutionScore: Equatable {
    var overall: Double = 0             // derived: weighted composite (0–100)
    var fillRateComponent: Double = 0   // derived (weight: 30%)
    var slippageComponent: Double = 0   // derived (weight: 25%)
    var latencyComponent: Double = 0    // derived (weight: 20%)
    var impactComponent: Double = 0     // derived (weight: 15%)
    var efficiencyComponent: Double = 0 // derived (weight: 10%)

    var grade: String {
        if overall >= 95 { return "A+" }
        if overall >= 90 { return "A" }
        if overall >= 80 { return "B+" }
        if overall >= 70 { return "B" }
        if overall >= 60 { return "C" }
        if overall >= 50 { return "D" }
        return "F"
    }

    var gradeColor: Color {
        if overall >= 90 { return LxColor.neonLime }
        if overall >= 70 { return LxColor.electricCyan }
        if overall >= 50 { return LxColor.amber }
        return LxColor.bloodRed
    }

    var locKey: String { "exec.score" }
}

// MARK: - Execution Insight

enum InsightSeverity: String, Equatable {
    case info     = "info"
    case warning  = "warning"
    case critical = "critical"

    var color: Color {
        switch self {
        case .info:     return LxColor.electricCyan
        case .warning:  return LxColor.amber
        case .critical: return LxColor.bloodRed
        }
    }

    var icon: String {
        switch self {
        case .info:     return "lightbulb.fill"
        case .warning:  return "exclamationmark.triangle.fill"
        case .critical: return "exclamationmark.octagon.fill"
        }
    }
}

struct ExecutionInsight: Identifiable, Equatable {
    let id: UUID
    let timestamp: Date
    let severity: InsightSeverity
    let titleKey: String        // localization key
    let detail: String          // human-readable detail with evidence
    let metric: String          // which metric triggered this
    let value: Double           // actual measured/derived value
    let threshold: Double       // threshold that was crossed
    let isInferred: Bool        // true if based on derived data, false if directly measured

    static func == (lhs: ExecutionInsight, rhs: ExecutionInsight) -> Bool {
        lhs.id == rhs.id
    }
}

// MARK: - Execution Analytics (aggregate)

struct ExecutionAnalytics: Equatable {
    var slippage: SlippageAnalytics = .init()
    var fillQuality: FillQualitySummary = .init()
    var latency: LatencyDecomposition = .init()
    var score: ExecutionScore = .init()
    var insights: [ExecutionInsight] = []
    var recentEvents: [ExecutionEvent] = []
    var poorExecutions: [ExecutionEvent] = []
    var lastUpdate: Date = .distantPast
    var hasData: Bool = false

    static func == (lhs: ExecutionAnalytics, rhs: ExecutionAnalytics) -> Bool {
        lhs.slippage == rhs.slippage &&
        lhs.fillQuality == rhs.fillQuality &&
        lhs.latency == rhs.latency &&
        lhs.score == rhs.score &&
        lhs.insights.count == rhs.insights.count &&
        lhs.recentEvents == rhs.recentEvents
    }
}

// MARK: - Analytics Computation Engine

/// Computes execution analytics from engine state. All inputs are measured values
/// from the C++ core; outputs are clearly documented as measured or derived.
enum ExecutionAnalyticsComputer {

    private static let maxRecentEvents = 50
    private static let maxInsights = 20

    // MARK: - Primary Computation

    static func compute(
        osm: OSMSummaryModel,
        metrics: MetricsSnapshot,
        system: SystemMonitorModel,
        stages: [StageHistogramModel],
        strategy: StrategyMetricsModel,
        health: StrategyHealthModel,
        previousEvents: [ExecutionEvent],
        previousInsights: [ExecutionInsight]
    ) -> ExecutionAnalytics {

        let slippage = computeSlippage(osm: osm, orders: osm.orders)
        let fillQuality = computeFillQuality(metrics: metrics, osm: osm)
        let latency = computeLatency(system: system, stages: stages)
        let recentEvents = computeRecentEvents(orders: osm.orders, previous: previousEvents)
        let poorExecutions = recentEvents.filter { $0.grade == .poor || $0.grade == .fair }
        let score = computeScore(
            slippage: slippage, fillQuality: fillQuality,
            latency: latency, metrics: metrics
        )
        let insights = computeInsights(
            slippage: slippage, fillQuality: fillQuality,
            latency: latency, score: score,
            previous: previousInsights
        )

        let hasData = metrics.ordersSent > 0 || !stages.isEmpty

        return ExecutionAnalytics(
            slippage: slippage,
            fillQuality: fillQuality,
            latency: latency,
            score: score,
            insights: insights,
            recentEvents: Array(recentEvents.prefix(maxRecentEvents)),
            poorExecutions: Array(poorExecutions.prefix(10)),
            lastUpdate: Date(),
            hasData: hasData
        )
    }

    // MARK: - Slippage

    private static func computeSlippage(osm: OSMSummaryModel, orders: [ManagedOrderModel]) -> SlippageAnalytics {
        var analytics = SlippageAnalytics()
        analytics.avgSlippageBps = osm.avgSlippage * 10000 // C++ stores as fraction, convert to bps
        analytics.marketImpactBps = osm.marketImpactBps
        analytics.grade = FillQualityGrade.from(slippageBps: analytics.avgSlippageBps)

        // Per-order slippage for filled/partial orders
        var buySlippages: [Double] = []
        var sellSlippages: [Double] = []

        for order in orders {
            guard order.avgFillPrice > 0 && order.price > 0 else { continue }
            guard order.state == 3 || order.state == 4 else { continue } // PartialFill or Filled
            let slip = abs(order.avgFillPrice - order.price) / order.price * 10000.0
            analytics.recentOrderSlippages.append(slip)
            if order.isBuy {
                buySlippages.append(slip)
            } else {
                sellSlippages.append(slip)
            }
        }

        if !analytics.recentOrderSlippages.isEmpty {
            analytics.maxRecentSlippage = analytics.recentOrderSlippages.max() ?? 0
            analytics.minRecentSlippage = analytics.recentOrderSlippages.min() ?? 0
        }
        if !buySlippages.isEmpty {
            analytics.buyAvgSlippage = buySlippages.reduce(0, +) / Double(buySlippages.count)
        }
        if !sellSlippages.isEmpty {
            analytics.sellAvgSlippage = sellSlippages.reduce(0, +) / Double(sellSlippages.count)
        }

        return analytics
    }

    // MARK: - Fill Quality

    private static func computeFillQuality(metrics: MetricsSnapshot, osm: OSMSummaryModel) -> FillQualitySummary {
        var fq = FillQualitySummary()
        fq.totalFilledOrders = metrics.ordersFilled
        fq.totalSentOrders = metrics.ordersSent
        fq.totalCancelledOrders = metrics.ordersCancelled
        fq.avgFillTimeUs = osm.avgFillTimeUs

        if metrics.ordersSent > 0 {
            fq.fillRate = Double(metrics.ordersFilled) / Double(metrics.ordersSent)
            fq.cancelRate = Double(metrics.ordersCancelled) / Double(metrics.ordersSent)
        }

        if metrics.signalsTotal > 0 {
            fq.signalEfficiency = Double(metrics.ordersSent) / Double(metrics.signalsTotal)
        }

        // Count partial fills from active orders
        fq.partialFillCount = osm.orders.filter { $0.state == 3 }.count

        return fq
    }

    // MARK: - Latency Decomposition

    private static func computeLatency(
        system: SystemMonitorModel,
        stages: [StageHistogramModel]
    ) -> LatencyDecomposition {
        var decomp = LatencyDecomposition()
        decomp.totalP50Us = system.e2eLatencyP50Us
        decomp.totalP99Us = system.e2eLatencyP99Us
        decomp.exchangeLatencyMs = system.exchangeLatencyMs

        let totalMean = stages.reduce(0.0) { $0 + $1.meanUs }
        var maxContribution = 0.0
        var bottleneck = ""

        decomp.stages = stages.map { h in
            let contribution = totalMean > 0 ? (h.meanUs / totalMean * 100.0) : 0
            let isBiggest = contribution > maxContribution
            if isBiggest {
                maxContribution = contribution
                bottleneck = h.stageName
            }
            return LatencyStage(
                id: h.id, name: h.stageName,
                p50Us: h.p50Us, p99Us: h.p99Us,
                meanUs: h.meanUs, maxUs: h.maxUs,
                stddevUs: h.stddevUs, count: h.count,
                contributionPct: contribution,
                isBottleneck: false // set below
            )
        }

        // Mark bottleneck
        for i in decomp.stages.indices {
            decomp.stages[i].isBottleneck = (decomp.stages[i].name == bottleneck)
        }
        decomp.bottleneckStage = bottleneck

        // Internal vs exchange proportion
        if system.e2eLatencyP50Us > 0 {
            let exchangeUs = system.exchangeLatencyMs * 1000.0
            let internalUs = max(0, system.e2eLatencyP50Us - exchangeUs)
            decomp.internalLatencyPct = internalUs / system.e2eLatencyP50Us * 100.0
        }

        return decomp
    }

    // MARK: - Execution Score

    /// Composite score (0–100) from measured execution quality metrics.
    /// Weights: fill rate 30%, slippage 25%, latency 20%, market impact 15%, efficiency 10%.
    private static func computeScore(
        slippage: SlippageAnalytics,
        fillQuality: FillQualitySummary,
        latency: LatencyDecomposition,
        metrics: MetricsSnapshot
    ) -> ExecutionScore {
        var s = ExecutionScore()

        // Fill rate: 100% fill rate = 100 score
        s.fillRateComponent = min(fillQuality.fillRate * 100.0, 100.0)

        // Slippage: 0 bps = 100, 10+ bps = 0
        s.slippageComponent = max(0, 100.0 - slippage.avgSlippageBps * 10.0)

        // Latency: p99 < 50µs = 100, > 5000µs = 0
        if latency.totalP99Us > 0 {
            s.latencyComponent = max(0, min(100.0, 100.0 - (latency.totalP99Us - 50.0) / 49.5))
        } else {
            s.latencyComponent = 100.0
        }

        // Market impact: 0 bps = 100, 5+ bps = 0
        s.impactComponent = max(0, 100.0 - slippage.marketImpactBps * 20.0)

        // Signal efficiency: 100% = 100 score
        s.efficiencyComponent = min(fillQuality.signalEfficiency * 100.0, 100.0)

        // Weighted composite
        s.overall = s.fillRateComponent * 0.30 +
                    s.slippageComponent * 0.25 +
                    s.latencyComponent * 0.20 +
                    s.impactComponent * 0.15 +
                    s.efficiencyComponent * 0.10

        // Clamp
        s.overall = max(0, min(100, s.overall))

        return s
    }

    // MARK: - Recent Events

    private static func computeRecentEvents(
        orders: [ManagedOrderModel],
        previous: [ExecutionEvent]
    ) -> [ExecutionEvent] {
        var events = previous

        for order in orders {
            // Only track filled or partial-filled orders
            guard order.state == 3 || order.state == 4 else { continue }
            guard order.avgFillPrice > 0 && order.price > 0 else { continue }

            // Skip if already tracked with same state
            if events.contains(where: { $0.id == order.id && $0.state == order.state && $0.filledQty == order.filledQty }) {
                continue
            }
            // Remove old snapshot of same order if state changed
            events.removeAll { $0.id == order.id }

            let slip = abs(order.avgFillPrice - order.price) / order.price * 10000.0
            let fillUs: Double
            if order.lastUpdateNs > order.createdNs {
                fillUs = Double(order.lastUpdateNs - order.createdNs) / 1000.0
            } else {
                fillUs = 0
            }

            let event = ExecutionEvent(
                id: order.id,
                observedAt: Date(),
                isBuy: order.isBuy,
                expectedPrice: order.price,
                fillPrice: order.avgFillPrice,
                qty: order.qty,
                filledQty: order.filledQty,
                slippageBps: slip,
                fillTimeUs: fillUs,
                fillCompleteness: order.qty > 0 ? order.filledQty / order.qty : 0,
                state: order.state,
                fillProbability: order.fillProbability,
                cancelAttempts: order.cancelAttempts,
                grade: FillQualityGrade.from(slippageBps: slip)
            )
            events.append(event)
        }

        // Keep only most recent
        if events.count > maxRecentEvents {
            events = Array(events.suffix(maxRecentEvents))
        }
        return events
    }

    // MARK: - Insights

    /// Generates actionable insights from measured and derived metrics.
    /// Each insight references the specific metric and threshold that triggered it.
    private static func computeInsights(
        slippage: SlippageAnalytics,
        fillQuality: FillQualitySummary,
        latency: LatencyDecomposition,
        score: ExecutionScore,
        previous: [ExecutionInsight]
    ) -> [ExecutionInsight] {
        var insights: [ExecutionInsight] = []

        // Slippage elevated (measured: avgSlippage from C++ EMA)
        if slippage.avgSlippageBps > 3.0 {
            let sev: InsightSeverity = slippage.avgSlippageBps > 5.0 ? .critical : .warning
            insights.append(ExecutionInsight(
                id: stableID("slippage_elevated"),
                timestamp: Date(), severity: sev,
                titleKey: "exec.insight.slippageElevated",
                detail: String(format: "%.2f bps (threshold: 3.0 bps)", slippage.avgSlippageBps),
                metric: "avgSlippageBps", value: slippage.avgSlippageBps,
                threshold: 3.0, isInferred: false
            ))
        }

        // Fill rate low (derived from measured counters)
        if fillQuality.totalSentOrders > 10 && fillQuality.fillRate < 0.5 {
            insights.append(ExecutionInsight(
                id: stableID("fill_rate_low"),
                timestamp: Date(), severity: .warning,
                titleKey: "exec.insight.fillRateLow",
                detail: String(format: "%.1f%% (threshold: 50%%)", fillQuality.fillRate * 100),
                metric: "fillRate", value: fillQuality.fillRate,
                threshold: 0.5, isInferred: false
            ))
        }

        // Latency spike (measured: e2e p99)
        if latency.totalP99Us > 500 {
            insights.append(ExecutionInsight(
                id: stableID("latency_spike"),
                timestamp: Date(), severity: .warning,
                titleKey: "exec.insight.latencySpike",
                detail: String(format: "p99 = %.0f µs (threshold: 500 µs)", latency.totalP99Us),
                metric: "e2eLatencyP99Us", value: latency.totalP99Us,
                threshold: 500, isInferred: false
            ))
        }

        // Market impact elevated (measured: marketImpactBps from C++ EMA)
        if slippage.marketImpactBps > 3.0 {
            insights.append(ExecutionInsight(
                id: stableID("market_impact_high"),
                timestamp: Date(), severity: .info,
                titleKey: "exec.insight.marketImpactHigh",
                detail: String(format: "%.2f bps (threshold: 3.0 bps)", slippage.marketImpactBps),
                metric: "marketImpactBps", value: slippage.marketImpactBps,
                threshold: 3.0, isInferred: false
            ))
        }

        // Bottleneck identification (derived from measured stage histograms)
        if !latency.bottleneckStage.isEmpty {
            if let stage = latency.stages.first(where: { $0.isBottleneck }),
               stage.contributionPct > 40.0 {
                insights.append(ExecutionInsight(
                    id: stableID("latency_bottleneck"),
                    timestamp: Date(), severity: .info,
                    titleKey: "exec.insight.latencyBottleneck",
                    detail: "\(stage.name): \(String(format: "%.0f%%", stage.contributionPct)) of pipeline",
                    metric: "stageContribution", value: stage.contributionPct,
                    threshold: 40.0, isInferred: true
                ))
            }
        }

        // Fill delay worsening (measured: avgFillTimeUs from C++ EMA)
        if fillQuality.avgFillTimeUs > 1000 {
            insights.append(ExecutionInsight(
                id: stableID("fill_delay_high"),
                timestamp: Date(), severity: .warning,
                titleKey: "exec.insight.fillDelayHigh",
                detail: String(format: "%.0f µs (threshold: 1000 µs)", fillQuality.avgFillTimeUs),
                metric: "avgFillTimeUs", value: fillQuality.avgFillTimeUs,
                threshold: 1000, isInferred: false
            ))
        }

        // Cancel rate high (derived from measured counters)
        if fillQuality.totalSentOrders > 10 && fillQuality.cancelRate > 0.3 {
            insights.append(ExecutionInsight(
                id: stableID("cancel_rate_high"),
                timestamp: Date(), severity: .info,
                titleKey: "exec.insight.cancelRateHigh",
                detail: String(format: "%.1f%% (threshold: 30%%)", fillQuality.cancelRate * 100),
                metric: "cancelRate", value: fillQuality.cancelRate,
                threshold: 0.3, isInferred: false
            ))
        }

        // Partial fill surge (derived from current order snapshot)
        if fillQuality.partialFillCount > 3 {
            insights.append(ExecutionInsight(
                id: stableID("partial_fills_elevated"),
                timestamp: Date(), severity: .info,
                titleKey: "exec.insight.partialFillsElevated",
                detail: "\(fillQuality.partialFillCount) partial fills active",
                metric: "partialFillCount",
                value: Double(fillQuality.partialFillCount),
                threshold: 3, isInferred: false
            ))
        }

        // Overall score degradation (derived composite)
        if score.overall > 0 && score.overall < 50 && fillQuality.totalSentOrders > 10 {
            insights.append(ExecutionInsight(
                id: stableID("score_degraded"),
                timestamp: Date(), severity: .critical,
                titleKey: "exec.insight.scoreDegraded",
                detail: String(format: "Score: %.0f/100 (grade: %@)", score.overall, score.grade),
                metric: "executionScore", value: score.overall,
                threshold: 50, isInferred: true
            ))
        }

        return Array(insights.prefix(maxInsights))
    }

    // MARK: - Incident Generation

    /// Returns incidents to record when execution quality degrades significantly.
    /// Uses disciplined thresholds to avoid incident spam.
    static func generateIncidents(
        analytics: ExecutionAnalytics,
        previousAnalytics: ExecutionAnalytics
    ) -> [(severity: IncidentSeverity, category: IncidentCategory, titleKey: String, detail: String)] {
        var incidents: [(severity: IncidentSeverity, category: IncidentCategory, titleKey: String, detail: String)] = []

        // Slippage spike: only trigger when crossing from below to above threshold
        if analytics.slippage.isCritical && !previousAnalytics.slippage.isCritical {
            incidents.append((
                severity: .warning, category: .execution,
                titleKey: "incident.slippageSpike",
                detail: String(format: "Avg slippage: %.2f bps", analytics.slippage.avgSlippageBps)
            ))
        }

        // Fill rate collapse
        if analytics.fillQuality.totalSentOrders > 20 &&
           analytics.fillQuality.fillRate < 0.3 &&
           previousAnalytics.fillQuality.fillRate >= 0.3 {
            incidents.append((
                severity: .warning, category: .execution,
                titleKey: "incident.fillRateCollapse",
                detail: String(format: "Fill rate: %.1f%%", analytics.fillQuality.fillRate * 100)
            ))
        }

        // Latency anomaly
        if analytics.latency.totalP99Us > 1000 && previousAnalytics.latency.totalP99Us <= 1000 {
            incidents.append((
                severity: .warning, category: .execution,
                titleKey: "incident.latencyAnomaly",
                detail: String(format: "E2E p99: %.0f µs", analytics.latency.totalP99Us)
            ))
        }

        // Score collapse
        if analytics.score.overall < 40 && previousAnalytics.score.overall >= 40 &&
           analytics.fillQuality.totalSentOrders > 10 {
            incidents.append((
                severity: .critical, category: .execution,
                titleKey: "incident.execScoreCollapse",
                detail: String(format: "Execution score: %.0f/100 (%@)", analytics.score.overall, analytics.score.grade)
            ))
        }

        return incidents
    }

    // MARK: - Helpers

    /// Stable UUID from string seed for insight dedup across polls
    private static func stableID(_ seed: String) -> UUID {
        let hash = seed.utf8.reduce(0) { $0 &+ UInt64($1) &* 31 }
        let bytes = withUnsafeBytes(of: hash) { Array($0) }
        var uuidBytes: [UInt8] = Array(repeating: 0, count: 16)
        for i in 0..<min(bytes.count, 16) { uuidBytes[i] = bytes[i] }
        uuidBytes[6] = (uuidBytes[6] & 0x0F) | 0x40  // version 4
        uuidBytes[8] = (uuidBytes[8] & 0x3F) | 0x80  // variant 1
        return UUID(uuid: (
            uuidBytes[0], uuidBytes[1], uuidBytes[2], uuidBytes[3],
            uuidBytes[4], uuidBytes[5], uuidBytes[6], uuidBytes[7],
            uuidBytes[8], uuidBytes[9], uuidBytes[10], uuidBytes[11],
            uuidBytes[12], uuidBytes[13], uuidBytes[14], uuidBytes[15]
        ))
    }
}
