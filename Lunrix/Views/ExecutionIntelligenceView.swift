// ExecutionIntelligenceView.swift — Execution Intelligence Center (Lynrix v2.5 Sprint 3)
// Premium analytical dashboard: execution score, slippage, fill quality,
// latency decomposition, poor executions, and actionable insights.

import SwiftUI

struct ExecutionIntelligenceView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @ObservedObject private var layout = WidgetLayoutManager.shared
    
    private func w(_ key: String) -> Bool {
        layout.isVisible(WidgetID(screen: .executionIntel, key: key))
    }

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                HStack {
                    Spacer()
                    WidgetVisibilityMenu(screen: .executionIntel)
                }
                .padding(.bottom, -8)
                
                eicHeader
                if engine.executionAnalytics.hasData {
                    if w("kpiSummary") { kpiSummaryRow }
                    HStack(alignment: .top, spacing: 14) {
                        VStack(spacing: 14) {
                            if w("execScore") { executionScoreCard }
                            if w("slippage") { slippageAnalysisCard }
                        }
                        .frame(maxWidth: .infinity)
                        VStack(spacing: 14) {
                            if w("fillQuality") { fillQualityCard }
                            if w("latencyDecomp") { latencyDecompositionCard }
                        }
                        .frame(maxWidth: .infinity)
                    }
                    if w("poorExecs") && !engine.executionAnalytics.poorExecutions.isEmpty {
                        poorExecutionsCard
                    }
                    if w("insights") && !engine.executionAnalytics.insights.isEmpty {
                        insightsCard
                    }
                } else {
                    emptyStateCard
                }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }

    // MARK: - Header

    private var eicHeader: some View {
        GlassPanel(neon: LxColor.gold) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Image(systemName: "chart.bar.doc.horizontal")
                            .font(.system(size: 18, weight: .bold))
                            .foregroundColor(LxColor.gold)
                            .shadow(color: LxColor.gold.opacity(0.5), radius: 6)
                        Text(loc.t("exec.title"))
                            .font(LxFont.mono(16, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                    }
                    Text(loc.t("exec.subtitle"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
                if engine.executionAnalytics.hasData {
                    scoreGradeBadge(engine.executionAnalytics.score)
                }
            }
        }
    }

    // MARK: - Score Badge

    private func scoreGradeBadge(_ score: ExecutionScore) -> some View {
        HStack(spacing: 6) {
            Text(score.grade)
                .font(LxFont.mono(22, weight: .black))
                .foregroundColor(score.gradeColor)
                .shadow(color: score.gradeColor.opacity(0.5 * theme.glowOpacity), radius: 6 * theme.glowIntensity)
            VStack(alignment: .leading, spacing: 0) {
                Text(String(format: "%.0f", score.overall))
                    .font(LxFont.mono(13, weight: .bold))
                    .foregroundColor(theme.textPrimary)
                Text("/100")
                    .font(LxFont.mono(9))
                    .foregroundColor(theme.textTertiary)
            }
        }
    }

    // MARK: - KPI Summary Row

    private var kpiSummaryRow: some View {
        let a = engine.executionAnalytics
        return HStack(spacing: 10) {
            NeonMetricCard(
                loc.t("exec.kpi.fillRate"),
                value: String(format: "%.1f%%", a.fillQuality.fillRate * 100),
                color: a.fillQuality.fillRateGrade.color
            )
            NeonMetricCard(
                loc.t("exec.kpi.avgSlippage"),
                value: String(format: "%.2f bps", a.slippage.avgSlippageBps),
                color: a.slippage.grade.color
            )
            NeonMetricCard(
                loc.t("exec.kpi.fillTime"),
                value: String(format: "%.0f µs", a.fillQuality.avgFillTimeUs),
                color: a.fillQuality.avgFillTimeUs > 1000 ? LxColor.amber : LxColor.neonLime
            )
            NeonMetricCard(
                loc.t("exec.kpi.impact"),
                value: String(format: "%.2f bps", a.slippage.marketImpactBps),
                color: a.slippage.marketImpactBps > 3 ? LxColor.amber : LxColor.neonLime
            )
            NeonMetricCard(
                loc.t("exec.kpi.e2eP99"),
                value: String(format: "%.0f µs", a.latency.totalP99Us),
                color: a.latency.totalP99Us > 500 ? LxColor.bloodRed : LxColor.electricCyan
            )
        }
    }

    // MARK: - Execution Score Card

    private var executionScoreCard: some View {
        let s = engine.executionAnalytics.score
        return GlassPanel(neon: s.gradeColor) {
            VStack(alignment: .leading, spacing: 10) {
                GlassSectionHeader(loc.t("exec.score"), icon: "star.fill", color: s.gradeColor)

                HStack(spacing: 16) {
                    // Score dial
                    ZStack {
                        Circle()
                            .stroke(theme.divider, lineWidth: 6)
                            .frame(width: 72, height: 72)
                        Circle()
                            .trim(from: 0, to: CGFloat(s.overall / 100.0))
                            .stroke(s.gradeColor, style: StrokeStyle(lineWidth: 6, lineCap: .round))
                            .frame(width: 72, height: 72)
                            .rotationEffect(.degrees(-90))
                            .shadow(color: s.gradeColor.opacity(0.3 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
                        VStack(spacing: 0) {
                            Text(s.grade)
                                .font(LxFont.mono(18, weight: .black))
                                .foregroundColor(s.gradeColor)
                            Text(String(format: "%.0f", s.overall))
                                .font(LxFont.mono(10))
                                .foregroundColor(theme.textSecondary)
                        }
                    }

                    // Component breakdown
                    VStack(alignment: .leading, spacing: 5) {
                        scoreRow(loc.t("exec.comp.fillRate"), value: s.fillRateComponent, weight: "30%")
                        scoreRow(loc.t("exec.comp.slippage"), value: s.slippageComponent, weight: "25%")
                        scoreRow(loc.t("exec.comp.latency"), value: s.latencyComponent, weight: "20%")
                        scoreRow(loc.t("exec.comp.impact"), value: s.impactComponent, weight: "15%")
                        scoreRow(loc.t("exec.comp.efficiency"), value: s.efficiencyComponent, weight: "10%")
                    }
                }
            }
        }
    }

    private func scoreRow(_ label: String, value: Double, weight: String) -> some View {
        HStack(spacing: 6) {
            Text(label)
                .font(LxFont.mono(9))
                .foregroundColor(theme.textSecondary)
                .frame(width: 70, alignment: .leading)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2)
                        .fill(theme.divider)
                        .frame(height: 4)
                    RoundedRectangle(cornerRadius: 2)
                        .fill(colorForScore(value))
                        .frame(width: max(0, geo.size.width * CGFloat(value / 100.0)), height: 4)
                }
            }
            .frame(height: 4)
            Text(String(format: "%.0f", value))
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(colorForScore(value))
                .frame(width: 22, alignment: .trailing)
            Text(weight)
                .font(LxFont.mono(7))
                .foregroundColor(theme.textTertiary)
                .frame(width: 22)
        }
    }

    private func colorForScore(_ v: Double) -> Color {
        if v >= 80 { return LxColor.neonLime }
        if v >= 60 { return LxColor.electricCyan }
        if v >= 40 { return LxColor.amber }
        return LxColor.bloodRed
    }

    // MARK: - Slippage Analysis Card

    private var slippageAnalysisCard: some View {
        let slip = engine.executionAnalytics.slippage
        return GlassPanel(neon: slip.grade.color) {
            VStack(alignment: .leading, spacing: 10) {
                GlassSectionHeader(loc.t("exec.slippage"), icon: "arrow.up.arrow.down", color: slip.grade.color)

                HStack(spacing: 20) {
                    VStack(spacing: 4) {
                        Text(String(format: "%.2f", slip.avgSlippageBps))
                            .font(LxFont.mono(20, weight: .bold))
                            .foregroundColor(slip.grade.color)
                            .shadow(color: slip.grade.color.opacity(0.3 * theme.glowOpacity), radius: 3 * theme.glowIntensity)
                        Text(loc.t("exec.bpsAvg"))
                            .font(LxFont.mono(8))
                            .foregroundColor(theme.textTertiary)
                    }

                    VStack(alignment: .leading, spacing: 4) {
                        slippageMetricRow(loc.t("exec.slip.impact"), String(format: "%.2f bps", slip.marketImpactBps))
                        slippageMetricRow(loc.t("exec.slip.buy"), String(format: "%.2f bps", slip.buyAvgSlippage))
                        slippageMetricRow(loc.t("exec.slip.sell"), String(format: "%.2f bps", slip.sellAvgSlippage))
                    }
                }

                // Recent order slippage bars
                if !slip.recentOrderSlippages.isEmpty {
                    VStack(alignment: .leading, spacing: 3) {
                        Text(loc.t("exec.slip.recent"))
                            .font(LxFont.mono(8, weight: .bold))
                            .foregroundColor(theme.textTertiary)
                        HStack(alignment: .bottom, spacing: 2) {
                            ForEach(Array(slip.recentOrderSlippages.suffix(20).enumerated()), id: \.offset) { _, val in
                                let grade = FillQualityGrade.from(slippageBps: val)
                                RoundedRectangle(cornerRadius: 1)
                                    .fill(grade.color)
                                    .frame(width: 6, height: max(3, CGFloat(min(val, 10)) * 3))
                            }
                        }
                        .frame(height: 30)
                    }
                }

                // Grade badge
                HStack(spacing: 4) {
                    Image(systemName: slip.grade.icon)
                        .font(.system(size: 10))
                    Text(loc.t(slip.grade.locKey))
                        .font(LxFont.mono(9, weight: .bold))
                }
                .foregroundColor(slip.grade.color)
            }
        }
    }

    private func slippageMetricRow(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label)
                .font(LxFont.mono(9))
                .foregroundColor(theme.textSecondary)
            Spacer()
            Text(value)
                .font(LxFont.mono(9, weight: .medium))
                .foregroundColor(theme.textPrimary)
        }
    }

    // MARK: - Fill Quality Card

    private var fillQualityCard: some View {
        let fq = engine.executionAnalytics.fillQuality
        return GlassPanel(neon: fq.fillRateGrade.color) {
            VStack(alignment: .leading, spacing: 10) {
                GlassSectionHeader(loc.t("exec.fillQuality"), icon: "checkmark.circle", color: fq.fillRateGrade.color)

                HStack(spacing: 20) {
                    // Fill rate dial
                    ZStack {
                        Circle()
                            .stroke(theme.divider, lineWidth: 5)
                            .frame(width: 56, height: 56)
                        Circle()
                            .trim(from: 0, to: CGFloat(min(fq.fillRate, 1.0)))
                            .stroke(fq.fillRateGrade.color, style: StrokeStyle(lineWidth: 5, lineCap: .round))
                            .frame(width: 56, height: 56)
                            .rotationEffect(.degrees(-90))
                            .shadow(color: fq.fillRateGrade.color.opacity(0.3 * theme.glowOpacity), radius: 3 * theme.glowIntensity)
                        Text(String(format: "%.0f%%", fq.fillRate * 100))
                            .font(LxFont.mono(12, weight: .bold))
                            .foregroundColor(fq.fillRateGrade.color)
                    }

                    VStack(alignment: .leading, spacing: 5) {
                        fillMetricRow(loc.t("exec.fill.sent"), "\(fq.totalSentOrders)")
                        fillMetricRow(loc.t("exec.fill.filled"), "\(fq.totalFilledOrders)")
                        fillMetricRow(loc.t("exec.fill.cancelled"), "\(fq.totalCancelledOrders)")
                        fillMetricRow(loc.t("exec.fill.cancelRate"), String(format: "%.1f%%", fq.cancelRate * 100))
                        fillMetricRow(loc.t("exec.fill.signalEff"), String(format: "%.1f%%", fq.signalEfficiency * 100))
                        fillMetricRow(loc.t("exec.fill.avgTime"), String(format: "%.0f µs", fq.avgFillTimeUs))
                    }
                }

                if fq.partialFillCount > 0 {
                    HStack(spacing: 4) {
                        Image(systemName: "chart.pie")
                            .font(.system(size: 9))
                        Text(String(format: loc.t("exec.fill.partials"), fq.partialFillCount))
                            .font(LxFont.mono(9))
                    }
                    .foregroundColor(LxColor.amber)
                }
            }
        }
    }

    private func fillMetricRow(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label)
                .font(LxFont.mono(9))
                .foregroundColor(theme.textSecondary)
            Spacer()
            Text(value)
                .font(LxFont.mono(9, weight: .medium))
                .foregroundColor(theme.textPrimary)
        }
    }

    // MARK: - Latency Decomposition Card

    private var latencyDecompositionCard: some View {
        let lat = engine.executionAnalytics.latency
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    GlassSectionHeader(loc.t("exec.latency"), icon: "clock.arrow.2.circlepath", color: LxColor.electricCyan)
                    Spacer()
                    VStack(alignment: .trailing, spacing: 0) {
                        Text(String(format: "p50: %.0f µs", lat.totalP50Us))
                            .font(LxFont.mono(8))
                            .foregroundColor(theme.textSecondary)
                        Text(String(format: "p99: %.0f µs", lat.totalP99Us))
                            .font(LxFont.mono(8, weight: .bold))
                            .foregroundColor(lat.totalP99Us > 500 ? LxColor.bloodRed : LxColor.electricCyan)
                    }
                }

                if !lat.stages.isEmpty {
                    // Waterfall bars
                    ForEach(lat.stages) { stage in
                        latencyStageRow(stage, totalP50: lat.totalP50Us)
                    }

                    if lat.exchangeLatencyMs > 0 {
                        HStack(spacing: 6) {
                            Text(loc.t("exec.lat.exchange"))
                                .font(LxFont.mono(8))
                                .foregroundColor(theme.textSecondary)
                                .frame(width: 70, alignment: .leading)
                            Text(String(format: "%.1f ms", lat.exchangeLatencyMs))
                                .font(LxFont.mono(8, weight: .medium))
                                .foregroundColor(LxColor.amber)
                            Spacer()
                            Text(loc.t("exec.lat.external"))
                                .font(LxFont.mono(7))
                                .foregroundColor(theme.textTertiary)
                        }
                    }

                    if !lat.bottleneckStage.isEmpty {
                        HStack(spacing: 4) {
                            Image(systemName: "exclamationmark.triangle")
                                .font(.system(size: 9))
                            Text(String(format: loc.t("exec.lat.bottleneck"), lat.bottleneckStage))
                                .font(LxFont.mono(8))
                        }
                        .foregroundColor(LxColor.amber)
                    }
                } else {
                    Text(loc.t("exec.lat.noData"))
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textTertiary)
                }
            }
        }
    }

    private func latencyStageRow(_ stage: LatencyStage, totalP50: Double) -> some View {
        let barFraction = totalP50 > 0 ? min(stage.p50Us / totalP50, 1.0) : 0
        let barColor = stage.isBottleneck ? LxColor.amber : LxColor.electricCyan
        return HStack(spacing: 6) {
            Text(stage.name)
                .font(LxFont.mono(8))
                .foregroundColor(stage.isBottleneck ? LxColor.amber : theme.textSecondary)
                .frame(width: 70, alignment: .leading)
                .lineLimit(1)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2)
                        .fill(theme.divider)
                        .frame(height: 6)
                    RoundedRectangle(cornerRadius: 2)
                        .fill(barColor.opacity(0.7))
                        .frame(width: max(2, geo.size.width * CGFloat(barFraction)), height: 6)
                        .shadow(color: barColor.opacity(0.2 * theme.glowOpacity), radius: 2 * theme.glowIntensity)
                }
            }
            .frame(height: 6)
            Text(String(format: "%.0f", stage.p50Us))
                .font(LxFont.mono(8, weight: .medium))
                .foregroundColor(theme.textPrimary)
                .frame(width: 30, alignment: .trailing)
            Text(String(format: "%.0f%%", stage.contributionPct))
                .font(LxFont.mono(7))
                .foregroundColor(theme.textTertiary)
                .frame(width: 25, alignment: .trailing)
        }
        .frame(height: 12)
    }

    // MARK: - Poor Executions Card

    private var poorExecutionsCard: some View {
        GlassPanel(neon: LxColor.bloodRed) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("exec.poorExec"), icon: "exclamationmark.triangle", color: LxColor.bloodRed)

                ForEach(engine.executionAnalytics.poorExecutions.prefix(5)) { event in
                    HStack(spacing: 8) {
                        Image(systemName: event.grade.icon)
                            .font(.system(size: 10))
                            .foregroundColor(event.grade.color)
                        Text(event.isBuy ? "BUY" : "SELL")
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(event.isBuy ? LxColor.neonLime : LxColor.bloodRed)
                        Text(String(format: "%.1f", event.expectedPrice))
                            .font(LxFont.mono(9))
                            .foregroundColor(theme.textSecondary)
                        Image(systemName: "arrow.right")
                            .font(.system(size: 7))
                            .foregroundColor(theme.textTertiary)
                        Text(String(format: "%.1f", event.fillPrice))
                            .font(LxFont.mono(9))
                            .foregroundColor(theme.textPrimary)
                        Spacer()
                        Text(String(format: "%.2f bps", event.slippageBps))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(event.grade.color)
                        if event.fillCompleteness < 1.0 {
                            Text(String(format: "%.0f%%", event.fillCompleteness * 100))
                                .font(LxFont.mono(8))
                                .foregroundColor(LxColor.amber)
                        }
                    }
                    .padding(.vertical, 2)
                }
            }
        }
    }

    // MARK: - Insights Card

    private var insightsCard: some View {
        GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("exec.insights"), icon: "lightbulb.fill", color: LxColor.gold)

                ForEach(engine.executionAnalytics.insights) { insight in
                    HStack(alignment: .top, spacing: 8) {
                        Image(systemName: insight.severity.icon)
                            .font(.system(size: 11))
                            .foregroundColor(insight.severity.color)
                            .frame(width: 16)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(loc.t(insight.titleKey))
                                .font(LxFont.mono(10, weight: .medium))
                                .foregroundColor(theme.textPrimary)
                            Text(insight.detail)
                                .font(LxFont.mono(9))
                                .foregroundColor(theme.textSecondary)
                            if insight.isInferred {
                                Text(loc.t("exec.inferred"))
                                    .font(LxFont.mono(7))
                                    .foregroundColor(theme.textTertiary)
                                    .italic()
                            }
                        }
                        Spacer()
                    }
                    .padding(.vertical, 2)
                }
            }
        }
    }

    // MARK: - Empty State

    private var emptyStateCard: some View {
        GlassPanel(neon: LxColor.coolSteel) {
            VStack(spacing: 12) {
                Image(systemName: "chart.bar.doc.horizontal")
                    .font(.system(size: 32))
                    .foregroundColor(theme.textTertiary)
                Text(loc.t("exec.noData"))
                    .font(LxFont.mono(12, weight: .medium))
                    .foregroundColor(theme.textSecondary)
                Text(loc.t("exec.noDataDetail"))
                    .font(LxFont.mono(10))
                    .foregroundColor(theme.textTertiary)
                    .multilineTextAlignment(.center)
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, 20)
        }
    }
}
