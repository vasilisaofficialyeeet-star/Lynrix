// MarketRegimeCenterView.swift — Market Regime Center (Lynrix v2.5 Sprint 5)
// Centralized regime analysis: current state, transitions, stability, distribution, performance correlation.

import SwiftUI

struct MarketRegimeCenterView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @ObservedObject var store = MarketRegimeIntelligenceStore.shared
    @Environment(\.theme) var theme

    private var snap: MarketRegimeIntelligenceSnapshot { store.snapshot }

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                currentRegimeCard
                HStack(spacing: 12) {
                    stabilityCard
                    scoresCard
                }
                distributionCard
                if !snap.performanceByRegime.isEmpty {
                    regimePerformanceCard
                }
                if !snap.recentTransitions.isEmpty {
                    transitionHistoryCard
                }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }

    // MARK: - Current Regime

    private var currentRegimeCard: some View {
        let rColor = LxColor.regime(snap.currentRegime)
        return GlassPanel(neon: rColor, padding: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 8) {
                        RegimeBadge(regime: snap.currentRegime, confidence: snap.confidence)
                        if snap.regimeChanged {
                            Image(systemName: "arrow.triangle.2.circlepath")
                                .font(.system(size: 10))
                                .foregroundColor(LxColor.amber)
                        }
                    }
                    HStack(spacing: 12) {
                        miniMetric(loc.t("regime.confidence"), String(format: "%.0f%%", snap.confidence * 100),
                                   snap.confidence > 0.6 ? LxColor.neonLime : LxColor.amber)
                        miniMetric(loc.t("regime.volatility"), String(format: "%.4f", snap.volatility), theme.textPrimary)
                        miniMetric(loc.t("regime.duration"),
                                   formatDuration(snap.stability.currentDurationSec), theme.textSecondary)
                    }
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 4) {
                    Text(String(format: "%.0f", snap.stability.stabilityScore))
                        .font(LxFont.mono(28, weight: .bold))
                        .foregroundColor(snap.stability.isStable ? LxColor.neonLime : LxColor.amber)
                        .shadow(color: (snap.stability.isStable ? LxColor.neonLime : LxColor.amber).opacity(0.3), radius: 4)
                    Text(loc.t("regime.stabilityScore"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
            }
        }
    }

    private func miniMetric(_ label: String, _ value: String, _ color: Color) -> some View {
        VStack(spacing: 2) {
            Text(label)
                .font(LxFont.mono(8, weight: .bold))
                .foregroundColor(theme.textTertiary)
            Text(value)
                .font(LxFont.mono(11, weight: .bold))
                .foregroundColor(color)
        }
    }

    // MARK: - Stability Card

    private var stabilityCard: some View {
        let s = snap.stability
        return GlassPanel(neon: s.isStable ? LxColor.neonLime : LxColor.amber) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("regime.stability"), icon: "metronome", color: s.isStable ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("regime.transitionsHour"), value: "\(s.transitionsLastHour)",
                            color: s.transitionsLastHour < 5 ? LxColor.neonLime :
                                  (s.transitionsLastHour < 10 ? LxColor.gold : LxColor.bloodRed))
                GlassMetric(loc.t("regime.avgDuration"), value: formatDuration(s.avgDurationSec), color: theme.textPrimary)
                GlassMetric(loc.t("regime.currentDuration"), value: formatDuration(s.currentDurationSec), color: theme.textPrimary)
                GlassMetric(loc.t("regime.status"),
                            value: s.isStable ? loc.t("regime.stableLabel") : loc.t("regime.unstableLabel"),
                            color: s.isStable ? LxColor.neonLime : LxColor.amber)
            }
        }
    }

    // MARK: - Scores Card

    private var scoresCard: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("regime.detectorScores"), icon: "waveform.path.ecg", color: LxColor.electricCyan)
                scoreBar(loc.t("regime.trendScore"), value: snap.trendScore, color: LxColor.neonLime)
                scoreBar(loc.t("regime.mrScore"), value: snap.mrScore, color: LxColor.electricCyan)
                scoreBar(loc.t("regime.liqScore"), value: snap.liqScore, color: LxColor.magentaPink)
                if snap.previousRegime != snap.currentRegime {
                    HStack {
                        Text(loc.t("regime.previous"))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                        Spacer()
                        Text(loc.t(LxColor.regimeLocKey(snap.previousRegime)))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(LxColor.regime(snap.previousRegime))
                    }
                }
            }
        }
    }

    private func scoreBar(_ label: String, value: Double, color: Color) -> some View {
        HStack(spacing: 6) {
            Text(label)
                .font(LxFont.mono(10))
                .foregroundColor(theme.textSecondary)
                .frame(width: 60, alignment: .trailing)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2).fill(theme.panelBackground).frame(height: 6)
                    RoundedRectangle(cornerRadius: 2).fill(color)
                        .frame(width: max(0, geo.size.width * CGFloat(min(1, max(0, value)))), height: 6)
                }
            }
            .frame(height: 6)
            Text(String(format: "%.2f", value))
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(color)
                .frame(width: 32, alignment: .trailing)
        }
    }

    // MARK: - Distribution Card

    private var distributionCard: some View {
        GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("regime.distribution"), icon: "chart.pie", color: LxColor.gold)
                ForEach(snap.distribution) { entry in
                    HStack(spacing: 8) {
                        Circle()
                            .fill(LxColor.regime(entry.regime))
                            .frame(width: 8, height: 8)
                        Text(loc.t(LxColor.regimeLocKey(entry.regime)))
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textSecondary)
                            .frame(width: 80, alignment: .leading)
                        GeometryReader { geo in
                            ZStack(alignment: .leading) {
                                RoundedRectangle(cornerRadius: 2).fill(theme.panelBackground).frame(height: 8)
                                RoundedRectangle(cornerRadius: 2)
                                    .fill(LxColor.regime(entry.regime).opacity(0.7))
                                    .frame(width: max(0, geo.size.width * CGFloat(entry.percentage)), height: 8)
                            }
                        }
                        .frame(height: 8)
                        Text(String(format: "%.1f%%", entry.percentage * 100))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(LxColor.regime(entry.regime))
                            .frame(width: 40, alignment: .trailing)
                    }
                }
                if let dominant = snap.distribution.max(by: { $0.tickCount < $1.tickCount }), dominant.tickCount > 0 {
                    HStack {
                        Text(loc.t("regime.dominant"))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                        Spacer()
                        Text(loc.t(LxColor.regimeLocKey(dominant.regime)))
                            .font(LxFont.mono(10, weight: .bold))
                            .foregroundColor(LxColor.regime(dominant.regime))
                    }
                }
            }
        }
    }

    // MARK: - Regime Performance

    private var regimePerformanceCard: some View {
        GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("regime.performance"), icon: "chart.bar.xaxis", color: LxColor.neonLime)
                ForEach(snap.performanceByRegime, id: \.regime) { entry in
                    HStack(spacing: 8) {
                        Circle()
                            .fill(LxColor.regime(entry.regime))
                            .frame(width: 8, height: 8)
                        Text(loc.t(LxColor.regimeLocKey(entry.regime)))
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textSecondary)
                            .frame(width: 80, alignment: .leading)
                        Text("\(entry.tradeCount)")
                            .font(LxFont.mono(9))
                            .foregroundColor(theme.textTertiary)
                            .frame(width: 24)
                        Text(String(format: "%.0f%%", entry.winRate * 100))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(entry.winRate > 0.5 ? LxColor.neonLime : LxColor.magentaPink)
                            .frame(width: 36)
                        Spacer()
                        Text(String(format: "%+.4f", entry.totalPnl))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(entry.totalPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink)
                            .frame(width: 70, alignment: .trailing)
                    }
                }
            }
        }
    }

    // MARK: - Transition History

    private var transitionHistoryCard: some View {
        GlassPanel(neon: LxColor.amber) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("regime.transitions"), icon: "arrow.triangle.2.circlepath", color: LxColor.amber)
                ForEach(snap.recentTransitions.prefix(12)) { t in
                    HStack(spacing: 6) {
                        Text(formatTime(t.timestamp))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                            .frame(width: 55, alignment: .leading)
                        Circle()
                            .fill(LxColor.regime(t.fromRegime))
                            .frame(width: 6, height: 6)
                        Text(loc.t(LxColor.regimeLocKey(t.fromRegime)))
                            .font(LxFont.mono(9))
                            .foregroundColor(LxColor.regime(t.fromRegime))
                            .frame(width: 70, alignment: .leading)
                        Image(systemName: "arrow.right")
                            .font(.system(size: 8))
                            .foregroundColor(theme.textTertiary)
                        Circle()
                            .fill(LxColor.regime(t.toRegime))
                            .frame(width: 6, height: 6)
                        Text(loc.t(LxColor.regimeLocKey(t.toRegime)))
                            .font(LxFont.mono(9))
                            .foregroundColor(LxColor.regime(t.toRegime))
                        Spacer()
                        Text(String(format: "%.0f%%", t.confidenceAtTransition * 100))
                            .font(LxFont.mono(8))
                            .foregroundColor(theme.textTertiary)
                    }
                }
            }
        }
    }

    // MARK: - Helpers

    private func formatTime(_ date: Date) -> String {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f.string(from: date)
    }

    private func formatDuration(_ sec: Double) -> String {
        if sec < 60 { return String(format: "%.0fs", sec) }
        if sec < 3600 { return String(format: "%.1fm", sec / 60) }
        return String(format: "%.1fh", sec / 3600)
    }
}
