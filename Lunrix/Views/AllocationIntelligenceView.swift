// AllocationIntelligenceView.swift — Allocation Intelligence Center (Lynrix v2.5 Sprint 5)
// Regime-aware allocation recommendations, risk budget, signal quality, and position sizing hints.

import SwiftUI

struct AllocationIntelligenceView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme

    private var rec: AllocationRecommendation {
        AllocationIntelligenceComputer.compute(
            regime: engine.regime,
            prediction: engine.prediction,
            threshold: engine.threshold,
            strategyMetrics: engine.strategyMetrics,
            strategyHealth: engine.strategyHealth,
            circuitBreaker: engine.circuitBreaker,
            position: engine.position,
            varState: engine.varState,
            executionAnalytics: engine.executionAnalytics,
            config: engine.config
        )
    }

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                signalCard
                HStack(spacing: 12) {
                    riskBudgetCard
                    signalQualityCard
                }
                HStack(spacing: 12) {
                    regimeContextCard
                    positionSizingCard
                }
                reasonsCard
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }

    // MARK: - Signal Card

    private var signalCard: some View {
        let s = rec.signal
        return GlassPanel(neon: s.color, padding: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 8) {
                        Image(systemName: s.icon)
                            .font(.system(size: 18, weight: .bold))
                            .foregroundColor(s.color)
                            .shadow(color: s.color.opacity(0.4), radius: 4)
                        Text(loc.t(s.locKey))
                            .font(LxFont.mono(18, weight: .bold))
                            .foregroundColor(s.color)
                    }
                    HStack(spacing: 12) {
                        miniLabel(loc.t("alloc.confidence"),
                                  String(format: "%.0f%%", rec.confidenceInSignal * 100),
                                  rec.confidenceInSignal > 0.6 ? LxColor.neonLime : LxColor.amber)
                        miniLabel(loc.t("alloc.posScale"),
                                  String(format: "%.2fx", rec.suggestedPositionScale),
                                  rec.suggestedPositionScale >= 1.0 ? LxColor.neonLime : LxColor.amber)
                    }
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 4) {
                    Text(String(format: "%.0f", rec.compositeScore))
                        .font(LxFont.mono(32, weight: .bold))
                        .foregroundColor(compositeColor(rec.compositeScore))
                        .shadow(color: compositeColor(rec.compositeScore).opacity(0.3), radius: 4)
                    Text(loc.t("alloc.compositeScore"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
            }
        }
    }

    private func miniLabel(_ label: String, _ value: String, _ color: Color) -> some View {
        VStack(spacing: 2) {
            Text(label)
                .font(LxFont.mono(8, weight: .bold))
                .foregroundColor(theme.textTertiary)
            Text(value)
                .font(LxFont.mono(11, weight: .bold))
                .foregroundColor(color)
        }
    }

    // MARK: - Risk Budget Card

    private var riskBudgetCard: some View {
        let b = rec.riskBudget
        return GlassPanel(neon: b.overallUtilization > 0.7 ? LxColor.bloodRed : LxColor.amber) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("alloc.riskBudget"), icon: "shield.lefthalf.filled",
                                   color: b.overallUtilization > 0.7 ? LxColor.bloodRed : LxColor.amber)
                budgetBar(loc.t("alloc.drawdown"), b.drawdownUsedPct)
                budgetBar(loc.t("alloc.dailyLoss"), b.dailyLossUsedPct)
                budgetBar(loc.t("alloc.position"), b.positionUsedPct)
                budgetBar(loc.t("alloc.leverage"), b.leverageUsedPct)
                budgetBar(loc.t("alloc.var"), b.varUsedPct)
                HStack {
                    Text(loc.t("alloc.remaining"))
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textSecondary)
                    Spacer()
                    Text(String(format: "%.0f%%", b.budgetRemaining * 100))
                        .font(LxFont.mono(12, weight: .bold))
                        .foregroundColor(b.budgetRemaining > 0.5 ? LxColor.neonLime :
                                        (b.budgetRemaining > 0.2 ? LxColor.amber : LxColor.bloodRed))
                }
            }
        }
    }

    private func budgetBar(_ label: String, _ used: Double) -> some View {
        let clamped = min(1, max(0, used))
        let color: Color = clamped < 0.5 ? LxColor.neonLime : (clamped < 0.8 ? LxColor.amber : LxColor.bloodRed)
        return HStack(spacing: 6) {
            Text(label)
                .font(LxFont.mono(9))
                .foregroundColor(theme.textSecondary)
                .frame(width: 60, alignment: .trailing)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2).fill(theme.panelBackground).frame(height: 6)
                    RoundedRectangle(cornerRadius: 2).fill(color)
                        .frame(width: max(0, geo.size.width * CGFloat(clamped)), height: 6)
                }
            }
            .frame(height: 6)
            Text(String(format: "%.0f%%", clamped * 100))
                .font(LxFont.mono(8, weight: .bold))
                .foregroundColor(color)
                .frame(width: 30, alignment: .trailing)
        }
    }

    // MARK: - Signal Quality Card

    private var signalQualityCard: some View {
        let sq = rec.signalQuality
        let gradeColor = gradeColor(sq.qualityGrade)
        return GlassPanel(neon: gradeColor) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("alloc.signalQuality"), icon: "antenna.radiowaves.left.and.right", color: gradeColor)
                HStack {
                    Text(loc.t("alloc.grade"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    Spacer()
                    Text(sq.qualityGrade)
                        .font(LxFont.mono(20, weight: .bold))
                        .foregroundColor(gradeColor)
                }
                GlassMetric(loc.t("alloc.recentAccuracy"), value: String(format: "%.1f%%", sq.recentAccuracy * 100),
                            color: sq.recentAccuracy > 0.5 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("alloc.modelConf"), value: String(format: "%.1f%%", sq.modelConfidence * 100),
                            color: sq.modelConfidence > 0.5 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("alloc.regimeConf"), value: String(format: "%.1f%%", sq.regimeConfidence * 100),
                            color: sq.regimeConfidence > 0.6 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("alloc.execScore"), value: String(format: "%.0f%%", sq.executionScore * 100),
                            color: sq.executionScore > 0.7 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("alloc.efficiency"), value: String(format: "%.1f%%", sq.signalEfficiency * 100),
                            color: sq.signalEfficiency > 0.5 ? LxColor.neonLime : LxColor.amber)
            }
        }
    }

    // MARK: - Regime Context Card

    private var regimeContextCard: some View {
        let rc = rec.regimeContext
        let rColor = LxColor.regime(rc.regime)
        return GlassPanel(neon: rColor) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("alloc.regimeContext"), icon: "waveform.path.ecg", color: rColor)
                HStack {
                    RegimeBadge(regime: rc.regime, confidence: engine.regime.confidence)
                    Spacer()
                    Image(systemName: rc.regimeFavorable ? "checkmark.circle.fill" : "xmark.circle.fill")
                        .foregroundColor(rc.regimeFavorable ? LxColor.neonLime : LxColor.amber)
                    Text(rc.regimeFavorable ? loc.t("alloc.favorable") : loc.t("alloc.unfavorable"))
                        .font(LxFont.mono(10, weight: .bold))
                        .foregroundColor(rc.regimeFavorable ? LxColor.neonLime : LxColor.amber)
                }
                GlassMetric(loc.t("alloc.sizeScale"), value: String(format: "%.1fx", rc.suggestedSizeScale),
                            color: rc.suggestedSizeScale >= 1.0 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("alloc.thresholdAdj"), value: String(format: "%+.2f", rc.suggestedThresholdAdj),
                            color: rc.suggestedThresholdAdj <= 0 ? LxColor.neonLime : LxColor.amber)
                if !rc.rationale.isEmpty {
                    Text(loc.t(rc.rationale))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                        .padding(.top, 2)
                }
            }
        }
    }

    // MARK: - Position Sizing Card

    private var positionSizingCard: some View {
        GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("alloc.positionSizing"), icon: "slider.horizontal.3", color: LxColor.gold)
                GlassMetric(loc.t("alloc.suggestedScale"),
                            value: String(format: "%.2fx", rec.suggestedPositionScale),
                            color: rec.suggestedPositionScale >= 1.0 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("alloc.currentPos"),
                            value: String(format: "%.4f", abs(engine.position.size)),
                            color: theme.textPrimary)
                GlassMetric(loc.t("alloc.maxPos"),
                            value: String(format: "%.4f", engine.config.maxPositionSize),
                            color: theme.textSecondary)
                let effective = engine.config.orderQty * rec.suggestedPositionScale
                GlassMetric(loc.t("alloc.effectiveQty"),
                            value: String(format: "%.6f", effective),
                            color: LxColor.gold)
            }
        }
    }

    // MARK: - Reasons Card

    private var reasonsCard: some View {
        GlassPanel(neon: LxColor.coolSteel) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("alloc.rationale"), icon: "list.bullet.rectangle", color: LxColor.coolSteel)
                if rec.reasons.isEmpty {
                    Text(loc.t("alloc.noReasons"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                } else {
                    ForEach(rec.reasons, id: \.self) { reasonKey in
                        HStack(spacing: 6) {
                            Image(systemName: "arrow.right.circle.fill")
                                .font(.system(size: 9))
                                .foregroundColor(rec.signal.color)
                            Text(loc.t(reasonKey))
                                .font(LxFont.mono(10))
                                .foregroundColor(theme.textSecondary)
                        }
                    }
                }
            }
        }
    }

    // MARK: - Helpers

    private func compositeColor(_ score: Double) -> Color {
        if score >= 70 { return LxColor.neonLime }
        if score >= 50 { return LxColor.gold }
        if score >= 30 { return LxColor.amber }
        return LxColor.bloodRed
    }

    private func gradeColor(_ grade: String) -> Color {
        switch grade {
        case "A": return LxColor.neonLime
        case "B": return LxColor.electricCyan
        case "C": return LxColor.gold
        case "D": return LxColor.amber
        default:  return LxColor.bloodRed
        }
    }
}
