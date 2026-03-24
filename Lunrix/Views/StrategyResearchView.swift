// StrategyResearchView.swift — Strategy Research & Analysis (Lynrix v2.5 Sprint 5)
// Multi-horizon comparison, regime-performance correlation, risk decomposition, execution analysis.

import SwiftUI

struct StrategyResearchView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                summaryCard
                HStack(spacing: 12) {
                    horizonComparisonCard
                    riskDecompositionCard
                }
                HStack(spacing: 12) {
                    executionAnalysisCard
                    regimeCorrelationCard
                }
                tradeDistributionCard
                healthDecompositionCard
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }

    // MARK: - Summary

    private var summaryCard: some View {
        let sm = engine.strategyMetrics
        let h = engine.strategyHealth
        let hColor = h.healthScore > 0.7 ? LxColor.neonLime : (h.healthScore > 0.4 ? LxColor.gold : LxColor.bloodRed)
        return GlassPanel(neon: hColor, padding: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 8) {
                        StatusDot(hColor)
                        GlowText(loc.t(h.levelLocKey), font: LxFont.mono(14, weight: .bold), color: hColor, glow: 4)
                        Text("•")
                            .foregroundColor(theme.textTertiary)
                        Text(loc.t(LxColor.regimeLocKey(engine.regime.current)))
                            .font(LxFont.mono(11, weight: .bold))
                            .foregroundColor(LxColor.regime(engine.regime.current))
                    }
                    HStack(spacing: 14) {
                        kpi(loc.t("strategy.sharpe"), String(format: "%.2f", sm.sharpeRatio),
                            sm.sharpeRatio > 1 ? LxColor.neonLime : (sm.sharpeRatio < 0 ? LxColor.magentaPink : theme.textPrimary))
                        kpi(loc.t("strategy.winRate"), String(format: "%.1f%%", sm.winRate * 100),
                            sm.winRate > 0.55 ? LxColor.neonLime : LxColor.amber)
                        kpi(loc.t("strategy.totalPnl"), String(format: "$%.4f", sm.totalPnl),
                            sm.totalPnl > 0 ? LxColor.neonLime : LxColor.magentaPink)
                        kpi(loc.t("strategy.maxDD"), String(format: "%.2f%%", sm.maxDrawdownPct * 100), LxColor.magentaPink)
                    }
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 4) {
                    Text("\(sm.totalTrades)")
                        .font(LxFont.mono(24, weight: .bold))
                        .foregroundColor(theme.textPrimary)
                    Text(loc.t("research.totalTrades"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
            }
        }
    }

    private func kpi(_ label: String, _ value: String, _ color: Color) -> some View {
        VStack(spacing: 2) {
            Text(label)
                .font(LxFont.mono(8, weight: .bold))
                .foregroundColor(theme.textTertiary)
            Text(value)
                .font(LxFont.mono(11, weight: .bold))
                .foregroundColor(color)
        }
    }

    // MARK: - Horizon Comparison

    private var horizonComparisonCard: some View {
        let acc = engine.accuracy
        let horizons: [(String, Double, String)] = [
            ("100ms", acc.horizonAccuracy100ms, "research.horizon100ms"),
            ("500ms", acc.horizonAccuracy500ms, "research.horizon500ms"),
            ("1s", acc.horizonAccuracy1s, "research.horizon1s"),
            ("3s", acc.horizonAccuracy3s, "research.horizon3s")
        ]
        let best = horizons.max(by: { $0.1 < $1.1 })?.0 ?? "—"
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("research.horizonComparison"), icon: "clock.arrow.2.circlepath", color: LxColor.electricCyan)
                ForEach(horizons, id: \.0) { h in
                    let isBest = h.0 == best
                    HStack(spacing: 6) {
                        Text(h.0)
                            .font(LxFont.mono(10, weight: isBest ? .bold : .regular))
                            .foregroundColor(isBest ? LxColor.electricCyan : theme.textSecondary)
                            .frame(width: 40, alignment: .trailing)
                        GeometryReader { geo in
                            ZStack(alignment: .leading) {
                                RoundedRectangle(cornerRadius: 2).fill(theme.panelBackground).frame(height: 8)
                                RoundedRectangle(cornerRadius: 2)
                                    .fill(isBest ? LxColor.electricCyan : (h.1 > 0.5 ? LxColor.neonLime : LxColor.amber))
                                    .frame(width: max(0, geo.size.width * CGFloat(h.1)), height: 8)
                            }
                        }
                        .frame(height: 8)
                        Text(String(format: "%.1f%%", h.1 * 100))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(isBest ? LxColor.electricCyan : theme.textPrimary)
                            .frame(width: 40, alignment: .trailing)
                    }
                }
                HStack {
                    Text(loc.t("research.bestHorizon"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    Spacer()
                    Text(best)
                        .font(LxFont.mono(11, weight: .bold))
                        .foregroundColor(LxColor.electricCyan)
                }
            }
        }
    }

    // MARK: - Risk Decomposition

    private var riskDecompositionCard: some View {
        let sm = engine.strategyMetrics
        let v = engine.varState
        return GlassPanel(neon: LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("research.riskDecomp"), icon: "shield.lefthalf.filled", color: LxColor.magentaPink)
                GlassMetric(loc.t("strategy.maxDD"), value: String(format: "%.2f%%", sm.maxDrawdownPct * 100), color: LxColor.magentaPink)
                GlassMetric(loc.t("research.currentDD"), value: String(format: "%.2f%%", sm.currentDrawdown * 100),
                            color: sm.currentDrawdown > 0.05 ? LxColor.bloodRed : LxColor.amber)
                GlassMetric(loc.t("research.var95"), value: String(format: "$%.2f", v.var95), color: LxColor.magentaPink)
                GlassMetric(loc.t("research.var99"), value: String(format: "$%.2f", v.var99), color: LxColor.bloodRed)
                GlassMetric(loc.t("research.cvar95"), value: String(format: "$%.2f", v.cvar95), color: LxColor.magentaPink)
                GlassMetric(loc.t("strategy.calmar"), value: String(format: "%.2f", sm.calmarRatio),
                            color: sm.calmarRatio > 1 ? LxColor.neonLime : theme.textPrimary)
                GlassMetric(loc.t("strategy.recovery"), value: String(format: "%.2f", sm.recoveryFactor), color: theme.textPrimary)
            }
        }
    }

    // MARK: - Execution Analysis

    private var executionAnalysisCard: some View {
        let ea = engine.executionAnalytics
        let sc = ea.score
        let gradeColor = sc.overall > 70 ? LxColor.neonLime : (sc.overall > 40 ? LxColor.gold : LxColor.bloodRed)
        return GlassPanel(neon: gradeColor) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("research.execution"), icon: "chart.bar.doc.horizontal", color: gradeColor)
                HStack {
                    Text(loc.t("research.execScore"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    Spacer()
                    Text(String(format: "%.0f", sc.overall))
                        .font(LxFont.mono(18, weight: .bold))
                        .foregroundColor(gradeColor)
                }
                execBar(loc.t("research.fillRate"), sc.fillRateComponent)
                execBar(loc.t("research.slippage"), sc.slippageComponent)
                execBar(loc.t("research.latency"), sc.latencyComponent)
                execBar(loc.t("research.impact"), sc.impactComponent)
                execBar(loc.t("research.signalEff"), sc.efficiencyComponent)
            }
        }
    }

    private func execBar(_ label: String, _ score: Double) -> some View {
        let color: Color = score > 70 ? LxColor.neonLime : (score > 40 ? LxColor.gold : LxColor.bloodRed)
        return HStack(spacing: 6) {
            Text(label)
                .font(LxFont.mono(9))
                .foregroundColor(theme.textSecondary)
                .frame(width: 60, alignment: .trailing)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2).fill(theme.panelBackground).frame(height: 6)
                    RoundedRectangle(cornerRadius: 2).fill(color)
                        .frame(width: max(0, geo.size.width * CGFloat(score / 100)), height: 6)
                }
            }
            .frame(height: 6)
            Text(String(format: "%.0f", score))
                .font(LxFont.mono(8, weight: .bold))
                .foregroundColor(color)
                .frame(width: 24, alignment: .trailing)
        }
    }

    // MARK: - Regime Correlation

    private var regimeCorrelationCard: some View {
        let trades = TradeJournalStore.shared.trades
        let regimeGroups = Dictionary(grouping: trades, by: { $0.riskAtEntry.regime })
        return GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("research.regimeCorrelation"), icon: "waveform.path.ecg", color: LxColor.gold)
                if regimeGroups.isEmpty {
                    Text(loc.t("research.noTradeData"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                } else {
                    HStack(spacing: 4) {
                        Text(loc.t("research.regime"))
                            .font(LxFont.mono(8, weight: .bold))
                            .foregroundColor(theme.textTertiary)
                            .frame(width: 70, alignment: .leading)
                        Text(loc.t("research.trades"))
                            .font(LxFont.mono(8, weight: .bold))
                            .foregroundColor(theme.textTertiary)
                            .frame(width: 30)
                        Text(loc.t("research.winRateShort"))
                            .font(LxFont.mono(8, weight: .bold))
                            .foregroundColor(theme.textTertiary)
                            .frame(width: 36)
                        Text(loc.t("research.avgPnl"))
                            .font(LxFont.mono(8, weight: .bold))
                            .foregroundColor(theme.textTertiary)
                            .frame(width: 60, alignment: .trailing)
                        Spacer()
                        Text(loc.t("research.totalPnlShort"))
                            .font(LxFont.mono(8, weight: .bold))
                            .foregroundColor(theme.textTertiary)
                            .frame(width: 60, alignment: .trailing)
                    }
                    ForEach(regimeGroups.keys.sorted(), id: \.self) { regime in
                        let rTrades = regimeGroups[regime] ?? []
                        let wins = rTrades.filter { $0.outcome == .win }.count
                        let wr = rTrades.isEmpty ? 0.0 : Double(wins) / Double(rTrades.count)
                        let totalPnl = rTrades.reduce(0.0) { $0 + $1.realizedPnl }
                        let avgPnl = rTrades.isEmpty ? 0.0 : totalPnl / Double(rTrades.count)
                        HStack(spacing: 4) {
                            Circle().fill(LxColor.regime(regime)).frame(width: 6, height: 6)
                            Text(loc.t(LxColor.regimeLocKey(regime)))
                                .font(LxFont.mono(9))
                                .foregroundColor(theme.textSecondary)
                                .frame(width: 62, alignment: .leading)
                            Text("\(rTrades.count)")
                                .font(LxFont.mono(9))
                                .foregroundColor(theme.textTertiary)
                                .frame(width: 30)
                            Text(String(format: "%.0f%%", wr * 100))
                                .font(LxFont.mono(9, weight: .bold))
                                .foregroundColor(wr > 0.5 ? LxColor.neonLime : LxColor.magentaPink)
                                .frame(width: 36)
                            Text(String(format: "%+.5f", avgPnl))
                                .font(LxFont.mono(9))
                                .foregroundColor(avgPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink)
                                .frame(width: 60, alignment: .trailing)
                            Spacer()
                            Text(String(format: "%+.4f", totalPnl))
                                .font(LxFont.mono(9, weight: .bold))
                                .foregroundColor(totalPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink)
                                .frame(width: 60, alignment: .trailing)
                        }
                    }
                }
            }
        }
    }

    // MARK: - Trade Distribution

    private var tradeDistributionCard: some View {
        let sm = engine.strategyMetrics
        return GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("research.tradeDistribution"), icon: "chart.bar.fill", color: LxColor.neonLime)
                HStack(spacing: 12) {
                    distStat(loc.t("research.winning"), "\(sm.winningTrades)", LxColor.neonLime)
                    distStat(loc.t("research.losing"), "\(sm.losingTrades)", LxColor.magentaPink)
                    distStat(loc.t("research.consWins"), "\(sm.maxConsecutiveWins)", LxColor.neonLime)
                    distStat(loc.t("research.consLosses"), "\(sm.maxConsecutiveLosses)", LxColor.magentaPink)
                    distStat(loc.t("research.bestTrade"), String(format: "$%.4f", sm.bestTrade), LxColor.neonLime)
                    distStat(loc.t("research.worstTrade"), String(format: "$%.4f", sm.worstTrade), LxColor.magentaPink)
                }
                if sm.totalTrades > 0 {
                    HStack(spacing: 0) {
                        let winPct = CGFloat(sm.winningTrades) / CGFloat(sm.totalTrades)
                        RoundedRectangle(cornerRadius: 3)
                            .fill(LxColor.neonLime.opacity(0.6))
                            .frame(width: max(0, 300 * winPct), height: 10)
                        RoundedRectangle(cornerRadius: 3)
                            .fill(LxColor.magentaPink.opacity(0.6))
                            .frame(height: 10)
                    }
                    .frame(maxWidth: .infinity)
                    .clipShape(RoundedRectangle(cornerRadius: 3))
                }
            }
        }
    }

    private func distStat(_ label: String, _ value: String, _ color: Color) -> some View {
        VStack(spacing: 2) {
            Text(label)
                .font(LxFont.mono(8, weight: .bold))
                .foregroundColor(theme.textTertiary)
            Text(value)
                .font(LxFont.mono(10, weight: .bold))
                .foregroundColor(color)
        }
    }

    // MARK: - Health Decomposition

    private var healthDecompositionCard: some View {
        let h = engine.strategyHealth
        let components: [(String, Double)] = [
            (loc.t("research.accScore"), h.accuracyScore * 100),
            (loc.t("research.pnlScore"), h.pnlScore * 100),
            (loc.t("research.ddScore"), h.drawdownScore * 100),
            (loc.t("research.sharpeScore"), h.sharpeScore * 100),
            (loc.t("research.consistencyScore"), h.consistencyScore * 100),
            (loc.t("research.fillRateScore"), h.fillRateScore * 100)
        ]
        return GlassPanel(neon: LxColor.coolSteel) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("research.healthDecomp"), icon: "heart.text.square", color: LxColor.coolSteel)
                ForEach(components, id: \.0) { item in
                    HStack(spacing: 6) {
                        Text(item.0)
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textSecondary)
                            .frame(width: 80, alignment: .trailing)
                        GeometryReader { geo in
                            ZStack(alignment: .leading) {
                                RoundedRectangle(cornerRadius: 2).fill(theme.panelBackground).frame(height: 6)
                                RoundedRectangle(cornerRadius: 2)
                                    .fill(item.1 > 60 ? LxColor.neonLime : (item.1 > 30 ? LxColor.gold : LxColor.bloodRed))
                                    .frame(width: max(0, geo.size.width * CGFloat(item.1 / 100)), height: 6)
                            }
                        }
                        .frame(height: 6)
                        Text(String(format: "%.0f", item.1))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(item.1 > 60 ? LxColor.neonLime : (item.1 > 30 ? LxColor.gold : LxColor.bloodRed))
                            .frame(width: 28, alignment: .trailing)
                    }
                }
            }
        }
    }
}
