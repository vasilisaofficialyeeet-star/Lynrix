// StrategyPerformanceView.swift — Glassmorphism 2026 Strategy Performance (Lynrix v2.5)

import SwiftUI
import Charts

struct StrategyPerformanceView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                healthBanner
                
                HStack(spacing: 12) {
                    keyMetricsCard
                    riskMetricsCard
                }
                
                HStack(spacing: 12) {
                    tradeStatsCard
                    healthComponentsCard
                }
                
                pnlChartCard
                rlOptimizerCard
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    private var healthBanner: some View {
        let h = engine.strategyHealth
        let hColor = h.healthScore > 0.7 ? LxColor.neonLime : (h.healthScore > 0.4 ? LxColor.gold : LxColor.bloodRed)
        return GlassPanel(neon: hColor) {
            HStack {
                StatusDot(hColor)
                GlowText(loc.t(h.levelLocKey), font: LxFont.mono(14, weight: .bold), color: hColor, glow: 4)
                Spacer()
                GlassMetric(loc.t("common.score"), value: String(format: "%.2f", h.healthScore), color: hColor)
                GlassMetric(loc.t("common.activity"), value: String(format: "%.0f%%", h.activityScale), 
                            color: h.activityScale < 0.5 ? LxColor.bloodRed : LxColor.neonLime)
                if h.accuracyDeclining {
                    StatusBadge(loc.t("strategy.accDeclining"), color: LxColor.amber)
                }
                if h.drawdownWarning {
                    StatusBadge(loc.t("strategy.ddWarning"), color: LxColor.bloodRed, pulse: true)
                }
            }
        }
    }
    
    private var keyMetricsCard: some View {
        let sm = engine.strategyMetrics
        return GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("strategy.keyMetrics"), icon: "chart.bar.fill", color: LxColor.neonLime)
                GlassMetric(loc.t("strategy.sharpe"), value: String(format: "%.2f", sm.sharpeRatio),
                            color: sm.sharpeRatio > 1 ? LxColor.neonLime : (sm.sharpeRatio < 0 ? LxColor.magentaPink : theme.textPrimary))
                GlassMetric(loc.t("strategy.sortino"), value: String(format: "%.2f", sm.sortinoRatio),
                            color: sm.sortinoRatio > 1 ? LxColor.neonLime : theme.textPrimary)
                GlassMetric(loc.t("strategy.profitFactor"), value: String(format: "%.2f", sm.profitFactor),
                            color: sm.profitFactor > 1.5 ? LxColor.neonLime : (sm.profitFactor < 1 ? LxColor.magentaPink : theme.textPrimary))
                GlassMetric(loc.t("strategy.winRate"), value: String(format: "%.1f%%", sm.winRate * 100),
                            color: sm.winRate > 0.55 ? LxColor.neonLime : (sm.winRate < 0.45 ? LxColor.magentaPink : theme.textPrimary))
                GlassMetric(loc.t("strategy.expectancy"), value: String(format: "$%.4f", sm.expectancy),
                            color: sm.expectancy > 0 ? LxColor.neonLime : LxColor.magentaPink)
                GlassMetric(loc.t("strategy.totalPnl"), value: String(format: "$%.4f", sm.totalPnl),
                            color: sm.totalPnl > 0 ? LxColor.neonLime : LxColor.magentaPink)
                GlassMetric(loc.t("strategy.calmar"), value: String(format: "%.2f", sm.calmarRatio), color: theme.textPrimary)
                GlassMetric(loc.t("strategy.recovery"), value: String(format: "%.2f", sm.recoveryFactor), color: theme.textPrimary)
            }
        }
    }
    
    private var riskMetricsCard: some View {
        let sm = engine.strategyMetrics
        return GlassPanel(neon: LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("strategy.riskMetrics"), icon: "shield.fill", color: LxColor.magentaPink)
                GlassMetric(loc.t("strategy.maxDrawdown"), value: String(format: "%.2f%%", sm.maxDrawdownPct * 100),
                            color: sm.maxDrawdownPct > 0.05 ? LxColor.bloodRed : theme.textPrimary)
                GlassMetric(loc.t("strategy.currentDD"), value: String(format: "%.2f%%", sm.currentDrawdown * 100),
                            color: sm.currentDrawdown > 0.03 ? LxColor.amber : theme.textPrimary)
                GlassMetric(loc.t("strategy.bestTrade"), value: String(format: "$%.4f", sm.bestTrade), color: LxColor.neonLime)
                GlassMetric(loc.t("strategy.worstTrade"), value: String(format: "$%.4f", sm.worstTrade), color: LxColor.magentaPink)
                GlassMetric(loc.t("strategy.avgWin"), value: String(format: "$%.4f", sm.avgWin), color: LxColor.neonLime)
                GlassMetric(loc.t("strategy.avgLoss"), value: String(format: "$%.4f", sm.avgLoss), color: LxColor.magentaPink)
                GlassMetric(loc.t("strategy.dailyPnl"), value: String(format: "$%.4f", sm.dailyPnl),
                            color: sm.dailyPnl > 0 ? LxColor.neonLime : LxColor.magentaPink)
                GlassMetric(loc.t("strategy.hourlyPnl"), value: String(format: "$%.6f", sm.hourlyPnl),
                            color: sm.hourlyPnl > 0 ? LxColor.neonLime : LxColor.magentaPink)
            }
        }
    }
    
    private var tradeStatsCard: some View {
        let sm = engine.strategyMetrics
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("strategy.tradeStats"), icon: "arrow.left.arrow.right", color: LxColor.electricCyan)
                GlassMetric(loc.t("strategy.totalTrades"), value: "\(sm.totalTrades)", color: theme.textPrimary)
                GlassMetric(loc.t("strategy.winning"), value: "\(sm.winningTrades)", color: LxColor.neonLime)
                GlassMetric(loc.t("strategy.losing"), value: "\(sm.losingTrades)", color: LxColor.magentaPink)
                GlassMetric(loc.t("strategy.consecWins"), value: "\(sm.consecutiveWins) (max \(sm.maxConsecutiveWins))", color: LxColor.neonLime)
                GlassMetric(loc.t("dashboard.consecLosses"), value: "\(sm.consecutiveLosses) (max \(sm.maxConsecutiveLosses))", color: LxColor.magentaPink)
            }
        }
    }
    
    private var healthComponentsCard: some View {
        let h = engine.strategyHealth
        return GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("strategy.healthComponents"), icon: "heart.fill", color: LxColor.gold)
                neonHealthBar(loc.t("strategy.accuracy"), score: h.accuracyScore, declining: h.accuracyDeclining)
                neonHealthBar(loc.t("strategy.pnl"), score: h.pnlScore, declining: h.pnlDeclining)
                neonHealthBar(loc.t("strategy.drawdownComponent"), score: h.drawdownScore, declining: false)
                neonHealthBar(loc.t("strategy.sharpeScore"), score: h.sharpeScore, declining: false)
                neonHealthBar(loc.t("strategy.consistency"), score: h.consistencyScore, declining: false)
                neonHealthBar(loc.t("strategy.fillRate"), score: h.fillRateScore, declining: false)
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                GlassMetric(loc.t("strategy.regimeChanges"), value: "\(h.regimeChanges1h)",
                            color: h.regimeChanges1h > 5 ? LxColor.amber : theme.textPrimary)
                GlassMetric(loc.t("strategy.thresholdOffset"), value: String(format: "+%.3f", h.thresholdOffset), color: theme.textPrimary)
            }
        }
    }
    
    private var pnlChartCard: some View {
        GlassPanel(neon: engine.strategyMetrics.totalPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("strategy.pnlHistory"), icon: "chart.xyaxis.line",
                                       color: engine.strategyMetrics.totalPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink)
                    Spacer()
                    SignedNumber(engine.strategyMetrics.totalPnl, format: "%+.4f $", font: LxFont.metric)
                }
                AnimatedEquityCurve(
                    data: engine.pnlHistory,
                    positive: LxColor.neonLime,
                    negative: LxColor.magentaPink,
                    height: 100
                )
            }
        }
    }
    
    private var rlOptimizerCard: some View {
        let rl = engine.rlState
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("strategy.rlOptimizer") + " (PPO)", icon: "brain", color: LxColor.electricCyan)
                    Spacer()
                    StatusBadge(rl.exploring ? loc.t("strategy.exploring") : loc.t("strategy.exploiting"),
                               color: rl.exploring ? LxColor.electricCyan : LxColor.neonLime)
                }
                HStack(spacing: 20) {
                    VStack(alignment: .leading, spacing: 4) {
                        GlassMetric(loc.t("strategy.steps"), value: "\(rl.totalSteps)", color: theme.textPrimary)
                        GlassMetric(loc.t("strategy.updates"), value: "\(rl.totalUpdates)", color: theme.textPrimary)
                        GlassMetric(loc.t("rl.avgReward"), value: String(format: "%.4f", rl.avgReward),
                                    color: rl.avgReward > 0 ? LxColor.neonLime : LxColor.magentaPink)
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        GlassMetric(loc.t("strategy.deltaThreshold"), value: String(format: "%+.4f", rl.signalThresholdDelta), color: LxColor.electricCyan)
                        GlassMetric(loc.t("strategy.sizeScale"), value: String(format: "%.2fx", rl.positionSizeScale), color: LxColor.neonLime)
                        GlassMetric(loc.t("strategy.offsetBPS"), value: String(format: "%+.2f", rl.orderOffsetBps), color: LxColor.amber)
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        GlassMetric(loc.t("rl.policyLoss"), value: String(format: "%.4f", rl.policyLoss), color: theme.textPrimary)
                        GlassMetric(loc.t("rl.valueLoss"), value: String(format: "%.4f", rl.valueLoss), color: theme.textPrimary)
                        GlassMetric(loc.t("strategy.valueEst"), value: String(format: "%.4f", rl.valueEstimate), color: LxColor.electricCyan)
                    }
                }
            }
        }
    }
    
    // MARK: - Helpers
    
    private func neonHealthBar(_ label: String, score: Double, declining: Bool) -> some View {
        let barCol = score > 0.7 ? LxColor.neonLime : (score > 0.4 ? LxColor.gold : LxColor.bloodRed)
        return HStack(spacing: 6) {
            Text(label)
                .font(LxFont.mono(10))
                .foregroundColor(theme.textTertiary)
                .frame(width: 80, alignment: .leading)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 4)
                        .fill(barCol.opacity(0.08))
                        .frame(height: 8)
                    RoundedRectangle(cornerRadius: 4)
                        .fill(
                            LinearGradient(colors: [barCol.opacity(0.5), barCol.opacity(0.2)],
                                           startPoint: .leading, endPoint: .trailing)
                        )
                        .frame(width: max(geo.size.width * score, 2), height: 8)
                        .shadow(color: barCol.opacity(0.2), radius: 2)
                }
            }
            .frame(height: 8)
            Text(String(format: "%.0f%%", score * 100))
                .font(LxFont.mono(9))
                .foregroundColor(barCol)
                .frame(width: 32, alignment: .trailing)
            if declining {
                Image(systemName: "arrow.down")
                    .font(.system(size: 8))
                    .foregroundColor(LxColor.amber)
            }
        }
    }
}
