// MLDiagnosticsView.swift — Glassmorphism 2026 ML Diagnostics (Lynrix v2.5)

import SwiftUI
import Charts

struct MLDiagnosticsView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    private let featureNames = [
        "imbalance_1", "imbalance_5", "imbalance_20", "ob_slope",
        "depth_conc", "cancel_spike", "liq_wall",
        "aggr_ratio", "avg_trade_sz", "trade_vel", "trade_accel", "vol_accel",
        "microprice", "spread_bps", "spread_chg", "mid_momentum", "volatility",
        "mp_dev", "st_pressure", "bid_depth", "ask_depth",
        "d_imb_dt", "d2_imb_dt2", "d_vol_dt", "d_mom_dt"
    ]
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                HStack(spacing: 12) {
                    accuracyOverviewCard
                    perClassCard
                }
                HStack(spacing: 12) {
                    horizonAccuracyCard
                    accuracyTrendCard
                }
                featureImportanceCard
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    private var accuracyOverviewCard: some View {
        let acc = engine.accuracy
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ml.modelAccuracy"), icon: "target", color: LxColor.electricCyan)
                GlassMetric(loc.t("ai.overall"), value: String(format: "%.1f%%", acc.accuracy * 100),
                            color: acc.accuracy > 0.4 ? LxColor.neonLime : (acc.accuracy < 0.33 ? LxColor.magentaPink : theme.textPrimary))
                GlassMetric(loc.t("ai.rolling"), value: String(format: "%.1f%%", acc.rollingAccuracy * 100),
                            color: acc.rollingAccuracy > 0.4 ? LxColor.neonLime : theme.textPrimary)
                GlassMetric(loc.t("ml.totalPredictions"), value: "\(acc.totalPredictions)", color: theme.textPrimary)
                GlassMetric(loc.t("ai.correct"), value: "\(acc.correctPredictions)", color: LxColor.neonLime)
                GlassMetric(loc.t("ml.calibrationError"), value: String(format: "%.4f", acc.calibrationError),
                            color: acc.calibrationError > 0.1 ? LxColor.amber : theme.textPrimary)
                GlassMetric(loc.t("ml.rollingWindow"), value: "\(acc.rollingWindow)", color: theme.textPrimary)
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                GlassMetric(loc.t("ml.backend"), value: acc.usingOnnx ? loc.t("ml.onnxRuntime") : loc.t("ml.nativeGru"),
                            color: acc.usingOnnx ? LxColor.magentaPink : LxColor.electricCyan)
                GlassMetric(loc.t("ml.inferenceLabel"), value: String(format: "%.0f us", engine.prediction.inferenceLatencyUs), color: LxColor.electricCyan)
            }
        }
    }
    
    private var perClassCard: some View {
        let acc = engine.accuracy
        return GlassPanel(neon: LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ml.perClassMetrics"), icon: "chart.bar.doc.horizontal", color: LxColor.magentaPink)
                HStack {
                    Text("").frame(width: 60)
                    Text(loc.t("ml.precision")).font(LxFont.mono(9, weight: .bold)).frame(width: 60)
                    Text(loc.t("ml.recall")).font(LxFont.mono(9, weight: .bold)).frame(width: 60)
                    Text(loc.t("ml.f1Score")).font(LxFont.mono(9, weight: .bold)).frame(width: 60)
                }
                .foregroundColor(theme.textTertiary)
                classRow("UP", precision: acc.precisionUp, recall: acc.recallUp, f1: acc.f1Up, color: LxColor.neonLime)
                classRow("DOWN", precision: acc.precisionDown, recall: acc.recallDown, f1: acc.f1Down, color: LxColor.magentaPink)
                classRow("FLAT", precision: acc.precisionFlat, recall: acc.recallFlat, f1: acc.f1Flat, color: LxColor.electricCyan)
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                let pred = engine.prediction
                HStack(spacing: 4) {
                    Text(loc.t("ml.current"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    predBar("↑", value: pred.probUp, color: LxColor.neonLime)
                    predBar("→", value: 1 - pred.probUp - pred.probDown, color: LxColor.electricCyan)
                    predBar("↓", value: pred.probDown, color: LxColor.magentaPink)
                }
                GlassMetric(loc.t("ml.confidence"), value: String(format: "%.1f%%", pred.modelConfidence * 100),
                            color: pred.modelConfidence > 0.7 ? LxColor.neonLime : LxColor.coolSteel)
            }
        }
    }
    
    private var horizonAccuracyCard: some View {
        let acc = engine.accuracy
        return GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ml.horizonAccuracy"), icon: "clock.arrow.2.circlepath", color: LxColor.gold)
                neonHorizonBar("100ms", accuracy: acc.horizonAccuracy100ms)
                neonHorizonBar("500ms", accuracy: acc.horizonAccuracy500ms)
                neonHorizonBar("1s", accuracy: acc.horizonAccuracy1s)
                neonHorizonBar("3s", accuracy: acc.horizonAccuracy3s)
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                let pred = engine.prediction
                VStack(alignment: .leading, spacing: 4) {
                    Text(loc.t("ml.multiHorizonProb"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    horizonProbRow("100ms", up: pred.h100ms.up, down: pred.h100ms.down)
                    horizonProbRow("500ms", up: pred.h500ms.up, down: pred.h500ms.down)
                    horizonProbRow("1s", up: pred.h1s.up, down: pred.h1s.down)
                    horizonProbRow("3s", up: pred.h3s.up, down: pred.h3s.down)
                }
            }
        }
    }
    
    private var accuracyTrendCard: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ml.accuracyTrend"), icon: "chart.line.uptrend.xyaxis", color: LxColor.electricCyan)
                if #available(macOS 14.0, *), !engine.accuracyHistory.isEmpty {
                    Chart {
                        ForEach(Array(engine.accuracyHistory.enumerated()), id: \.offset) { idx, val in
                            LineMark(x: .value("T", idx), y: .value("Acc", val * 100))
                                .foregroundStyle(LxColor.electricCyan)
                        }
                        RuleMark(y: .value("Random", 33.3))
                            .foregroundStyle(LxColor.magentaPink.opacity(0.5))
                            .lineStyle(StrokeStyle(dash: [4, 4]))
                    }
                    .frame(height: 140)
                    .chartYAxis { AxisMarks(position: .leading) }
                    .chartYScale(domain: 0...100)
                } else {
                    RoundedRectangle(cornerRadius: 6)
                        .fill(theme.glassHighlight)
                        .frame(height: 100)
                        .overlay(
                            Text(loc.t("ai.noData"))
                                .font(LxFont.label)
                                .foregroundColor(theme.textTertiary)
                        )
                }
            }
        }
    }
    
    private var featureImportanceCard: some View {
        let fi = engine.featureImportance
        return GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("ml.featureImportance"), icon: "chart.bar.fill", color: LxColor.neonLime)
                    Spacer()
                    Text("\(fi.activeFeatures) \(loc.t("ml.significant"))")
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
                let topFeatures = Array(fi.ranking.prefix(10))
                if #available(macOS 14.0, *) {
                    Chart {
                        ForEach(topFeatures.prefix(10), id: \.self) { idx in
                            let name = idx < featureNames.count ? featureNames[idx] : "f\(idx)"
                            let importance = idx < fi.mutualInformation.count ? fi.mutualInformation[idx] : 0
                            BarMark(x: .value("MI", importance), y: .value("Feature", name))
                                .foregroundStyle(
                                    LinearGradient(colors: [LxColor.neonLime.opacity(0.6), LxColor.electricCyan.opacity(0.3)],
                                                   startPoint: .leading, endPoint: .trailing)
                                )
                        }
                    }
                    .frame(height: 200)
                    .chartXAxisLabel(loc.t("ml.mutualInformation"))
                } else {
                    ForEach(topFeatures.prefix(10), id: \.self) { idx in
                        let name = idx < featureNames.count ? featureNames[idx] : "f\(idx)"
                        let corr = idx < fi.correlation.count ? fi.correlation[idx] : 0
                        let mi = idx < fi.mutualInformation.count ? fi.mutualInformation[idx] : 0
                        HStack {
                            Text(name).font(LxFont.mono(10)).foregroundColor(theme.textSecondary).frame(width: 100, alignment: .leading)
                            Text(String(format: "MI=%.4f", mi)).font(LxFont.mono(9)).foregroundColor(LxColor.electricCyan)
                            Text(String(format: "r=%.3f", corr)).font(LxFont.mono(9))
                                .foregroundColor(corr > 0 ? LxColor.neonLime : LxColor.magentaPink)
                        }
                    }
                }
            }
        }
    }
    
    // MARK: - Helpers
    
    private func classRow(_ label: String, precision: Double, recall: Double, f1: Double, color: Color) -> some View {
        HStack {
            Text(label).font(LxFont.mono(10, weight: .bold)).foregroundColor(color)
                .shadow(color: color.opacity(0.3), radius: 2)
                .frame(width: 60, alignment: .leading)
            Text(String(format: "%.1f%%", precision * 100)).font(LxFont.mono(10)).foregroundColor(theme.textSecondary).frame(width: 60)
            Text(String(format: "%.1f%%", recall * 100)).font(LxFont.mono(10)).foregroundColor(theme.textSecondary).frame(width: 60)
            Text(String(format: "%.3f", f1)).font(LxFont.mono(10)).foregroundColor(theme.textSecondary).frame(width: 60)
        }
    }
    
    private func predBar(_ label: String, value: Double, color: Color) -> some View {
        VStack(spacing: 2) {
            Text(label).font(LxFont.mono(9)).foregroundColor(color)
            Text(String(format: "%.0f%%", value * 100)).font(LxFont.mono(9)).foregroundColor(color)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 4)
        .background(color.opacity(max(value * 0.4, 0.05)))
        .clipShape(RoundedRectangle(cornerRadius: 4))
    }
    
    private func neonHorizonBar(_ label: String, accuracy: Double) -> some View {
        let barCol = accuracy > 0.4 ? LxColor.neonLime : (accuracy > 0.33 ? LxColor.gold : LxColor.bloodRed)
        return HStack(spacing: 6) {
            Text(label).font(LxFont.mono(10)).foregroundColor(theme.textTertiary).frame(width: 50, alignment: .leading)
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 5).fill(barCol.opacity(0.08)).frame(height: 10)
                    RoundedRectangle(cornerRadius: 5)
                        .fill(LinearGradient(colors: [barCol.opacity(0.5), barCol.opacity(0.2)],
                                             startPoint: .leading, endPoint: .trailing))
                        .frame(width: max(geo.size.width * accuracy, 2), height: 10)
                        .shadow(color: barCol.opacity(0.2), radius: 2)
                    Rectangle().fill(LxColor.bloodRed.opacity(0.5)).frame(width: 1, height: 14)
                        .offset(x: geo.size.width * 0.333)
                }
            }
            .frame(height: 14)
            Text(String(format: "%.1f%%", accuracy * 100)).font(LxFont.mono(9)).foregroundColor(barCol).frame(width: 40, alignment: .trailing)
        }
    }
    
    private func horizonProbRow(_ label: String, up: Double, down: Double) -> some View {
        HStack(spacing: 4) {
            Text(label).font(LxFont.mono(9)).foregroundColor(theme.textTertiary).frame(width: 40, alignment: .leading)
            Text(String(format: "↑%.0f%%", up * 100)).font(LxFont.mono(9)).foregroundColor(LxColor.neonLime).frame(width: 40)
            Text(String(format: "↓%.0f%%", down * 100)).font(LxFont.mono(9)).foregroundColor(LxColor.magentaPink).frame(width: 40)
            let dir = up > down + 0.05 ? "↑" : (down > up + 0.05 ? "↓" : "→")
            Text(dir).font(LxFont.mono(10)).foregroundColor(theme.textSecondary)
        }
    }
}
