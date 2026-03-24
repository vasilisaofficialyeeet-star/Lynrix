// ModelGovernanceView.swift — Model Governance Center (Lynrix v2.5 Sprint 5)
// Model identity, deployment status, performance metrics, health/drift indicators.

import SwiftUI

struct ModelGovernanceView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme

    private var gov: ModelGovernanceSnapshot {
        ModelGovernanceComputer.compute(
            accuracy: engine.accuracy,
            prediction: engine.prediction,
            strategyHealth: engine.strategyHealth,
            systemMonitor: engine.systemMonitor,
            featureImportance: engine.featureImportance,
            rlv2: engine.rlv2State,
            config: engine.config
        )
    }

    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                identityCard
                HStack(spacing: 12) {
                    healthCard
                    driftCard
                }
                HStack(spacing: 12) {
                    performanceCard
                    horizonCard
                }
                HStack(spacing: 12) {
                    f1Card
                    inferenceCard
                }
                componentScoresCard
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }

    // MARK: - Identity Card

    private var identityCard: some View {
        let id = gov.identity
        return GlassPanel(neon: id.deploymentStatus.color, padding: 10) {
            HStack {
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 8) {
                        Image(systemName: id.deploymentStatus.icon)
                            .font(.system(size: 14))
                            .foregroundColor(id.deploymentStatus.color)
                        Text(id.name)
                            .font(LxFont.mono(16, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                        Text(id.version)
                            .font(LxFont.mono(11))
                            .foregroundColor(theme.textTertiary)
                    }
                    HStack(spacing: 12) {
                        statusPill(loc.t(id.deploymentStatus.locKey), color: id.deploymentStatus.color)
                        statusPill(id.backend, color: LxColor.electricCyan)
                        statusPill(id.isOnnx ? loc.t("ai.onnx") : loc.t("model.native"), color: LxColor.gold)
                        statusPill("\(id.activeFeatures) \(loc.t("model.features"))", color: theme.textTertiary)
                    }
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 4) {
                    Text(String(format: "%.0f", gov.health.overallScore))
                        .font(LxFont.mono(28, weight: .bold))
                        .foregroundColor(healthScoreColor(gov.health.overallScore))
                        .shadow(color: healthScoreColor(gov.health.overallScore).opacity(0.3), radius: 4)
                    Text(loc.t("model.healthScore"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
            }
        }
    }

    private func statusPill(_ text: String, color: Color) -> some View {
        Text(text)
            .font(LxFont.mono(9, weight: .bold))
            .foregroundColor(color)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(RoundedRectangle(cornerRadius: 4).fill(color.opacity(0.1)))
            .overlay(RoundedRectangle(cornerRadius: 4).stroke(color.opacity(0.2), lineWidth: 0.5))
    }

    // MARK: - Health Card

    private var healthCard: some View {
        let h = gov.health
        return GlassPanel(neon: healthScoreColor(h.overallScore)) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("model.health"), icon: "heart.text.square", color: healthScoreColor(h.overallScore))
                healthRow(loc.t("model.accuracy"), ok: h.accuracyStable, detail: h.accuracyStable ? loc.t("model.stable") : loc.t("model.declining"))
                healthRow(loc.t("model.latency"), ok: h.latencyHealthy, detail: h.latencyHealthy ? loc.t("model.normal") : loc.t("model.elevated"))
                healthRow(loc.t("model.calibration"), ok: h.calibrationHealthy, detail: h.calibrationHealthy ? loc.t("model.good") : loc.t("model.poor"))
                healthRow(loc.t("model.confidence"), ok: h.confidenceHealthy, detail: h.confidenceHealthy ? loc.t("model.adequate") : loc.t("model.low"))
                healthRow(loc.t("model.rlStability"), ok: h.rlStable, detail: h.rlStable ? loc.t("model.stable") : loc.t("model.unstable"))
            }
        }
    }

    private func healthRow(_ label: String, ok: Bool, detail: String) -> some View {
        HStack {
            Image(systemName: ok ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                .font(.system(size: 9))
                .foregroundColor(ok ? LxColor.neonLime : LxColor.amber)
            Text(label)
                .font(LxFont.mono(10))
                .foregroundColor(theme.textSecondary)
            Spacer()
            Text(detail)
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(ok ? LxColor.neonLime : LxColor.amber)
        }
    }

    // MARK: - Drift Card

    private var driftCard: some View {
        let h = gov.health
        let p = gov.performance
        return GlassPanel(neon: h.driftLevel.color) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("model.drift"), icon: "waveform.path.ecg.rectangle", color: h.driftLevel.color)
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(loc.t("model.driftLevel"))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                        Text(loc.t(h.driftLevel.locKey))
                            .font(LxFont.mono(14, weight: .bold))
                            .foregroundColor(h.driftLevel.color)
                    }
                    Spacer()
                    VStack(alignment: .trailing, spacing: 4) {
                        Text(loc.t("model.calibError"))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                        Text(String(format: "%.3f", p.calibrationError))
                            .font(LxFont.mono(14, weight: .bold))
                            .foregroundColor(p.calibrationError < 0.08 ? LxColor.neonLime :
                                           (p.calibrationError < 0.15 ? LxColor.gold : LxColor.bloodRed))
                    }
                }
                GlassMetric(loc.t("model.rollingAcc"), value: String(format: "%.1f%%", p.rollingAccuracy * 100),
                            color: p.rollingAccuracy > 0.5 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("model.totalPreds"), value: "\(p.totalPredictions)", color: theme.textPrimary)
                if gov.uptimeHours > 0 {
                    GlassMetric(loc.t("model.uptime"), value: String(format: "%.1fh", gov.uptimeHours), color: theme.textSecondary)
                }
            }
        }
    }

    // MARK: - Performance Card

    private var performanceCard: some View {
        let p = gov.performance
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("model.performance"), icon: "chart.line.uptrend.xyaxis", color: LxColor.electricCyan)
                GlassMetric(loc.t("model.overallAcc"), value: String(format: "%.1f%%", p.overallAccuracy * 100),
                            color: p.overallAccuracy > 0.5 ? LxColor.neonLime : LxColor.magentaPink)
                GlassMetric(loc.t("model.rollingAcc"), value: String(format: "%.1f%%", p.rollingAccuracy * 100),
                            color: p.rollingAccuracy > 0.5 ? LxColor.neonLime : LxColor.magentaPink)
                GlassMetric(loc.t("model.confidence"), value: String(format: "%.1f%%", p.currentConfidence * 100),
                            color: p.currentConfidence > 0.5 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("model.avgF1"), value: String(format: "%.3f", p.avgF1),
                            color: p.avgF1 > 0.5 ? LxColor.neonLime : LxColor.amber)
            }
        }
    }

    // MARK: - Horizon Card

    private var horizonCard: some View {
        let p = gov.performance
        let horizons: [(String, Double)] = [
            ("100ms", p.horizonAccuracy100ms),
            ("500ms", p.horizonAccuracy500ms),
            ("1s", p.horizonAccuracy1s),
            ("3s", p.horizonAccuracy3s)
        ]
        return GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("model.horizons"), icon: "clock.arrow.2.circlepath", color: LxColor.gold)
                ForEach(horizons, id: \.0) { horizon in
                    horizonBar(horizon.0, accuracy: horizon.1, isBest: horizon.0 == p.bestHorizon)
                }
                HStack {
                    Text(loc.t("model.bestHorizon"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    Spacer()
                    Text("\(p.bestHorizon) (\(String(format: "%.1f%%", p.bestHorizonAccuracy * 100)))")
                        .font(LxFont.mono(10, weight: .bold))
                        .foregroundColor(LxColor.gold)
                }
            }
        }
    }

    private func horizonBar(_ label: String, accuracy: Double, isBest: Bool) -> some View {
        HStack(spacing: 6) {
            Text(label)
                .font(LxFont.mono(10, weight: isBest ? .bold : .regular))
                .foregroundColor(isBest ? LxColor.gold : theme.textSecondary)
                .frame(width: 40, alignment: .trailing)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2)
                        .fill(theme.panelBackground)
                        .frame(height: 6)
                    RoundedRectangle(cornerRadius: 2)
                        .fill(accuracy > 0.5 ? LxColor.neonLime : LxColor.amber)
                        .frame(width: max(0, geo.size.width * CGFloat(accuracy)), height: 6)
                }
            }
            .frame(height: 6)
            Text(String(format: "%.1f%%", accuracy * 100))
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(accuracy > 0.5 ? LxColor.neonLime : LxColor.amber)
                .frame(width: 40, alignment: .trailing)
        }
    }

    // MARK: - F1 Card

    private var f1Card: some View {
        let p = gov.performance
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("model.f1Scores"), icon: "target", color: LxColor.electricCyan)
                GlassMetric(loc.t("model.f1Up"), value: String(format: "%.3f", p.f1Up),
                            color: p.f1Up > 0.5 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("model.f1Down"), value: String(format: "%.3f", p.f1Down),
                            color: p.f1Down > 0.5 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("model.f1Flat"), value: String(format: "%.3f", p.f1Flat),
                            color: p.f1Flat > 0.5 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("model.avgF1"), value: String(format: "%.3f", p.avgF1),
                            color: p.avgF1 > 0.5 ? LxColor.neonLime : LxColor.amber)
            }
        }
    }

    // MARK: - Inference Card

    private var inferenceCard: some View {
        let p = gov.performance
        return GlassPanel(neon: LxColor.coolSteel) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("model.inference"), icon: "cpu", color: LxColor.coolSteel)
                GlassMetric(loc.t("model.latencyP50"), value: String(format: "%.0f µs", p.latencyP50Us), color: theme.textPrimary)
                GlassMetric(loc.t("model.latencyP99"), value: String(format: "%.0f µs", p.latencyP99Us),
                            color: p.latencyP99Us < 500 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("model.inferenceLatency"), value: String(format: "%.0f µs", p.inferenceLatencyUs), color: theme.textPrimary)
                GlassMetric(loc.t("model.backend"), value: gov.identity.backend, color: LxColor.electricCyan)
            }
        }
    }

    // MARK: - Component Scores

    private var componentScoresCard: some View {
        let h = gov.health
        let components: [(String, Double)] = [
            (loc.t("model.accuracy"), h.accuracyScore),
            (loc.t("model.calibration"), h.calibrationScore),
            (loc.t("model.latency"), h.latencyScore),
            (loc.t("model.confidence"), h.confidenceScore),
            (loc.t("model.rlStability"), h.stabilityScore)
        ]
        let weights = ["30%", "20%", "15%", "20%", "15%"]
        return GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("model.healthBreakdown"), icon: "chart.bar.fill", color: LxColor.gold)
                ForEach(Array(zip(components, weights)), id: \.0.0) { item in
                    let (label, score) = item.0
                    let weight = item.1
                    HStack(spacing: 6) {
                        Text(label)
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textSecondary)
                            .frame(width: 80, alignment: .trailing)
                        Text(weight)
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                            .frame(width: 26)
                        GeometryReader { geo in
                            ZStack(alignment: .leading) {
                                RoundedRectangle(cornerRadius: 2)
                                    .fill(theme.panelBackground)
                                    .frame(height: 8)
                                RoundedRectangle(cornerRadius: 2)
                                    .fill(healthScoreColor(score))
                                    .frame(width: max(0, geo.size.width * CGFloat(score / 100)), height: 8)
                            }
                        }
                        .frame(height: 8)
                        Text(String(format: "%.0f", score))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(healthScoreColor(score))
                            .frame(width: 28, alignment: .trailing)
                    }
                }
            }
        }
    }

    // MARK: - Helpers

    private func healthScoreColor(_ score: Double) -> Color {
        if score >= 70 { return LxColor.neonLime }
        if score >= 50 { return LxColor.gold }
        if score >= 30 { return LxColor.amber }
        return LxColor.bloodRed
    }
}
